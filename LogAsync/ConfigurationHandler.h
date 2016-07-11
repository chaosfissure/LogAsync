#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_set>

#include "TimeManip.h"

static const std::string DEFAULT_LOGGING_FORMAT = "%t | %S | %T | %m";

struct LogData
{
	uint64_t _insertionPoint; // Assumption is that we won't ever log 2^64 logs, and if we do, only a small number
	                          // being logged will be logged out of order.  This is more accurate (guaranteed
	                          // in-order if the position is atomic) than using time as a sorting metric.

	system_clock::time_point _timeLogged;  // Wall clock timestamp (NONSTATIC)
	std::string _codeSrc;                  // Line of code with file name (STATIC)
	std::unordered_set<std::string> _tags; // List of tags associated with the line (STATIC)
	std::string _logContent;               // The logged string. (NONSTATIC)

    LogData();
    LogData(std::string&& src, std::unordered_set<std::string>&& tags, std::string&& content);

    // --------------------------------------------------------------------------------------------
    // Sort operates on operator<, but sort with the largest element first.  However,
    // as we're interested in sorting by both smallest time AND insertion point, we'll want to 
    // invert this - return true only if the other element is larger than this one!
    // --------------------------------------------------------------------------------------------
    bool operator<(const LogData& o) const;

};

// --------------------------------------------------------------------------------------------
// LoggingFormat contains the configuration used to parse the timestamp of the logged message
// and the format of the logging line.
// --------------------------------------------------------------------------------------------
class LoggingFormat
{
private:
    // We'll just collect a sequential list of functions that will be appended to a string in-order
    // to construct the final logging string.

	
    std::vector<std::function<std::string(const LogData&)>> _parsingSchema;
    std::string _dateformat;

    // --------------------------------------------------------------------------------------------
    // See TimeManip.h [ConstructTimestamp] for more information in the formatting of this string.
    // --------------------------------------------------------------------------------------------
    void SetDateFormat(const std::string& s);

    
public:
    LoggingFormat();

    // --------------------------------------------------------------------------------------------
    // _logformat is a string dictating log format.  It uses the following terms:
    //
    // - %t:  timestamp of the log message.
    //
    // - %s:  source information (file/line) of the logged line.
    // - %S:  source information (file/line) of the logged line, stripped of any path elements.
    // 
    // - %T:  Tags associated with the log data.  WARNING(!!) This assumes that tags given on any
    //        logging line are not modified dynamically - this is for efficiency and speed in being
    //        able to look up tags!
    //
    // - %m:  message content.
    //
    // - %%:  a percent sign.
    //
    //  The input will be modified, which is why it's not passed by const ref.
    // --------------------------------------------------------------------------------------------
    void SetLogFormat(std::string logformat = DEFAULT_LOGGING_FORMAT, 
                      std::string dateformat = DEFAULT_TIME);

    // --------------------------------------------------------------------------------------------
    // Based on the configuration settings of the class, process the logging struct and convert
    // it into a string that can be logged, sent over a socket, or whatever it's configured to do.
    // --------------------------------------------------------------------------------------------
	std::string GetLogStringFrom(const LogData& l) const;
	void AppendLogToString(const LogData& l, std::string& out) const;

};
