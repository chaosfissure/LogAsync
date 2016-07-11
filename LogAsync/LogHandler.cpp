#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include "LogHandler.h"

constexpr size_t BUFFER_SIZE = 4096;

// ---------------------------------------------------------------------------------
// Implementation for LogBase
// ---------------------------------------------------------------------------------
LogBase::LogBase() :
    _filterLock(),
    _configLock(),
	_useCache(true),
    _localQuitLogging(false),
    _config(),
    _inputFilters(),
	_sourceEvalCache()
{}

LogBase::~LogBase() 
{
    _localQuitLogging = true;
}

void LogBase::DisableCache() 
{
	std::lock_guard<std::mutex> lock(_filterLock);
	_sourceEvalCache.clear();
	_useCache = false; 
}
void LogBase::EnableCache() 
{
	std::lock_guard<std::mutex> lock(_filterLock);
	_useCache = true; 
}

// ---------------------------------------------------------------------------------
// ASSUMES THAT WE HAVE A LOCK ACQUIRED.  This does not lock for performance reasons,
// and assumes that nothing will modify _inputFilters or _sourceEvalCache!
// ---------------------------------------------------------------------------------
bool LogBase::MeetsLoggingCriteria(const LogData& l)
{
	// Don't even lookup matches if we know everything's going to be logged.
	if (_inputFilters.empty()) { return true; }

	// Check and see if the value is cached already.
	auto cachePos = _sourceEvalCache.find(l._codeSrc);
	if (cachePos == _sourceEvalCache.end())
	{
		for (auto & filter : _inputFilters)
		{
			if (filter(l)) 
			{
				// Cache the value we've added since now that
				// we know the line matches the criteria.
				if (_useCache) { _sourceEvalCache[l._codeSrc] = true; }
				return true;
			}
		}

		// No match, this line is false in the cache.
		if (_useCache) { _sourceEvalCache[l._codeSrc] = false; }
		return false;
	}

	// Return the cached value.
	return cachePos->second;
}

// ---------------------------------------------------------------------------------
// Clears source cache because we're adding new filters that might affect the
// result, and adds the new filter to the input list.
// ---------------------------------------------------------------------------------
void LogBase::AddInputFilter(FilterType&& func)
{
    std::lock_guard<std::mutex> lock(_filterLock);
    _inputFilters.emplace_back(func);
	_sourceEvalCache.clear();
}

// ---------------------------------------------------------------------------------
// Clears source cache because we're adding new filters that might affect the
// result, and adds the new filter to the input list.
// ---------------------------------------------------------------------------------
void LogBase::AddExclusiveInputFilter(FilterType&& func)
{
    ClearAllFilters();
    AddInputFilter(std::move(func));
	_sourceEvalCache.clear();
}

// ---------------------------------------------------------------------------------
// Clears source cache because we're removing all filters.
// ---------------------------------------------------------------------------------
void LogBase::ClearAllFilters()
{
    std::lock_guard<std::mutex> lock(_filterLock);
    _inputFilters.clear();
	_sourceEvalCache.clear();
}

void LogBase::SetConfiguration(const std::string& logformat, const std::string& dateformat)
{
    std::lock_guard<std::mutex> lock(_configLock);
	_config.SetLogFormat(logformat, dateformat);
}

// ---------------------------------------------------------------------------------
// Implementation for RotatedLog
// ---------------------------------------------------------------------------------
RotatedLog::RotatedLog(const std::string& baseName) :
    LogBase(),

    _activeFileSize(0),
    _filename(baseName),

	_diskIsFull(false),
	_diskThreshold(100.0),

    _rotationType(ROTATION_METHOD::NO_ROTATION),
    _maxFilesizeBytes(0),

    _rotateIntervalSeconds(0),
    _numToRotateThrough(0),

    _rotationHMS({0,0,0}),
    _lastRotatedAt(system_clock::now()),

    _monitorRotation(nullptr),
	_monitorDiskSpace(nullptr)
{
    if (_filename.empty())
    {
        _filename = "Unknown." + boost::lexical_cast<std::string>(time(0)) + ".log";
    }

	_monitorDiskSpace = std::make_unique<ThreadRAII>(&RotatedLog::CheckDiskSpace, this);
}

