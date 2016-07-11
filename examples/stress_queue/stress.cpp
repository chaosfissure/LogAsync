#include <future>
#include <atomic>

#include "LogAsync.h"

int main(int argc, char* argv[])
{
	//Logging::InitLogging(Logging::InitializationMode::NO_OP_MODE);
	Logging::InitLogging(Logging::InitializationMode::NO_OP_ORDERED);

    // We need to register a logging unit, otherwise the system says "Oh! None are present! Let's not log!"
    auto logfile = Logging::RegisterLog("LogAsync_NoOp.txt");

	std::vector<std::future<void>> futures; // A bunch of threads.
	std::atomic<bool> quit(false); // Tell the threads to quit
	std::atomic<int> wait(0); // Wait for all threads to be ready before starting timers...

	const unsigned numThreads = std::max<unsigned>(1, std::thread::hardware_concurrency() - 1);
	//const unsigned numThreads = 1;
	for (unsigned i = 0; i < numThreads; ++i)
	{
		futures.emplace_back(std::async(std::launch::async, [i, &quit, &wait, numThreads]()
		{
			int j = 0;
			++wait;

			// Wait for all threads to be ready before starting...
			while (wait != numThreads) { std::this_thread::sleep_for(microseconds(1)); }
			while (!quit.load(std::memory_order_relaxed))
			{
				// This is the only part of the setup that is related to logging.
				LOG_ASYNC("asdf") << "Thread " << i << " logging " << j++ << std::endl;
				/*
				for (unsigned q = 0; q < 8; ++q)
				{
					std::this_thread::yield();
				}
				*/
			}
		}));
	}

	// Wait for all threads to be ready before starting...
	while (wait != numThreads) { std::this_thread::sleep_for(microseconds(1)); }

	// Capture three seconds worth of logdata.
	std::this_thread::sleep_for(seconds(3));
	quit = true;
	for (auto& future : futures) { future.get(); }
	
	Logging::ShutdownLogging();
    return 0;
}