#include <vector>
#include <future>

#include "LogAsync.h"

int main(int argc, char* argv[])
{
    auto logfile = Logging::RegisterLog("LogAsync_Basics.txt");

    // 1) We have basic logging of strings and tags.  Tags can, but do not need to include logging levels.
    
    LOG_ASYNC("Testing") << "I have " << 4 << " cars and " << 1.0/3.0 << " gallons of gas remaining!" << std::endl;
    LOG_ASYNC_C({"Testing"}, "I have %d cars and %.4f gallons of gas remaining!", 15, 1.0 / 3.0);

    // 2) We can conditionally log statements based on expressions.

    volatile bool truthBeTold = true;

    LOG_ASYNC_IF(truthBeTold == true, "Testing") << "The truth was told." << std::endl;      // This should show up
    LOG_ASYNC_IF_C(truthBeTold == true, {"Testing"}, "The truth was told");                  // This should show up too
    
    LOG_ASYNC_IF(truthBeTold == false, "Testing") << "The truth was not told." << std::endl; // This should not
    LOG_ASYNC_IF_C(truthBeTold == false, {"Testing"}, "The truth was not told.");            // This shouldn't either.

    // 3) We can log every N instances of something we want to track. ----------------------------------

    for (unsigned i = 0; i < 20; ++i)
    {
        LOG_ASYNC_EVERY(5, "Testing") << "Logging with i=" << i << std::endl;
        LOG_ASYNC_EVERY_C(5, {"Testing"}, "Printf-style Logging with i=%d", i);
    }


    // 4) We can log every N instances of an identified term.  If you have multiple threads running the
    //    same code, then you can pass a thread id or unique integer to the ID field of LOG_ASYNC_EVERY_ID
    //    so that it keeps track of every n logs per thread you're running.
    
    std::vector<std::future<void>> massiveAsync;

    for (unsigned i = 0; i < 100; ++i)
    {
        massiveAsync.emplace_back(
            std::async(
                std::launch::async,
                [](const int id)
                  {
                    for (volatile unsigned j = 0; j < 100; ++j)
                    {
                        // Even in tests like this, some of the numbers might not show up entirely accurately,
                        // though most of them should - depending on how a compiler decides to optimize the loops
                        // below, and how mutex access takes hold, there may be one or two numbers off...
                        LOG_ASYNC_EVERY_ID(id, 10, "Testing") << "Logging from ID " << id << " with j=" << j << std::endl;
                        LOG_ASYNC_EVERY_ID_C(id, 10, {"Testing"}, "Logging C from ID %d with j=%d", id, j);
                    }
                },
                i));
    }

    for (auto& elem : massiveAsync) { elem.get(); }
    
    Logging::ShutdownLogging();
    
    return 0;
}