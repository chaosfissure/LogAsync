#pragma once

#include <chrono>
#include <ctime>
#include <string>
#include <boost/lexical_cast.hpp>

#include <fmt/format.h>
#include <fmt/time.h>

#ifdef _MSC_VER
#define LOCALTIME_FUNC(time_t_val, tm_result) localtime_s(tm_result, time_t_val)
#else
#define LOCALTIME_FUNC(time_t_val, tm_result) localtime_r(time_t_val, tm_result)
#endif

// ------------------------------------------------------------------------------------
// Utility typedefs for convenience.
// ------------------------------------------------------------------------------------
using std::chrono::system_clock;
using std::chrono::steady_clock;

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::microseconds;
using std::chrono::nanoseconds;

using std::chrono::duration;
using std::chrono::duration_cast;

constexpr unsigned STRFTIME_BUF_SIZE = 100;               // Controls the size of the std::strftime buffer.  Can be expanded as necessary.

constexpr unsigned NANOSECONDS_NUM_DECIMAL_PLACES = 9;    // Internally we're using nanoseconds as the highest granularity.

constexpr unsigned DEFAULT_RESOLUTION_DECIMAL_PLACES = 6; // If we can't parse a fractional time term, use this many decimal places.

static const std::string FRACTIONAL_TIME_TERM = "$";      // The control term used to determine how many fractional seconds to parse.
                                                          // It should be used as $1, $2, ..., $9 to represent granularity, but it will only
                                                          // support down to nanosecond resolution (9 digits of precision).

static const std::string DEFAULT_TIME = "%Y/%m/%d %H:%M:%S.$6";
static const std::string ISO_6801_TIME = "%Y-%m-%dT%H-%M-%S.$6%zZ"; // YYYY-MM-DDThh:mm:ss.msTZD


// ------------------------------------------------------------------------------------
// Convenience functions
// ------------------------------------------------------------------------------------

inline std::string TMToStringYMD(const tm& t)
{
    std::string tmp = "";
    tmp.reserve(16);
	tmp += fmt::FormatInt(t.tm_year + 1900).c_str();
	tmp += '.';
	tmp += fmt::FormatInt(t.tm_mon + 1).c_str();
	tmp += '.';
    tmp += fmt::FormatInt(t.tm_mday).c_str();
    return tmp;
}

// --------------------------------------------------------------------------------------------
// Given an input timing string, determine the precision at which fractional seconds will be
// logged.  If there's multiple "$" delimiters, the final delimiter will be respected.
//
// If a "$<1-9>" cannot be parsed successfully, it will default to DEFAULT_RESOLUTION_DECIMAL_PLACES
// decimals of precision.
//
// Returns a pair:
// - First: Format string representing <precision> decimal places
// - Second: 
// --------------------------------------------------------------------------------------------
inline std::pair<std::string, std::string> FractionalSecondPrecision(const std::string& in)
{
	// Extract the precision from the input string.  If there's multiple precision strings
	// specified, the last one parsed is the one we'll ultimately use when constructing
	// timestamps.

	// Additionally, if we see a strin
    unsigned precision = DEFAULT_RESOLUTION_DECIMAL_PLACES;
    std::string processed = "";
    for (unsigned i = 0; i < in.size(); ++i)
    {
		
        if (in[i] == '$')
        {
            processed += in[i];
            if (++i < in.size())
            {
                const std::string nextChar(1, in[i]);
                unsigned parsedVal = std::atoi(nextChar.c_str());

                if (parsedVal != 0) { precision = parsedVal; }
                else                { processed += nextChar; }
            }
        }
        else { processed += in[i]; }
    }

	precision = std::max<unsigned>(1, std::min<unsigned>(9, precision));
	std::string precisionFormat = "{:0." + fmt::FormatInt(precision).str() + "f}";
    return std::make_pair(precisionFormat, processed);
}

// --------------------------------------------------------------------------------------------
// ConstructTimestamp parses as per std::strftime - http://en.cppreference.com/w/cpp/chrono/c/strftime,
// with one exception:
//
// There's a delimiter "$" which is used to represent the position of a fractional timestamp.
// This function takes the number of decimals as an input parameter, so any calling functions
// must obtain a suitable number of decimal places, which can be extracted as any number immediately
// following the delimiter.
// --------------------------------------------------------------------------------------------
inline std::string ConstructTimestamp(const std::string& format, const system_clock::time_point when, const std::string& floatformat)
{
	
    const time_t msgTimeTVal = system_clock::to_time_t(when);
    tm msgTimeResult = {0};
    LOCALTIME_FUNC(&msgTimeTVal, &msgTimeResult);
      
	char strftime_buf[STRFTIME_BUF_SIZE];
	std::strftime(strftime_buf, STRFTIME_BUF_SIZE, format.c_str(), &msgTimeResult);
	std::string currentTimestamp(strftime_buf);
    
    // Inject the fractional seconds each time we have a FRACTIONAL_TIME_TERM in the string.
    // We might have it multiple times (though it's practically unlikely), so we do need to
    // handle this occurring.
    size_t ms_begin = currentTimestamp.find(FRACTIONAL_TIME_TERM);
    while (ms_begin != std::string::npos)
    {
		const double elapsedTime = duration<double, std::ratio<1, 1>>(when - system_clock::from_time_t(msgTimeTVal)).count();
		std::string strElapsedNanoseconds = fmt::format(floatformat, elapsedTime).substr(2);

        // Inject the fractional timestamp into the message.
        currentTimestamp =
            currentTimestamp.substr(0, ms_begin) +                              // Portion before the fractional seconds.
            strElapsedNanoseconds +                                             // Injection of the fractional part.
            currentTimestamp.substr(ms_begin + FRACTIONAL_TIME_TERM.size());    // Portion after the fractional seconds.

        // Advance forward in case this term exists again.
        ms_begin = currentTimestamp.find(FRACTIONAL_TIME_TERM, ms_begin+1);
    }

    return currentTimestamp;
}
