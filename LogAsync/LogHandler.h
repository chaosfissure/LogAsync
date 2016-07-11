#pragma once

#include <iostream>
#include <fstream>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "ThreadUtilities.h"
#include "ConfigurationHandler.h"

constexpr size_t MIN_LOG_ENTRIES_BEFORE_FLUSH = 256;

constexpr uint64_t TO_KILOBYTES(const uint64_t kb_val) { return 1024 * kb_val; }
constexpr uint64_t TO_MEGABYTES(const uint64_t mb_val) { return 1024 * TO_KILOBYTES(mb_val); }
constexpr uint64_t TO_GIGABYTES(const uint64_t gb_val) { return 1024 * TO_MEGABYTES(gb_val); }
constexpr double FROM_KILOBYTES(const uint64_t byte_val) { return byte_val / 1024.0; }
constexpr double FROM_MEGABYTES(const uint64_t byte_val) { return FROM_KILOBYTES(byte_val) / 1024.0; }
constexpr double FROM_GIGABYTES(const uint64_t byte_val) { return FROM_MEGABYTES(byte_val) / 1024.0; }

// ------------------------------------------------------------------------------------
//  RotatedLog is a wrapper around logging being done to an arbitrary file.
//
//  Many logging systems log to a single file, and this can be useful, but in very
//  massive systems, I've found it hard to juggle multiple log files or isolate logs
//  in ways that don't cause a single file to become swamped. GLOG logs to a single file,
//  while Log4cxx has a concept of "Loggers" which can be arbitrarily assigned at a config
//  parse, but require lots of setup to get working as desired.
//
//
//  SocketLogs hope to offer the flexibility to log to a socket with very trivial overhead
//  required to use them.  They're not as tested or refined as the RotatedLogs, but should
//  be able to forward data over TCP (with reconnect) or UDP, and cap their message sizes
//  to the maximum size of a given TCP message.
// ------------------------------------------------------------------------------------

class LogBase
{
public:
    typedef std::function<bool(const LogData& l)> FilterType;

protected:

    // Make sure that filters used by this log aren't modified while we're looking at them.
    std::mutex _filterLock;
    std::mutex _configLock;
	bool _useCache;
    volatile bool _localQuitLogging;

    // Configuration settings for formatting of data.
    LoggingFormat _config;

    // A collection of functions that determine what logs actually should be logged to this particular file.
    // Rather than filtering on tags alone, it also has the flexibility to use all parts of a LogData struct to
    // accept or reject various logs. If an acceptable filter is found (i.e. one of the filters evaluates to true), 
	// the line will be logged as specified in the _config parameter.  
    std::vector<FilterType> _inputFilters;

	// Because doing comparisons against logging line tags takes time, we can keep track of cource code locations
	// and cache if they evaluate true or false against our filters.  This speeds up the average use case of the
	// logging system.
	std::unordered_map<std::string, bool> _sourceEvalCache;

    // ------------------------------------------------------------------------------------
    // Do our filters allow us to log the data?
    // If we don't have any filters, we assume all data is loggable.
    // ------------------------------------------------------------------------------------
    bool MeetsLoggingCriteria(const LogData& l);

public:
    LogBase();
    virtual ~LogBase();

	// ------------------------------------------------------------------------------------
	// Disables caching the evaluation of a log line.
	//
	// This should be set if you have input filters that are not guaranteed to evaluate
	// data deterministically, for instance if you filter input based on the timestamp
	// or look for a certain number in the logged data, then you can't use the cache to 
	// know if the line is consistently loggable.
	// ------------------------------------------------------------------------------------
	void DisableCache();

	// ------------------------------------------------------------------------------------
	// Enables caching of data again.  By default, this IS enabled, so calling it is not
	// necessary unless you've explicitly disabled the cache above.
	// ------------------------------------------------------------------------------------
	void EnableCache();

    // ------------------------------------------------------------------------------------
    // Adds an input filter criteria to the log data.
	//
	// !!! WARNING !!!
	//
	// Terms labeled "NONSTATIC" (LogData._timeLogged and LogData._logContent) are not
	// something known at compile time.
	//
	// These terms SHOULD NOT be referenced in logging filters unless you disable the
	// cache, because the cache makes an optimization assumption that any given source
	// code location will evaluate the same way as long as the filters don't change!
	// -----------------------------------------------------------------------------------
    void AddInputFilter(FilterType&& func);
    
	// ------------------------------------------------------------------------------------
	// Adds an input filter criteria to the log data, and purges the existing filter data.
	//
	// !!! WARNING !!!
	//
	// Terms labeled "NONSTATIC" (LogData._timeLogged and LogData._logContent) are not
	// something known at compile time.
	//
	// These terms SHOULD NOT be referenced in logging filters unless you disable the cache, 
	// because the cache makes an optimization assumption that any given source code location
	// will evaluate the same way each time!
	// ---------------------------------------------------------------------------------------
    void AddExclusiveInputFilter(FilterType&& func);

    // ------------------------------------------------------------------------------------
    // Clears all existing filters
    // ------------------------------------------------------------------------------------
    void ClearAllFilters();

