#include "LogAsync.h"

int main(int argc, char* argv[])
{
    // We can also log to a socket rather than just to a log file.
    
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // It's important to know that these tend to be significantly slower than
    // file logs because every log is sent individually over UDP rather than
    // being broken up.  This will slow logging overall and consume significant
    // memory if you don't space apart logging calls.
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    
    // IPV6 functionality has not been tested, and TCP functionality isn't
    // working very well - it doesn't support reconnect in a clean way with
    // keepalive/heartbeat messages.  This is definitely in the list of stuff
    // to be done in the future, as well as buffering an arbitrary amount of
    // data before sending it off on a socket for increased efficiency.
    
    auto UDP = Logging::RegisterUDPv4_Destination("10.0.0.5", "5000");
	auto UDPLogMirror = Logging::RegisterLog("LogAsync_NetworkMirror.txt");
    
    // Sockets are inherited from a LogBase class - which means you can filter what they log.
    UDP->AddInputFilter([](const LogData& l) { return l._tags.find("Cheerio") != l._tags.end(); });
	UDPLogMirror->AddInputFilter([](const LogData& l) { return l._tags.find("Cheerio") != l._tags.end(); });

    volatile bool quit = false;
    std::thread tmp([&quit]() 
    {
        unsigned counter = 0;
        while (!quit)
        {
            LOG_ASYNC("Cheerio") << "Sending cheerios.  Total sent: " << counter++ << std::endl;
            std::this_thread::sleep_for(milliseconds(1));
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));
    quit = true;

    if (tmp.joinable()) { tmp.join(); }
    
    Logging::ShutdownLogging();
   
    return 0;
}