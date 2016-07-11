#pragma once

// ------------------------------------------------------------------------------------
//  LogAsync - An asynchronous, ordered, logging wrapper intended to make it easier
//             to log data in critical sections of code without blocking as long for a
//             logging call.
//
//             At the moment, it's intended to be a wrapper that is threadsafe, 
//             order-preserving, reasonably fast, and relatively lightweight to use
//
//             It's primitive and doesn't do anything fancy, and it's easy to plug
//             in GLOG, Log4cxx, or another logging system to work on the backend
//             of this.  It defaults to logging via     .
// ------------------------------------------------------------------------------------

#include <iostream>
#include <string>
#include <sstream>
#include <functional>
#include <vector>
#include <tuple>
#include <memory>
#include <cstdint>
#include <unordered_set>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "LogHandler.h"
#include "SocketSender.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ "::" TOSTRING(__LINE__)

// --------------------------------------------------------------------------------------------
// Logging stream macros - these are used to stream (and log) data, and must be terminated with
// a std::endl for the logging to occur correctly.
//
// Logging is based on tags, not on levels.  This is an intentional choice - tags are more
// descriptive than logging levels and allow for much more complex organization of file data.
//
// !!! WARNING !!!  ONLY USE ONE LINE PER LOG.  DON'T USE MULTIPLE LOGGING TAGS IN ANY LINE OF
//                  CODE, OR THE SYSTEM WILL NOT BE ABLE TO HANDLE YOUR INPUT PROPERLY.
//
// !!! WARNING !!!  The system expects that all tags are constant and do not change dynamically
//                  at runtime.  Writing code that does so will likely interfere with the correct
//                  logging operation of the system.
//
// !!! WARNING !!!  For speed purposes, tags logged to LOG_ASYNC_EVERY_ID use thread local
//                  storage.  If you don't 
// --------------------------------------------------------------------------------------------

static const char* const LOG_FATAL   = "LOG_FATAL"; // Does not call std::terminate; just treated as a level.
static const char* const LOG_ERROR   = "LOG_ERROR";
static const char* const LOG_WARNING = "LOG_WARN";
static const char* const LOG_INFO    = "LOG_INFO";
static const char* const LOG_DEBUG   = "LOG_DEBUG";
static const char* const LOG_ALL     = "LOG_ALL"; // Allows everything to be logged, even if no logging level tags are provided.

#define LOG_ASYNC(...) if (::Logging::IsLoggable({__VA_ARGS__})) ::Logging::GetLogStream(AT, {__VA_ARGS__})
#define LOG_ASYNC_IF(expr, ...) if (expr) LOG_ASYNC(__VA_ARGS__)
#define LOG_ASYNC_EVERY(n, ...) if (::Logging::IsLoggableEvery<n>(AT)) LOG_ASYNC(__VA_ARGS__)
#define LOG_ASYNC_EVERY_ID(id, n, ...) if (::Logging::IsLoggibleEveryID<n>(id,AT)) LOG_ASYNC(__VA_ARGS__)

// We're compatible with printf style stuff too, but it's won't be quite as clean to set up.
// TAGS should be formatted as follows:
// { "Tag1", "Tag2", .... , "Pizza" }
//
// One thing to note is that GCC handles variadic macros differently than Visual Studio,
// we we need to have a switch between them for them to exhibit the same behavior.


#ifdef _MSC_VER

    #define LOG_ASYNC_C(tags, fmt, ...) if (::Logging::IsLoggable(tags)) ::Logging::HandlePrintfStyle(AT, tags, fmt, __VA_ARGS__)
    #define LOG_ASYNC_IF_C(expr, tags, fmt, ...) if (expr) LOG_ASYNC_C(tags, fmt, __VA_ARGS__)
    #define LOG_ASYNC_EVERY_C(n, tags, fmt, ...) if (::Logging::IsLoggableEvery<n>(AT)) LOG_ASYNC_C(tags, fmt, __VA_ARGS__)
    #define LOG_ASYNC_EVERY_ID_C(id, n, tags, fmt, ...) if (::Logging::IsLoggibleEveryID<n>(id,AT)) LOG_ASYNC_C(tags, fmt, __VA_ARGS__)

#else //This supports GCC, I don't know what format Clang would require for this.

    #define LOG_ASYNC_C(tags, fmt, ...) if (::Logging::IsLoggable(tags)) ::Logging::HandlePrintfStyle(AT, tags, fmt, ##__VA_ARGS__)
    #define LOG_ASYNC_IF_C(expr, tags, fmt, ...) if (expr) LOG_ASYNC_C(tags, fmt, ##__VA_ARGS__)
    #define LOG_ASYNC_EVERY_C(n, tags, fmt, ...) if (::Logging::IsLoggableEvery<n>(AT)) LOG_ASYNC_C(tags, fmt, ##__VA_ARGS__)
    #define LOG_ASYNC_EVERY_ID_C(id, n, tags, fmt, ...) if (::Logging::IsLoggibleEveryID<n>(id,AT)) LOG_ASYNC_C(tags, fmt, ##__VA_ARGS__)

#endif


namespace Logging
{
    // --------------------------------------------------------------------------------------------
    // Add a log that will process through the input data we have.
    // --------------------------------------------------------------------------------------------
    std::shared_ptr<RotatedLog> RegisterLog(const std::string& filename);
    std::shared_ptr<RotatedLog> RegisterSizeRotatedLog(const std::string& filename, const uint64_t maxBytes, const unsigned numToRotateThrough);
    std::shared_ptr<RotatedLog> RegisterPeriodRotatedLog(const std::string& filename, const uint64_t secondsPerLog, const unsigned numToRotateThrough);
    std::shared_ptr<RotatedLog> RegisterDailyLog(const std::string& filename, const unsigned hour, const unsigned minute, const unsigned second);
    
