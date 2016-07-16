#include "LogAsync.h"


// IMPORTANT NOTE:
// Since the logging system is asynchronous, updates to the logging filters may not correspond perfectly to
// how logs are internally collected and processed.  To ensure consistent behavior in this sample, we sleep
// between any updates in the filters so that everything will sync up and logs will indeed be logged as expected.
//
// This sleeping is NOT necessary in code that you write, unless you have explicit need for this type of logging.

int main(int argc, char* argv[])
{
	auto logfile = Logging::RegisterLog("LogAsync_Tags.txt");

	// We can log using multiple tags.  The tags can be whatever you want, but they should ideally be descriptive of what you're trying to log!
	LOG_ASYNC(LOG_INFO, "HelloWorld", "food", "foobar", "etcetc...") << "Hello, world! (will be logged)" << std::endl;

	// But it really doesn't make sense to log without tags.  You can try your luck at it if you desire, but it's not recommended to use the system in this way.
	LOG_ASYNC() << "Please use descriptive tags in your logs so you can track them later! (will be logged)" << std::endl;

	// ---------------------------------------------------------------------------------------------
	// So, what can we do with tags?
	// ---------------------------------------------------------------------------------------------

   // By default, everything passed to a logfile is logged.
	LOG_ASYNC("RandomTag") << "Hello, this is logged." << std::endl;
	std::this_thread::sleep_for(milliseconds(128));

	// But as soon as we insert a set of criteria the logs need to match, it becomes exclusive.  It will only log any logs matching the input filters.

	logfile->AddInputFilter([](const LogData& l) { return l._tags.find("elevators") != l._tags.end(); });

	std::this_thread::sleep_for(milliseconds(128));
	LOG_ASYNC("Testing") << "This isn't going to be logged." << std::endl;
	std::this_thread::sleep_for(milliseconds(128));

	logfile->AddInputFilter([](const LogData& l) { return l._tags.find("Testing") != l._tags.end(); });

	std::this_thread::sleep_for(milliseconds(128));
	LOG_ASYNC("Testing") << "Now it'll be logged." << std::endl;
	std::this_thread::sleep_for(milliseconds(128));

	// We don't only need to use tag filters.  We can register all logs from an entire file!  We can use any field
	// in LogData to determine if that particular log should be logged to this file.

	logfile->AddInputFilter([](const LogData& l) { return l._codeSrc.find("tag_details") != std::string::npos; });

	std::this_thread::sleep_for(milliseconds(128));
	LOG_ASYNC("LargeTrout") << "Something about a large trout will be logged" << std::endl;
	std::this_thread::sleep_for(milliseconds(128));

	// We can also clear all filters we have on the logs as well so that EVERYTHING is logged to the file.
	logfile->ClearAllFilters();

	std::this_thread::sleep_for(milliseconds(128));
	LOG_ASYNC("RandomTag") << "See, I still log." << std::endl;
	std::this_thread::sleep_for(milliseconds(128));

	// However, using some fields as filters, such as the time a message was logged the message content itself, 
	// requires disabling the logging cache.  Otherwise, the system won't re-evaluate each log against the 
	// filters.

	// Let's demonstrate this behavior, logging data that occurs on even second values:
	
	logfile->AddInputFilter([](const LogData& l) { return system_clock::to_time_t(l._timeLogged) % 2 == 0; });
	std::this_thread::sleep_for(milliseconds(128));

	auto start = system_clock::now();
	while (system_clock::now() < start + seconds(4))
	{
		LOG_ASYNC("CacheTest") << (system_clock::to_time_t(system_clock::now()) % 2 == 0 ? "Should be logged??" : "Should not be logged??") << std::endl;
		std::this_thread::sleep_for(milliseconds(256));
	}

	// DisableCache -- this is the important line to remember if you're wanting to filter log strings or timestamps.
	// It's inherited from LogBase, which all socket logs and file logs inherit from.

	logfile->DisableCache(); 
	start = system_clock::now();
	while (system_clock::now() < start + seconds(4))
	{
		LOG_ASYNC("CacheTest") << (system_clock::to_time_t(system_clock::now()) % 2 == 0 ? "Will be logged." : "Will not be logged.") << std::endl;
		std::this_thread::sleep_for(milliseconds(256));
	}

	Logging::ShutdownLogging();

	return 0;
}