#include "LogAsync.h"

int main(int argc, char* argv[])
{
    auto logfile = Logging::RegisterLog("LogAsync_Tags.txt");

    // We can log using multiple tags.  The tags can be whatever you want, but they
    // should ideally be descriptive of what you're trying to log!

    LOG_ASYNC(LOG_INFO, "HelloWorld", "food", "foobar", "etcetc...") << "Hello, world!" << std::endl;

    // But it really doesn't make sense to log without tags.  You can try your luck at it if you
    // desire, but it's not recommended to use the system in this way.
    
     LOG_ASYNC() << "Please use descriptive tags in your logs so you can track them later!" << std::endl;
     
     Logging::WaitForQueueEmpty();
     
     // ---------------------------------------------------------------------------------------------
     // So, what can we do with tags?
     // ---------------------------------------------------------------------------------------------
   
    // By default, everything passed to a logfile is logged.
    LOG_ASYNC("RandomTag") << "Hello" << std::endl;

    // But as soon as we insert a set of criteria the logs need to match, it becomes
    // exclusive.  It will only log any logs matching the input filters.

    logfile->AddInputFilter([](const LogData& l) { return l._tags.find("elevators") != l._tags.end(); });
    LOG_ASYNC("Testing") << "This isn't going to be logged." << std::endl;

    logfile->AddInputFilter([](const LogData& l) { return l._tags.find("Testing") != l._tags.end(); });
    LOG_ASYNC("Testing") << "Now it'll be logged though." << std::endl;

    // We don't only need to use tag filters.  We can register all logs from an entire file!
    // Or for that matter, we can use any field in LogData to determine if that particular log
    // should be logged to this file.
    
    logfile->AddInputFilter([](const LogData& l) { return l._codeSrc.find("tag_details") != std::string::npos; });
    LOG_ASYNC("LargeTrout") << "Something about a large trout." << std::endl;

    // We can also clear all filters we have on the logs as well so that EVERYTHING is logged to the file.
    logfile->ClearAllFilters();
    
    LOG_ASYNC("RandomTag") << "See, I still log." << std::endl;

    logfile->AddInputFilter([](const LogData& l) { return l._tags.find("elevators") != l._tags.end(); });
    LOG_ASYNC("RandomTag") << "But now I won't be logged :(" << std::endl;
    
    Logging::ShutdownLogging();
   
    return 0;
}