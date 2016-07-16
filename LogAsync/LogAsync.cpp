#include <thread>
#include <atomic>
#include <queue>
#include <cstdint>
#include <unordered_map>
#include <future>
#include <iterator>
#include <condition_variable>

#include <boost/lexical_cast.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/filesystem.hpp>

#include "QueueWrapper.h"

#include "LogAsync.h"
#include "LogHandler.h"
#include "SocketSender.h"

#ifdef _MSC_VER
#if _MSC_VER < 1900
#define thread_local __declspec(thread)
#endif
#endif

constexpr uint_fast32_t DEQUE_SIZE = 256;
constexpr uint_fast32_t NUM_LOGGING_WORKERS = 2;


static const std::array<const char*, 6> LOG_LEVELS =
{
	LOG_FATAL,
	LOG_ERROR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG,
	LOG_ALL
};

enum LOG_LEVELS_INTEGRAL
{
	LOG_FATAL_INT = 0,
	LOG_ERROR_INT = 1,
	LOG_WARNING_INT = 2,
	LOG_INFO_INT = 3,
	LOG_DEBUG_INT = 4,
	LOG_ALL_INT = 5,
};



inline int64_t LogLevelPosition(const char* src)
{
	const auto position = std::find(LOG_LEVELS.begin(), LOG_LEVELS.end(), src);
	if (position == LOG_LEVELS.end()) { return LOG_ALL_INT; }

	return std::distance(LOG_LEVELS.begin(), position);
}

template <unsigned level>
inline bool LogLevelAt(std::unordered_set<const char*>&& s)
{
	for (int i = level; i >= 0; --i)
	{
		if (s.find(LOG_LEVELS[i]) != s.end()) { return true; }
	}
	return false;
}

inline bool LogEverything(std::unordered_set<const char*>&& s) { return true; }

// --------------------------------------------------------------------------------------------
// Variables local/private to the Logging Namespace that are essential to run the various
// logging functions.
// --------------------------------------------------------------------------------------------
namespace Logging
{
	// Matches to logging lines / id's for various logging methods ---------------
	typedef std::shared_ptr<std::atomic<uint_fast32_t>> shared_uint32_ptr;
	typedef std::shared_ptr<std::atomic<int_fast32_t>> shared_int32_ptr;

	// "LogEvery" requires we keep track of the lines being logged on each thread.
	// We don't want threads to conflict so we need some global access for lookups.
	//
	// Strangely enough, the logging every n per thread is more simple because
	// it can be resolved with thread_locals without locks.
	std::unordered_map<const char*, shared_uint32_ptr> loggingLineLookup;
	boost::shared_mutex rwlock_loggingLine;

	// Keep track of our logging queues and threads.  We want to take advantage of 
	// multi-producer efficiencies in a queue but allow it to be processed and 
	// sorted by a single consumer separately.  This facilitates the need to keep 
	// a "swappable" queue at the ready, and work on a standby queue.
	ConcurrentQueueWrapper asyncQueue;
	std::unique_ptr<ThreadRAII> handle_queue;
	std::unique_ptr<ThreadRAII> handle_disk;

	// Allows us to stream on all threads from a different stream. ---------------
	thread_local LoggingStream managed_stream;

	std::function<bool(std::unordered_set<const char*>&&)> loggingLevelFilter = LogEverything;

	// Keep track of all our logging systems.
	std::vector<std::weak_ptr<LogBase>> allActiveLogs;
	boost::shared_mutex logAdditionMutex;

	// Disk space checking -------------------------------------------------------
	volatile bool quitLogging = false;
	volatile bool spaceExceeded = false;
	double diskSpaceRatio = 1.0;

	// Has our internal state been started? -------------------------------------
	std::atomic<bool> initialized(false);

	// Graceful program termination without needing to manually unregister something is good!
	class LogRAII;
	std::unique_ptr<LogRAII> terminate_logging;

}

// --------------------------------------------------------------------------------------------
// Let's see if we can't use RAII to handle graceful program termination as well.
// --------------------------------------------------------------------------------------------
namespace Logging
{
	class LogRAII final
	{
	public:
		LogRAII() {}
		~LogRAII() { quitLogging = true; }
	};
}

