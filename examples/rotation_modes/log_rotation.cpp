#include <atomic>
#include <chrono>

#include "LogAsync.h"

int main(int argc, char* argv[])
{
    // You can register as many log files as you want, and each of them will be passed the same input data.

    // Register a log that constantly is appended to.
    auto logfile1 = Logging::RegisterLog("LogAsync_RotateAppend.txt"); 

    // Register a log that rotates after it's 1mb in size, and rotates through 5 logfiles in total.
    auto logfile2 = Logging::RegisterSizeRotatedLog("LogAsync_RotateSized.txt", TO_MEGABYTES(1), 5); 

    // Register a log that rotates every 2 seconds, and rotates through 5 logfiles in total.
    auto logfile3 = Logging::RegisterPeriodRotatedLog("LogAsync_RotateDuration.txt", 2, 5); 

    // Register a log that switches daily, and have the daily switch occur four seconds after the current point in time
    // so that we can see the switch actually occur.
    const time_t tNow = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + seconds(4));
    tm switchAt = {0};
    LOCALTIME_FUNC(&tNow, &switchAt);
    auto logfile4 = Logging::RegisterDailyLog("LogAsync_RotateAtTime.txt", switchAt.tm_hour, switchAt.tm_min, switchAt.tm_sec); // Register a log that rotates


    // Let's do some logging now and see what happens.
    std::vector<std::thread> threads;
    std::atomic<uint_fast32_t> counter(0);
    volatile bool quitRequest = false;

    for (unsigned i = 0; i < std::max<unsigned>(1u, std::thread::hardware_concurrency()-1); ++i)
    {
        threads.emplace_back(
            [&quitRequest, &counter](const int id)
            {
                while (!quitRequest)
                {
                    // Note that this logging doesn't guarantee perfect ordering of the counter --
                    // there may be times where a thread may pull an atomic value before another thread,
					// but another thread may actually initiate the log request first!

                    LOG_ASYNC("Things") << "Thread " << id << " logging " << counter++ << std::endl;
                    std::this_thread::sleep_for(milliseconds(1));
                }
                LOG_ASYNC("ENDING") << "Thread " << id << " has finished." << std::endl;
            },
            i);
    }
    
    std::this_thread::sleep_for(seconds(7));
    quitRequest = true;
    for (auto& elem : threads) { if (elem.joinable()) { elem.join(); } }
   
    Logging::ShutdownLogging();
   
    return 0;
}