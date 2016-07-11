#include <iostream>
#include <unordered_map>
#include <mutex>

#include <boost/lexical_cast.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <fmt/format.h>

#include "ConfigurationHandler.h"
#include "ThreadUtilities.h"

constexpr size_t STRING_RESERVE_SIZE = 4096;


// Let's go with the assumption that we can cache a logging line to a string of tags so that
// we don't constantly need to do set finds on strings.  The assumption here is that the tags
// associated with any given logging line don't change dynamically at runtime.
std::unordered_map<std::string, std::string> lineToStringRep;
boost::shared_mutex lineToStringMutex;

inline std::string AddEntry(const std::string& logSource, const std::unordered_set<std::string>& tags)
{
    std::string formattedTags = "";

    // Conglomerate all the tags.
    for (auto it = tags.begin(); it != tags.end(); ++it)
    {
        formattedTags += *it + ", ";
    }

    // Remove the trailing ", " if it exists.
    if (!formattedTags.empty())
    {
        formattedTags = formattedTags.substr(0, formattedTags.size() - 2);
    }

    // Obtain writer access 
    boost::upgrade_lock<boost::shared_mutex> lock(lineToStringMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);
    lineToStringRep.emplace(logSource, formattedTags);
	return formattedTags;
}

// --------------------------------------------------------------------------------------------
// If we haven't cached a string of all the tags for a line, then use the input tags to do so.
// Otherwise, just get a reader lock on the thing.
// --------------------------------------------------------------------------------------------
inline std::string GetTagListForLine(const std::string& logSource, const std::unordered_set<std::string>& tags)
{
	boost::shared_lock<boost::shared_mutex> lock(lineToStringMutex);
	const auto tagLine = lineToStringRep.find(logSource);
    if (tagLine == lineToStringRep.end())
    {
		lock.unlock();
        return AddEntry(logSource, tags);
    }
	return tagLine->second;
}

LogData::LogData() :
	_insertionPoint(0),
	_timeLogged(system_clock::now()),
	_codeSrc("???? : ??"),
	_tags(),
	_logContent("Invalid log content")
{}



LogData::LogData(std::string&& src, std::unordered_set<std::string>&& tags, std::string&& content) :
	_insertionPoint(0),
	_timeLogged(system_clock::now()),
    _codeSrc(std::move(src)),
    _tags(std::move(tags)),
	_logContent(std::move(content))
{}

bool LogData::operator<(const LogData& o) const
{
    return _insertionPoint < o._insertionPoint;
}

LoggingFormat::LoggingFormat() :
    _parsingSchema(),
    _dateformat(ISO_6801_TIME)
{
    SetLogFormat(DEFAULT_LOGGING_FORMAT);
}

// --------------------------------------------------------------------------------------------
// See TimeManip.h [ConstructTimestamp] for more information in the formatting of this string.
// --------------------------------------------------------------------------------------------
void LoggingFormat::SetDateFormat(const std::string& s)
{
    _dateformat = s;
}

// --------------------------------------------------------------------------------------------
// _logformat is a string dictating log format.  It uses the following terms:
//
// - %t:  timestamp of the log message.
//
// - %s:  source information (file/line) of the logged line.
// - %S:  source information (file/line) of the logged line, stripped of any path elements.
// 
// - %l:  Level of logging (warning, error, etc) or tags associated with the log data.
//
// - %m:  message content.
//
// - %%:  a percent sign.
// --------------------------------------------------------------------------------------------
void LoggingFormat::SetLogFormat(std::string logformat, std::string dateformat)
{
    _parsingSchema.clear();

    _dateformat = dateformat;

    // Figure out where all of the tokens are in the string.  Preprocess the steps needed to construct
	// the log message in such a way that we can sequentially handle it at runtime.
	
    while (!logformat.empty())
    {
        size_t percentPos = logformat.find("%");
        if (percentPos == std::string::npos)
        {
            _parsingSchema.emplace_back([=](const LogData& l) -> std::string { return logformat; });
            logformat.clear();
        }
        else
        {
            // Don't bother appending an empty string function if we're transitioning straight into another token.
            if (percentPos != 0) 
            {
                _parsingSchema.emplace_back([=](const LogData& l) -> std::string { return logformat.substr(0, percentPos); });
            }

            logformat = logformat.substr(percentPos + 1);

            // Roll through the list of functions that we're parsing for.

            if (logformat[0] == 't') // Timestamp
            {
                // How many decimal places do we need to parse through?
                auto precision = FractionalSecondPrecision(_dateformat);

                _parsingSchema.emplace_back([=](const LogData& l) 
                {
                    return ConstructTimestamp(precision.second, l._timeLogged, precision.first);
                });

            }
            else if (logformat[0] == 's') // Source (full path + line number)
            {
                _parsingSchema.emplace_back([](const LogData& l) { return l._codeSrc; });
            }
            else if (logformat[0] == 'S') // Source (filename only + line number)
            {
				
                _parsingSchema.emplace_back([](const LogData& l)  
                { 
                    auto src = l._codeSrc;

					// Remove any filepath elements that might be present.
					if (src.find('\\') != std::string::npos) { src = src.substr(src.rfind('\\') + 1); }
					else if (src.find('/') != std::string::npos) { src = src.substr(src.rfind('/') + 1); }
                    return src;
                });
            }
            else if (logformat[0] == 'T') // Tags
            { 
                _parsingSchema.emplace_back([](const LogData& l)
                {
                    return GetTagListForLine(l._codeSrc, l._tags);
                });
            }
            else if (logformat[0] == 'm') // Message to be logged
            {
                _parsingSchema.emplace_back([](const LogData& l) { return l._logContent; });
            }
            else if (logformat[0] == '%') // A literal percent sign
            {
                _parsingSchema.emplace_back([](const LogData& l) { return "%"; });
            }

            // And we need to remove the character we just parsed!
            logformat = logformat.substr(1);
        }
    }
}

// --------------------------------------------------------------------------------------------
// Based on the configuration settings of the class, process the logging struct and convert
// it into a string that can be logged, sent over a socket, or whatever it's configured to do.
// --------------------------------------------------------------------------------------------
std::string LoggingFormat::GetLogStringFrom(const LogData& l) const
{
	std::string tmp = "";
	for (const auto& formatFunc : _parsingSchema) { tmp += formatFunc(l); }
	return tmp;
}

// 
// This version assumes that an input string is present to prevent extraneous memory allocation, 
// and allows the capacity of a string to be sized based on the input and reused rather than
// having a bunch of small allocs that have to be concatenated at the end.
// --------------------------------------------------------------------------------------------
void LoggingFormat::AppendLogToString(const LogData& l, std::string& out) const
{
	for (const auto& formatFunc : _parsingSchema) { out += formatFunc(l); }
}