// --------------------------------------------------------------------------------------------
// Common functions required of the logging system that may be used in later parts of the code
// but are not declared above.
// --------------------------------------------------------------------------------------------
namespace Logging
{

	// ----------------------------------------------------------------------
	// When is it acceptable to log data? Don't bother stressing the logging
	// system if we don't have anything that we'll even need to log data to.
	// ----------------------------------------------------------------------
	bool IsLoggable(std::unordered_set<const char*>&& tags)
	{
		return !quitLogging && !spaceExceeded && !allActiveLogs.empty() && loggingLevelFilter(std::move(tags));
	}

	// ----------------------------------------------------------------------
	// If we haven't seen a Logging Line, add it to the map.
	// ----------------------------------------------------------------------
	inline uint_fast32_t RegisterCountOf(const char* src)
	{
		boost::upgrade_lock<boost::shared_mutex> lock(rwlock_loggingLine);
		boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

		// Ensure we're actually the first entry and not just another thread
		// that's waited for the mutex to clear at this point.
		auto found = loggingLineLookup.find(src);
		if (found == loggingLineLookup.end())
		{
			// Since we always fetch_add on a read, prime the value with 1 rather than 0
			loggingLineLookup[src] = std::make_shared<shared_uint32_ptr::element_type>(1);
			return 0;
		}
		return found->second->fetch_add(1);
	}

	// ----------------------------------------------------------------------
	// Find the number of times a Logging Line has already been called,
	// inserting a new entry into the map if it's the first time it's been used.
	// ----------------------------------------------------------------------
	inline uint_fast32_t GetCountOf(const char* src)
	{
		boost::shared_lock<boost::shared_mutex> lock(rwlock_loggingLine);
		auto find_base = loggingLineLookup.find(src);
		if (find_base == loggingLineLookup.end())
		{
			lock.unlock();
			return RegisterCountOf(src);
		}
		return find_base->second->fetch_add(1);
	}

	// ----------------------------------------------------------------------
	// Find the number of times an ID/Logging Line pair has already been
	// called, inserting a new entry into the map if it's the first time
	// it's been seen.  Obviously this becomes a problem if we have a bajilion
	// threads running with these logging calls.
	// ----------------------------------------------------------------------
	inline uint_fast32_t GetCountOfID(uint_fast32_t id, const char* src)
	{
		static thread_local std::unordered_map<const char*, uint_fast32_t> loggingLineIDLookup;

		auto find_base = loggingLineIDLookup.find(src);
		if (find_base == loggingLineIDLookup.end())
		{
			loggingLineIDLookup[src] = 1;
			return 0;
		}
		return find_base->second++;
	}

	// ----------------------------------------------------------------------
	// Return a new RotatedLog based on an input filename that can be modified.
	// As soon as it goes out of scope on the parent, it'll stop being logged
	// by the logging system.
	// ----------------------------------------------------------------------
	inline std::shared_ptr<RotatedLog> CreateRotatedLog(const std::string& filename)
	{
		auto rotated = std::make_shared<RotatedLog>(filename);
		return rotated;
	}

	template <class T>
	inline std::shared_ptr<T> AddLogToSystem(std::shared_ptr<T> l)
	{
		InitLogging();
		boost::shared_lock<boost::shared_mutex> lock(logAdditionMutex);
		allActiveLogs.push_back(l);
		return l;
	}

	std::shared_ptr<RotatedLog> RegisterLog(const std::string& filename)
	{
		auto rotated = CreateRotatedLog(filename);
		rotated->SetDiskThresholdPercent(diskSpaceRatio);
		return AddLogToSystem(rotated);
	}
	std::shared_ptr<RotatedLog> RegisterSizeRotatedLog(const std::string& filename, const uint64_t maxBytes, const unsigned numToRotateThrough)
	{
		auto rotated = CreateRotatedLog(filename);
		rotated->ResetLogsAtSize(maxBytes, numToRotateThrough);
		return AddLogToSystem(rotated);
	}
	std::shared_ptr<RotatedLog> RegisterPeriodRotatedLog(const std::string& filename, const uint64_t secondsPerLog, const unsigned numToRotateThrough)
	{
		auto rotated = CreateRotatedLog(filename);
		rotated->ResetLogsAfterElapsed(secondsPerLog, numToRotateThrough);
		return AddLogToSystem(rotated);
	}
	std::shared_ptr<RotatedLog> RegisterDailyLog(const std::string& filename, const unsigned hour, const unsigned minute, const unsigned second)
	{
		auto rotated = CreateRotatedLog(filename);
		rotated->ResetLogsAtTime(hour, minute, second);
		return AddLogToSystem(rotated);
	}

