#pragma once

#include <boost/asio.hpp>
#include "LogHandler.h"

// It should support TCP reconnect and disconnect, and while this functionality is mostly proof-of-concept,
// it's been tested both over localhost and over a local wireless network with a python script receiving the data.
//
// IPV6 functionality has not been tested.

namespace NetworkIO
{
    // This is the main system managing and working with all the network systems.
    // Since ASIO has this thing with an ioservice and a bunch of tasks that need
    // to be both started and shut down cleanly, and are reference it, the interface
    // they're all going to be managed by the NetworkHandler.

    constexpr unsigned DEFAULT_LISTENING_PORT = 5015;

    class LogSocket : public LogBase
    {
    public:
        LogSocket() : LogBase() {}
        virtual ~LogSocket() {}
        virtual void HandleQueue(const std::vector<LogData>& toLog) = 0;
        virtual void SetTimeoutInterval(const int numSeconds) = 0;
    };

    std::shared_ptr<LogSocket> RegisterUDPv4_Destination(const std::string& ip, const std::string& port);
    std::shared_ptr<LogSocket> RegisterUDPv6_Destination(const std::string& ip, const std::string& port);

    // @TODO:
    // TCP needs to be looked at because I can't get it to ping pack over localhost to maintain both a
    // keepalive mechanism, and way to cut down on logging if the socket isn't connected...

    /*
    std::shared_ptr<LogSocket> RegisterTCPv4_Destination(const std::string& ip, 
                                                         const std::string& port,
                                                         const uint16_t keepalivePort);

    std::shared_ptr<LogSocket> RegisterTCPv6_Destination(const std::string& ip,
                                                         const std::string& port,
                                                         const uint16_t keepalivePort);
     */

}
