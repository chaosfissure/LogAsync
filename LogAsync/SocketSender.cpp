#include "SocketSender.h"
#include "SocketSenderImpl.h"

#include <atomic>
#include <iostream>


// --------------------------------------------------------------------------------------------
// Namespace vars for NetworkIO
// --------------------------------------------------------------------------------------------
namespace NetworkIO
{
    class NetworkIO_RAII;

    std::shared_ptr<boost::asio::io_service> _ioservice;
    class NetworkRAII;
    std::unique_ptr<NetworkIO_RAII> networkLifespan;
    std::atomic<bool> initialized(false);
}


// --------------------------------------------------------------------------------------------
// Terminate the IOService gracefully...
// --------------------------------------------------------------------------------------------
namespace NetworkIO
{
    class NetworkIO_RAII final
    {
    public:
        NetworkIO_RAII() {}
        ~NetworkIO_RAII()
        {
            if (_ioservice) { _ioservice->stop(); }
        }
    };
}

namespace NetworkIO
{
    void StartIOService()
    {
        if (!initialized.exchange(true))
        {
            networkLifespan = std::make_unique<NetworkIO_RAII>();
            _ioservice = std::make_shared<boost::asio::io_service>();
            _ioservice->run();
        }
    }

    std::shared_ptr<LogSocket> RegisterUDPv4_Destination(const std::string& ip, const std::string& port)
    {
        StartIOService();
        return std::make_shared<UDPSender>(ip, port, _ioservice, IP_Type::IP_V4);
    }
    std::shared_ptr<LogSocket> RegisterUDPv6_Destination(const std::string& ip, const std::string& port)
    {
        StartIOService();
        return std::make_shared<UDPSender>(ip, port, _ioservice, IP_Type::IP_V6);
    }

    /*
    std::shared_ptr<LogSocket> RegisterTCPv4_Destination(const std::string& ip, 
                                                         const std::string& port,
                                                         const uint16_t keepalivePort)
    {
        StartIOService();
        return std::make_shared<TCPSender>(ip, port, keepalivePort, _ioservice, IP_Type::IP_V4);
    }
    std::shared_ptr<LogSocket> RegisterTCPv6_Destination(const std::string& ip,
                                                         const std::string& port,
                                                         const uint16_t keepalivePort)
    {
        StartIOService();
        return std::make_shared<TCPSender>(ip, port, keepalivePort, _ioservice, IP_Type::IP_V6);
    }
    */

}