RotatedLog::~RotatedLog() 
{
    _localQuitLogging = true;
}

void RotatedLog::RenameExistingLogs() const
{
    auto pathOfFile = boost::filesystem::path(_filename).parent_path();
    const std::string baseFileName = boost::filesystem::path(_filename).filename().string();

    // We're only keeping track of a max number of logfiles.  If there is an "ending" log already present,
    // it'll need to be deleted so that we can shift newer logs into its position.

    std::string deletionCandidate = baseFileName + "." + boost::lexical_cast<std::string>(_numToRotateThrough - 1);
    if (boost::filesystem::exists(pathOfFile / deletionCandidate))
    {
        try
        {
            boost::filesystem::remove(pathOfFile / deletionCandidate);
        }
        catch (const boost::filesystem::filesystem_error& e)
        {
            std::cerr << e.what() << std::endl;
        }
    }

    // Any filename.n will be renamed to filename.n+1 in reverse order, unless it's the nth entry.
    // If we're on the inital entry, it will be given a .1 prefix.

    for (int i = _numToRotateThrough - 1; i > 0; --i)
    {
        const std::string renameCandidate = baseFileName + "." + boost::lexical_cast<std::string>(i);
        const std::string renameTo = baseFileName + "." + boost::lexical_cast<std::string>(i + 1);
        if (boost::filesystem::exists(pathOfFile / renameCandidate))
        {
            try
            {
                boost::filesystem::rename(pathOfFile / renameCandidate, 
                                          pathOfFile / renameTo);
            }
            catch (const boost::filesystem::filesystem_error& e)
            {
                std::cerr << e.what() << std::endl;
            }
        }
    }

    // Rename the base file.  It seems like this throws a lot of false positives when
    // running through Visual Studio but running it straight as an application has no
    // problems.  Visual Studio's IDE probably sandboxes around the IOStream methods,
    // so boost filesystem calls that expect for files to be totally released or unlocked
    // may not synchronize entirely with the state of the file in the application...
    try
    {
        auto basefile = pathOfFile / baseFileName;
        auto dotOne = pathOfFile / (baseFileName + ".1");

        if (boost::filesystem::exists(basefile))
        {
            boost::filesystem::rename(pathOfFile / baseFileName,
                                      pathOfFile / (baseFileName + ".1"));
        }
    }
    catch (const boost::filesystem::filesystem_error& e)
    {
        std::cerr << e.what() << std::endl;
    }
}


// ---------------------------------------------------------------------------
// Close any currently existing log, open a log with the provided name, and
// update statistics related to loading log files.
// ---------------------------------------------------------------------------
void RotatedLog::OpenLog(const std::string& name)
{
    _logfile.close();
    _activeFileSize = 0;

    _logfile.open(name, std::ios::app);
    _logfile.sync_with_stdio(false);

    _lastRotatedAt = system_clock::now();

    if (!_logfile.is_open())
    {
        std::cerr << "ERROR - Unable to open " << name << " for logging!" << std::endl;
    }

    StatFile(name);
}

// ---------------------------------------------------------------------------
// Get the size of a given file.  It's expected to be the file currently in 
// use by the system, which may differ from the base filename var stored
// by the class.
// ---------------------------------------------------------------------------
void RotatedLog::StatFile(const std::string& filename)
{
    try
    {
        if (boost::filesystem::exists(filename) && boost::filesystem::is_regular_file(filename))
        {
            _activeFileSize = boost::filesystem::file_size(filename);
        }
    }
    catch (const boost::filesystem::filesystem_error& e)
    {
        std::cerr << e.what() << std::endl;
    }
}