	std::shared_ptr<NetworkIO::LogSocket> RegisterUDPv4_Destination(const std::string& ip, const std::string& port)
	{
		auto socket = NetworkIO::RegisterUDPv4_Destination(ip, port);
		return AddLogToSystem(socket);
	}
	std::shared_ptr<NetworkIO::LogSocket> RegisterUDPv6_Destination(const std::string& ip, const std::string& port)
	{
		auto socket = NetworkIO::RegisterUDPv6_Destination(ip, port);
		return AddLogToSystem(socket);
	}
}

// --------------------------------------------------------------------------------------------
// Logging Stream Implementation details.
// --------------------------------------------------------------------------------------------
namespace Logging
{
	LoggingStream::LoggingStream() : _w(), _source(""), _tagFilter() {}
	LoggingStream::~LoggingStream() {}

	// ---------------------------------------------------------------------------
	// Reset state and tag filters, as well as the source of the current line. 
	// ---------------------------------------------------------------------------
	void LoggingStream::SetSource(const char* src)
	{
		_tagFilter.clear();
		_source = src;
	}

	// ---------------------------------------------------------------------------
	// Tags needs to reset logging level so we don't erroneously look at tags
	// for an older line if we call a new logging line without a prior std::endl.
	// ---------------------------------------------------------------------------
	void LoggingStream::SetTags(std::unordered_set<std::string>&& tags)
	{
		_tagFilter = std::move(tags);
	}

	// ---------------------------------------------------------------------------
	// Terminate logging the line and wrap up what the thread local stream has
	// collected. Additionally, it will clear and reset the stream so that it can 
	// be used by other functions.
	// ---------------------------------------------------------------------------
	LoggingStream& LoggingStream::operator<<(StandardEndLine c)
	{
		asyncQueue.AddToQueue(LogData(std::move(_source), std::move(_tagFilter), std::move(_w.str())));
		_w.clear();
		return *this;
	}
}


// --------------------------------------------------------------------------------------------
// The meat of the logging system - queue handling and access functions.
// --------------------------------------------------------------------------------------------
namespace Logging
{

	// ---------------------------------------------------------------------------
	// Remove extraneous log units if we noticed that some exist.
	// ---------------------------------------------------------------------------
	inline void HandleExpiredLogs(const unsigned numExpired)
	{
		// Only handle expired logs if we have a bunch of them to prevent any
		// system running continuously that creates / stops using these from
		// having some type of memory leak.
		if (numExpired <= 4) { return; }

		boost::upgrade_lock<boost::shared_mutex> lock(logAdditionMutex);
		boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

		allActiveLogs.erase(std::remove_if(allActiveLogs.begin(),
										   allActiveLogs.end(),
										   [](const std::weak_ptr<LogBase>& m) { return m.expired(); }),
							allActiveLogs.end());
	}