    std::shared_ptr<NetworkIO::LogSocket> RegisterUDPv4_Destination(const std::string& ip, const std::string& port);
    std::shared_ptr<NetworkIO::LogSocket> RegisterUDPv6_Destination(const std::string& ip, const std::string& port);

    // --------------------------------------------------------------------------------------------
    // Do we even need to log anything?
    // If we don't have anything that needs to be logged, there's no sense in even trying to
    // queue/dequeue logs that won't even be logged...
    // --------------------------------------------------------------------------------------------
    bool IsLoggable(std::unordered_set<const char*>&& tags);

    // --------------------------------------------------------------------------------------------
    // Sets up the logging system.  It isn't necessary to call this function, because it's called
    // by RegisterSocketLog and RegisterRotatedLog functions.
    // --------------------------------------------------------------------------------------------
	enum class InitializationMode
	{
		PERFECTLY_ORDERED, // Force all queue entries to be time synchronized - this is slower.
		ALLOW_UNORDERED,   // Ignore the need to perfectly order entries in a queue.

		// TESTING MODES, IGNORE THESE UNLESS YOU NEED TO TEST FORMATTING SPEED OR QUEUE SPEED
		NO_OP_MODE,        // [Unordered Queue Removal]
		NO_OP_ORDERED,     // [Queue is sorted by timestamp]
	};

    void InitLogging(const InitializationMode m = InitializationMode::PERFECTLY_ORDERED);

    // --------------------------------------------------------------------------------------------
    // It is not necessary to call this function, but doing so ensures that any outstanding messages
    // that have yet to be logged will be logged before the system is shut down.
    // --------------------------------------------------------------------------------------------
    void ShutdownLogging();

    // --------------------------------------------------------------------------------------------
    // Logs a certain line only x number of times.  Each time this block of code is hit, the
    // repetition count increases by 1.  This rolls around over a uint_fast32_t, which has a 
    // side effect of causing a log to be logged even if it hasn't reached n iterations.
    //
    // Assumptions:  Each line of code contains AT MOST ONE reference to this function (i.e.
    //               keep each log on a separate line or else we can't track the entries properly).
    // --------------------------------------------------------------------------------------------
    uint_fast32_t NumInstancesEvery(const char* src);
    uint_fast32_t NumInstancesEveryID(uint_fast32_t id, const char* src);

    template<unsigned LOG_FREQUENCY> inline bool IsLoggableEvery(const char* src)
    {
        return NumInstancesEvery(src) % LOG_FREQUENCY == 0;
    }

    template<unsigned LOG_FREQUENCY> inline bool IsLoggibleEveryID(const uint_fast32_t id, const char* src)
    {
        return NumInstancesEveryID(id, src) % LOG_FREQUENCY == 0;
    }

    // --------------------------------------------------------------------------------------------
    // LoggingStream is the streamable interface that the LOG_ASYNC macros return.
    //
    // std::endl is a termination flag that indicates that the streamed content should be
    // sent to the logging system; any lines not containing this flag will not behave as intended.
    // --------------------------------------------------------------------------------------------
    class LoggingStream
    {
    private:
		fmt::MemoryWriter _w;
        std::string _source;
        std::unordered_set<std::string> _tagFilter;
        typedef std::basic_ostream<char, std::char_traits<char> > CoutType;
        typedef CoutType& (*StandardEndLine)(CoutType&);

    public:
        LoggingStream();
        ~LoggingStream();

        // --------------------------------------------------------------------------------------------
        // Configure parts of the stream that the logging system will need to log later on.
		// These don't need to be called by users of the logging system.
        // --------------------------------------------------------------------------------------------
        void SetSource(const char* s);
        void SetTags(std::unordered_set<std::string>&& tags);

		// --------------------------------------------------------------------------------------------
		// Handle the input data we're actually logging.
		// Since std::endl acts as a logging terminator, it needs to be handled individually.
		// --------------------------------------------------------------------------------------------

        LoggingStream& operator<<(StandardEndLine c);
        template <class T> LoggingStream& operator<<(const T& o) { _w << o; return *this; }

    };

    // --------------------------------------------------------------------------------------------
    // Helper method called by LOG_ASYNC macros to obtain a thread-local LoggingStream.
    // --------------------------------------------------------------------------------------------
    LoggingStream& GetLogStream(const char* src, std::unordered_set<std::string>&& tags);

	// --------------------------------------------------------------------------------------------
	// Methods called by the prinf style logging stuff.
	// --------------------------------------------------------------------------------------------
	void LogPrintfStyle(const char* src, std::unordered_set<std::string>&& tags, std::string&& logWhat);

	template <class ...Args>
	inline void HandlePrintfStyle(const char* src, std::unordered_set<std::string>&& tags, const char* format, Args&& ...args)
	{
		std::string tmp = fmt::sprintf(format, std::forward<Args>(args)...);
		LogPrintfStyle(src, std::move(tags), std::move(tmp));
	}
	inline void HandlePrintfStyleEmpty(const char* src, std::unordered_set<std::string>&& tags, const char* format)
	{
		LogPrintfStyle(src, std::move(tags), format);
	}

    void SetLoggingLevel(const char* level);

	// --------------------------------------------------------------------------------------------
	// Ignore logging if the disk space is above a certain percentage.
	// - 0.0 means no logging will occur (will stop logging at 0% full)
	// - 100.0 means log until disk space is full.
	// - New logs are ENTIRELY IGNORED if the disk space criteria is met.
	// --------------------------------------------------------------------------------------------
	
	void SetDiskSpaceThreshold(const double percentage);
}