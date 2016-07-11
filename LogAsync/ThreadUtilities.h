#pragma once

#include <thread>
#include <mutex>

#include "TimeManip.h"

// A sleep that will terminate early if a condition is set.  Prevents sleeping threads from being impossible to
// reach or quit early if we need to globally terminate the application.
template< class Clock, class Duration, class T>
inline void InterruptedSleepUntil(const std::chrono::time_point<Clock, Duration>& tp, const T& terminateEarly)
{
	while (!terminateEarly && tp - Clock::now() > seconds(1)) { std::this_thread::sleep_for(milliseconds(512)); }
    if (!terminateEarly) { std::this_thread::sleep_until(tp); }
}

// A sleep that will terminate early if a condition is set.  Prevents sleeping threads from being impossible to
// reach or quit early if we need to globally terminate the application.
template<class TimeUnit, class T>
inline void InterruptedSleepFor(const TimeUnit t, const T& terminateEarly)
{
    InterruptedSleepUntil(steady_clock::now() + t, terminateEarly);
}

class ThreadRAII
{
private:
    volatile bool _killReq;
    std::thread _managed;

public:

    template <class ...Args>
    ThreadRAII(Args&& ...args) :
        _killReq(false),
        _managed(std::forward<Args>(args)..., std::ref(_killReq))
    {}

    ~ThreadRAII()
    {
        _killReq = true;
        if (_managed.joinable()) { _managed.join(); }
    }
};