	// ---------------------------------------------------------------------------
	// NO_OP_MODE - testbed for working with purely unsorted queue sizes.
	// ---------------------------------------------------------------------------
	void HandleNoOpQueue(const volatile bool& quit)
	{
		std::vector<LogData> dataVec;
		uint64_t numParsed = 0;
		const auto start = steady_clock::now();

		while (!quit)
		{
			asyncQueue.Dequeue(dataVec);
			if (!dataVec.empty()) { numParsed += dataVec.size(); }
			else { std::this_thread::sleep_for(milliseconds(1)); }
		}
		const auto end = steady_clock::now();
		const auto elapsed = duration<double, std::milli>(end - start).count();

		std::cout << "Processed " << numParsed << " messages in " << elapsed << "ms" << std::endl;
		std::cout << "Average time to log each message: " << elapsed / numParsed << "ms" << std::endl;
	}
	// ---------------------------------------------------------------------------
	// NO_OP_ORDERED - testbed for raw input/output used in sorted handling for
	//                 the actual logging system.
	// ---------------------------------------------------------------------------
	void HandleNoOpQueueSorted(const volatile bool& quit)
	{
		std::vector<LogData> dataVec;
		uint64_t numParsed = 0;
		const auto start = steady_clock::now();

		while (!quit)
		{
			asyncQueue.Dequeue(dataVec);
			if (!dataVec.empty())
			{
				numParsed += dataVec.size();
			}
			else { std::this_thread::sleep_for(milliseconds(1)); }
		}

		const auto end = steady_clock::now();
		const auto elapsed = duration<double, std::milli>(end - start).count();

		std::cout << "Processed " << numParsed << " messages in " << elapsed << "ms" << std::endl;
		std::cout << "Average time to log each message: " << elapsed / numParsed << "ms" << std::endl;
	}

	// ---------------------------------------------------------------------------
	// Offload from the queue without caring about sorting.
	// ---------------------------------------------------------------------------
	void HandleUnsortedQueue(const volatile bool& quit)
	{
		std::vector<LogData> dataVec;

		while (!quit)
		{
			std::vector<std::future<void>> futures;
			asyncQueue.Dequeue(dataVec);
			if (!dataVec.empty())
			{
				unsigned expiredLogs = 0;

				// Lock Guard scoping
				{
					boost::shared_lock<boost::shared_mutex> lock(logAdditionMutex);

					for (auto& weakRef : allActiveLogs)
					{
						if (auto strongRef = weakRef.lock())
						{
							futures.emplace_back(std::async([strongRef, &dataVec] { strongRef->HandleQueue(dataVec); }));
						}
						else { ++expiredLogs; }
					}
				}

				for (auto& future : futures) { future.get(); }

				HandleExpiredLogs(expiredLogs);
			}
			else { std::this_thread::sleep_for(milliseconds(1)); }
		}
	}


	// ---------------------------------------------------------------------------
	// Offload from the queue into a sorted way.
	// ---------------------------------------------------------------------------
	void HandleSortedQueue(const volatile bool& quit)
	{
		// Because the ConcurrentQueue buckets data by producer in order rather than
		// purely in order, we need to extract and sort the input data in order for
		// a sorted method - thus we need to exhaust the entire input queue.
		std::vector<LogData> dataVec;
	
		while (!quit)
		{
			asyncQueue.Dequeue(dataVec);
			if (!dataVec.empty())
			{
				std::vector<std::future<void>> futures;
				unsigned expiredLogs = 0;

				// Lock Guard scoping
				{
					boost::shared_lock<boost::shared_mutex> lock(logAdditionMutex);

					for (auto& weakRef : allActiveLogs)
					{
						if (auto strongRef = weakRef.lock())
						{
							futures.emplace_back(std::async([strongRef, &dataVec] { strongRef->HandleQueue(dataVec); }));
						}
						else { ++expiredLogs; }
					}
				}

				for (auto& future : futures) { future.get(); }
				HandleExpiredLogs(expiredLogs);
			}
			else { std::this_thread::sleep_for(milliseconds(1)); }
		}
	}

	// ---------------------------------------------------------------------------
	// Initialize logging and get the processing threads ready.
	//
	// This will only occur once, and is called by the functions that distribute
	// logging files / sockets out, so that there's no overhead if logging
	// is not being used.
	// ---------------------------------------------------------------------------
	void InitLogging(const InitializationMode m)
	{
		if (!initialized.exchange(true))
		{
			// Manage the lifespan of the logging to the program
			terminate_logging = std::make_unique<LogRAII>();

			// Start up the worker thread
			if (m == InitializationMode::ALLOW_UNORDERED)
			{
				handle_queue = std::make_unique<ThreadRAII>(HandleUnsortedQueue);
				asyncQueue.HandleDataUnordered();
			}
			else if (m == InitializationMode::PERFECTLY_ORDERED)
			{
				handle_queue = std::make_unique<ThreadRAII>(HandleSortedQueue);
				asyncQueue.HandleDataOrdered();
			}
			else if (m == InitializationMode::NO_OP_MODE)
			{
				handle_queue = std::make_unique<ThreadRAII>(HandleNoOpQueue);
				asyncQueue.HandleDataUnordered();
			}
			else if (m == InitializationMode::NO_OP_ORDERED)
			{
				handle_queue = std::make_unique<ThreadRAII>(HandleNoOpQueueSorted);
				asyncQueue.HandleDataOrdered();
			}
		}
	}

