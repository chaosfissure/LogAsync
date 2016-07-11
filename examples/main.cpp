#include "LogAsync.h"

int main(int argc, char* argv[])
{
    auto logfile = Logging::RegisterLog("LogAsync_HelloWorld.txt");
	
	// When the logfile goes out of scope and there's no references to
	// its shared pointer, it'll stop being logged to.  You'll need to
	// capture the logfile in a larger scope if you want to keep it around!
    
    LOG_ASYNC("Testing") << "Hello, world!" << std::endl;

    // --------------------------------------------------------------------------------------------
	// -> 2016/06/12 19:39:40.170500 | main.cpp::11 | Testing | Hello, world!
    // --------------------------------------------------------------------------------------------
        
	// By default, everything will be logged, even logs that do not contain logging levels.
	// As soon as you set a logging level, all input without a logging level will not show 
	// up in logs unless you set the logging level to LOG_ALL.

	for (auto term : {LOG_FATAL, LOG_ERROR, LOG_WARNING, LOG_INFO, LOG_DEBUG, LOG_ALL})
	{
		Logging::SetLoggingLevel(term);

		LOG_ASYNC(LOG_FATAL) << "Testing with Log Level " << term << "---------------------------------" << std::endl;
		LOG_ASYNC(LOG_FATAL) << "FATAL" << std::endl;
		LOG_ASYNC(LOG_ERROR) << "ERROR" << std::endl;
		LOG_ASYNC(LOG_WARNING) << "WARNING" << std::endl;
		LOG_ASYNC(LOG_INFO) << "INFO" << std::endl;
		LOG_ASYNC(LOG_DEBUG) << "DEBUG" << std::endl;
		LOG_ASYNC(LOG_ALL) << "ALL" << std::endl;
		LOG_ASYNC("No Log Level") << "No logging level provided." << std::endl;

		// Under normal circumstances, you don't need to sleep, but we can't guarantee that the queues will all be cleared prior to logging 
		std::this_thread::sleep_for(seconds(1));
	}

    // It's recommended to call ShutdownLogging.  This both stops and synchronizes the logging,
    // so that you can see everything you've passed into the system to that point in time.
    // Not calling this may result in the last few logs in the logging system not being
    // logged correctly.

	std::this_thread::sleep_for(seconds(1));

	Logging::ShutdownLogging();
	return 0;
}