std::string RotatedLog::ConstructLogFileName() const
{
    switch (_rotationType)
    {
        case RotatedLog::ROTATION_METHOD::ROTATE_AT:
        {
            // The TM struct storing the day.
            tm timeNow = {0};

            const time_t tNow = system_clock::to_time_t(system_clock::now());
            const time_t tRotated = system_clock::to_time_t(_lastRotatedAt);

            LOCALTIME_FUNC(&tNow, &timeNow);

            timeNow.tm_hour = _rotationHMS[0];
            timeNow.tm_min  = _rotationHMS[1];
            timeNow.tm_sec  = _rotationHMS[2];

            time_t switchSeconds = std::mktime(&timeNow);

            // If we've already switched today, use today's date.  Otherwise, we need to subtract a
            // day because this log is still valid for yesterday - the switch today hasn't occurred!

            if (tNow < switchSeconds) { switchSeconds -= 60 * 60 * 24; }

            tm forFileName = {0};
            LOCALTIME_FUNC(&switchSeconds, &forFileName);
            return _filename + "." + TMToStringYMD(forFileName);
        }

        case RotatedLog::ROTATION_METHOD::NO_ROTATION:
        case RotatedLog::ROTATION_METHOD::ROTATE_AFTER:
        case RotatedLog::ROTATION_METHOD::ROTATE_WHEN_SIZE:
        default:
        {
            return _filename;
        }
    }
}

void RotatedLog::HandleRotateAt(const volatile bool& quitEarly)
{
    while (!quitEarly && !_localQuitLogging)
    {
        const time_t tNow = system_clock::to_time_t(system_clock::now());
        const time_t tRotated = system_clock::to_time_t(_lastRotatedAt);

        tm switchAt = {0}; 
        LOCALTIME_FUNC(&tNow, &switchAt);

        switchAt.tm_hour = _rotationHMS[0];
        switchAt.tm_min  = _rotationHMS[1];
        switchAt.tm_sec  = _rotationHMS[2];

        const time_t switchSeconds = std::mktime(&switchAt);

        if (tNow <= switchSeconds)
        {
            InterruptedSleepUntil(system_clock::from_time_t(switchSeconds), quitEarly);
        }

        if (quitEarly || _localQuitLogging) { return; }

        std::lock_guard<std::mutex> lock(_fileLock);
        OpenLog(ConstructLogFileName());
    }
}

void RotatedLog::HandleRotateAfter(const volatile bool& quitEarly)
{
    while (!quitEarly && !_localQuitLogging)
    {
        const auto lastRotated = _lastRotatedAt;
        const auto rotateWhen = _lastRotatedAt + seconds(_rotateIntervalSeconds);

        InterruptedSleepUntil(rotateWhen, quitEarly);
        if (quitEarly || _localQuitLogging) { return; }

        // Only rotate the log if the last opened time hasn't changed.
        // If it has, it indicates that we've probably needed to reopen a file because it got closed or something.

        if (lastRotated == _lastRotatedAt) { OpenLog(ConstructLogFileName()); }
    }
}


// ---------------------------------------------------------------------------
// Assumes access to a mutex has already been secured.
// ---------------------------------------------------------------------------
void RotatedLog::CheckSizeAndShift()
{
    // Don't bother logging if we're supposed to exit gracefully.
    if (_localQuitLogging) { return; }

    bool openNewLog = false;

    if (_rotationType == ROTATION_METHOD::ROTATE_WHEN_SIZE)
    {
        if (_activeFileSize >= _maxFilesizeBytes) { openNewLog = true; }
    }

    else if (_rotationType == ROTATION_METHOD::ROTATE_AFTER)
    {
        if (_lastRotatedAt + seconds(_rotateIntervalSeconds) < system_clock::now()) { openNewLog = true; }
    }

    if (openNewLog)
    {
        std::cerr << "Rotating to new log." << std::endl;
        _logfile.close();
        
        RenameExistingLogs();

        // Start up the new log.
        OpenLog(ConstructLogFileName());
    }
}