	// --------------------------------------------------------------------------------------------
	// It is not necessary to call this function, but doing so ensures that any outstanding messages
	// that have yet to be logged will be logged before the system is shut down.
	// --------------------------------------------------------------------------------------------
	void ShutdownLogging()
	{
		if (initialized)
		{
			// Don't allow new logging.
			terminate_logging = nullptr;

			// Allow the queue to finish up and flush entirely.
			while (asyncQueue.GetRequestsRemaining() > 0) { std::this_thread::sleep_for(milliseconds(256)); }
		}
	}


	// ---------------------------------------------------------------------------
	// Filter all logs that aren't of a specified level.
	// ---------------------------------------------------------------------------
	void SetLoggingLevel(const char* level)
	{
		const auto position = LogLevelPosition(level);
		switch (position)
		{
			case LOG_FATAL_INT:
			{
				loggingLevelFilter = &LogLevelAt<LOG_FATAL_INT>;
				break;
			}
			case LOG_ERROR_INT:
			{
				loggingLevelFilter = &LogLevelAt<LOG_ERROR_INT>;
				break;
			}
			case LOG_WARNING_INT:
			{
				loggingLevelFilter = &LogLevelAt<LOG_WARNING_INT>;
				break;
			}
			case LOG_INFO_INT:
			{
				loggingLevelFilter = &LogLevelAt<LOG_INFO_INT>;
				break;
			}
			case LOG_DEBUG_INT:
			{
				loggingLevelFilter = &LogLevelAt<LOG_DEBUG_INT>;
				break;
			}
			case LOG_ALL_INT:
			default:
			{
				loggingLevelFilter = LogEverything;
				break;
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Returns the number of times a line of code has been logged by the system,
	// so that we can log every n lines.
	// ---------------------------------------------------------------------------
	uint_fast32_t NumInstancesEvery(const char* src)
	{
		return GetCountOf(src);
	}

	// ---------------------------------------------------------------------------
	// Returns the number of times a line of code has been logged by the system,
	// so that we can log every n lines per id calling the function.
	// ---------------------------------------------------------------------------
	uint_fast32_t NumInstancesEveryID(uint_fast32_t id, const char* src)
	{
		return GetCountOfID(id, src);
	}

	// ---------------------------------------------------------------------------
	// Called by all log stream code.
	// ---------------------------------------------------------------------------
	LoggingStream& GetLogStream(const char* src, std::unordered_set<std::string>&& tags)
	{
		managed_stream.SetSource(src);
		managed_stream.SetTags(std::move(tags));
		return managed_stream;
	}

	// ---------------------------------------------------------------------------
	// Called by all printf logging code
	// ---------------------------------------------------------------------------
	void LogPrintfStyle(const char* src, std::unordered_set<std::string>&& tags, std::string&& logWhat)
	{
		asyncQueue.AddToQueue(LogData(src, std::move(tags), std::move(logWhat)));
	}

	void SetDiskSpaceThreshold(const double percent)
	{
		double sanitizedPercentage = std::min<double>(std::max<double>(percent / 100.0, 0.0), 1.0);

		// Update only if the value has changed.
		if (abs(sanitizedPercentage - diskSpaceRatio) < std::numeric_limits<double>::epsilon())
		{
			// Update disk space tracking for existing logs.
			boost::shared_lock<boost::shared_mutex> lock(logAdditionMutex);
			for (auto& input : allActiveLogs)
			{
				if (auto weakRef = input.lock())
				{
					if (auto toRotatedLog = std::dynamic_pointer_cast<RotatedLog>(weakRef))
					{
						toRotatedLog->SetDiskThresholdPercent(diskSpaceRatio);
					}
				}
			}
		}
	}
}