    // ------------------------------------------------------------------------------------
    // Load a set of configuration settings.  This doesn't actually load a file/fstream;
    // it just sets the configuration settings.
    // ------------------------------------------------------------------------------------
    void SetConfiguration(const std::string& timeformat=DEFAULT_LOGGING_FORMAT,
						  const std::string& dateformat=DEFAULT_TIME);

    // ------------------------------------------------------------------------------------
    // Handle the queue of messages that's been sorted and offloaded by the logging system.
    // ------------------------------------------------------------------------------------
    virtual void HandleQueue(const std::vector<LogData>& l) = 0;
};

// ------------------------------------------------------------------------------------
//  RotatedLogs hope to be more flexible, by introducing "Logs" as an actual class that can
//  be manipulated. Logs specify what (of all total logged linesinput) they're able to log,
//  and can be sent to files which can individually be set to rotate after a set time, 
//  at a certain time, based on their size, or even just appended to a file continuously.
// ------------------------------------------------------------------------------------

class RotatedLog : public LogBase
{
private:
    enum class ROTATION_METHOD { NO_ROTATION, ROTATE_WHEN_SIZE, ROTATE_AT, ROTATE_AFTER };

    std::mutex _fileLock;

    // The active file that's open.
    std::ofstream _logfile;

    // Tracks file size when we open a file or log data.
    uint64_t _activeFileSize;

    // The base file name we log to.  Any rotation methods will append something to the end of the file name
    std::string _filename;

	// Is the disk space exceeding the space it needs
	volatile bool _diskIsFull;
	double _diskThreshold;

    // Keeps track of the way the class cycles through files and creates new logs.
    ROTATION_METHOD _rotationType;

    // Keeps track of the maximum allowed number of bytes of log input that can be written to a file before it's
    // rotated.  It might not accurately reflect the final file size, because a really large logging string could
    // cause the size of the log to balloon far past this limit.
    uint64_t _maxFilesizeBytes;

    // Number of seconds before a log is rotated to a new file, if the option is set for this.
    uint64_t _rotateIntervalSeconds;

    // The number of logs that we rotate through in total, and renumber / delete.
    int _numToRotateThrough;

    // Timestamp {H,M,S} that we will rotate to a new log (daily rotation).
    std::array<unsigned, 3> _rotationHMS;

    // The last time we actually rotated a log.
    system_clock::time_point _lastRotatedAt;

    // A thread that periodically monitors the status of the logging and handles periodic rotation if need be.
    std::unique_ptr<ThreadRAII> _monitorRotation;
	std::unique_ptr<ThreadRAII> _monitorDiskSpace;

    // ------------------------------------------------------------------------------------
    // Check if the file has exceeded its max size and shift/cascade rename if necessary.
    // Doesn't call a mutex, but assumes calling function has a lock on _m to prevent race
    // conditions.
    // ------------------------------------------------------------------------------------
    void CheckSizeAndShift();

	// ------------------------------------------------------------------------------------
	// Periodically check and make sure we have enough disk space to do logging.
	// ------------------------------------------------------------------------------------
	void CheckDiskSpace(const volatile bool& quitEarly);

    // ------------------------------------------------------------------------------------
    // Extract file size information from the file.
    // ------------------------------------------------------------------------------------
    void StatFile(const std::string& filename);

    // ------------------------------------------------------------------------------------
    // The filename we need to open may depend on time, and if so, we need a generic way
    // to have the logging open itself up if a file is unexpectedly closed or deleted.
    // ------------------------------------------------------------------------------------
    std::string ConstructLogFileName() const;

    // ------------------------------------------------------------------------------------
    // Close the current log (if there's one open), and open the log requested. This also
    // will load the file size information from StatFile.
    // ------------------------------------------------------------------------------------
    void OpenLog(const std::string& name);

    // ------------------------------------------------------------------------------------
    // Functions checking if rotation should occur based on the type of rotation performed.
    // This function is intended to be done by a separate thread - it will idle and
    // periodicaly check to see if we need to switch to a new file, and do so if necessary.
    //
    // The reason this occurs periodically rather than during logging is because being
    // dependent on a piece of data being logged to check conditions means that we could
    // be checking a bajillion times a second (overkill), or not have any checking for a
    // very long time if no messages are sent (missing time-based updates).
    // ------------------------------------------------------------------------------------
    void HandleRotateAt(const volatile bool& quitEarly);
    void HandleRotateAfter(const volatile bool& quitEarly);

    // ------------------------------------------------------------------------------------
    // Keep the name of the active stream used to log data the same, but shift all
    // log files with names or numbers on them back by 1.  This only occurs when we're
    // rotating logs with a given size parameter; if we're logging by date, then the
    // filename will include the time we started logging, rendering this unnecessary.
    // ------------------------------------------------------------------------------------
    void RenameExistingLogs() const;

public:

    // Default constructor with append mode specified.
    RotatedLog(const std::string& baseName);

    virtual ~RotatedLog();

    void ResetLogsAtTime(const unsigned hour, const unsigned minute, const unsigned second);
    void ResetLogsAfterElapsed(const uint64_t numSeconds, const int NumToRotateThrough);
    void ResetLogsAtSize(const uint64_t bytes, const int numToRotateThrough);
    void AppendOnly();

	void SetDiskThresholdPercent(const double d);

    void HandleQueue(const std::vector<LogData>& l);
};