void RotatedLog::HandleQueue(const std::vector<LogData>& toLog)
{
    std::string buffer;
	buffer.reserve(BUFFER_SIZE);

    constexpr uint64_t elemSize = sizeof(decltype(buffer)::value_type); // Futureproofing in case unicode or something?

	std::lock(_fileLock, _configLock, _filterLock);
    std::lock_guard<std::mutex> lock_io(_fileLock, std::adopt_lock);
    std::lock_guard<std::mutex> lock_config(_configLock, std::adopt_lock);
	std::lock_guard<std::mutex> lock_filters(_filterLock, std::adopt_lock);

    // Make sure the log file hasn't been deleted or something.
    if (_localQuitLogging) { return; }

    if (!_logfile.is_open())
    {
        OpenLog(ConstructLogFileName());
    }

    // If we can't open a file, there's no sense in trying to log to it.
    // NOTE: This means that entries will be skipped from logging if we
    // can't log to a file, rather than persisting until that point!

    if (_logfile.is_open() && !_diskIsFull)
    {
        std::string buffer;
        
        // Log all the lines that are good to log.
        for (const auto& elem : toLog)
        {
            if (MeetsLoggingCriteria(elem) && !_localQuitLogging)
            {
				_config.AppendLogToString(elem, buffer);
				buffer += '\n';

                if (buffer.size() >= BUFFER_SIZE)
                {
                    _logfile << buffer;
                    _logfile.flush();
                    _activeFileSize += buffer.size() * elemSize;
                    buffer.clear();
                }
                CheckSizeAndShift();
            }
        }

        // If we haven't finished logging all the lines because the
        // buffer isn't full, then we'll just log the content here so
		// that we don't need to wait for additional content to keep being logged.

        if (!_localQuitLogging && !buffer.empty() && !_diskIsFull)
        {
			_logfile << buffer;
			_logfile.flush();
			_activeFileSize += buffer.size() * elemSize;
			buffer.clear();
			CheckSizeAndShift();
        }
    }
}

void RotatedLog::ResetLogsAtTime(const unsigned hour, const unsigned minute, const unsigned second)
{
    std::lock_guard<std::mutex> lock(_fileLock);

    _rotationHMS = {hour, minute, second};

    // _monitor is set to nullptr first to clean up any remaining threads hanging around.
    // If we reset and change data before making sure this is not in use, it might misbehave.
    _monitorRotation = nullptr;

    _rotationType = ROTATION_METHOD::ROTATE_AT;
    OpenLog(ConstructLogFileName());
    _monitorRotation = std::make_unique<ThreadRAII>(&RotatedLog::HandleRotateAt, this);
}

void RotatedLog::ResetLogsAfterElapsed(const uint64_t numSeconds, const int numToRotateThrough)
{    
    std::lock_guard<std::mutex> lock(_fileLock);
    
    _monitorRotation = nullptr;

    _numToRotateThrough = numToRotateThrough;
    _rotateIntervalSeconds = numSeconds;
    _rotationType = ROTATION_METHOD::ROTATE_AFTER;

    OpenLog(ConstructLogFileName());

    _monitorRotation = std::make_unique<ThreadRAII>(&RotatedLog::HandleRotateAfter, this);
}

void RotatedLog::ResetLogsAtSize(const uint64_t bytes, const int numToRotateThrough)
{
    std::lock_guard<std::mutex> lock(_fileLock);

    // There's no monitoring thread as the logging function itself will keep track of
    // file size and switch to a new file as needed.

    _monitorRotation = nullptr;

    _rotationType = ROTATION_METHOD::ROTATE_WHEN_SIZE;
    _maxFilesizeBytes = bytes;
    _numToRotateThrough = numToRotateThrough;

    OpenLog(ConstructLogFileName());
}

void RotatedLog::AppendOnly()
{
    std::lock_guard<std::mutex> lock(_fileLock);

    _monitorRotation = nullptr;
    _rotationType = ROTATION_METHOD::NO_ROTATION;

    OpenLog(ConstructLogFileName());
}

void RotatedLog::SetDiskThresholdPercent(const double d)
{
	_diskThreshold = d;
}

void RotatedLog::CheckDiskSpace(const volatile bool& b)
{
	while (!b)
	{
		auto path = boost::filesystem::absolute(ConstructLogFileName()).parent_path();
		if (boost::filesystem::exists(path))
		{
			auto diskInfo = boost::filesystem::space(path);
			const double usedPercent = static_cast<double>(diskInfo.capacity - diskInfo.available) / static_cast<double>(diskInfo.capacity);

			_diskIsFull = (usedPercent >= _diskThreshold);
		}

		InterruptedSleepFor(seconds(5), b);
	}
}