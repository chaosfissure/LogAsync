#pragma once

#include <boost/asio.hpp>
#include <atomic>

#include "SocketSender.h"
#include "ConfigurationHandler.h"

namespace NetworkIO
{

    // ------------------------------------------------------------------------------------

    enum class IP_Type
    {
        IP_V4,
        IP_V6
    };
    
    using boost::asio::io_service;
    using boost::asio::ip::udp;
    using boost::asio::ip::tcp;

    typedef std::pair<std::string, std::string> IP_Port;
    typedef tcp::socket tcp_socket;
    typedef udp::socket udp_socket;

    constexpr unsigned DEFAULT_KEEPALIVE_PING_DURATION = 2;

    // ------------------------------------------------------------------------------------

    class NetworkSender : public LogSocket
    {
    protected:
        std::string _ipaddr;
        std::string _port;
        int64_t _timeout_interval_seconds;
        std::shared_ptr<io_service> _ioservice;
        IP_Type _ip_version;
        
    public:
        NetworkSender(const std::string& ip, const std::string& port, std::shared_ptr<io_service> ios, const IP_Type ip_version);
        virtual ~NetworkSender();

        void HandleQueue(const std::vector<LogData>& toLog);
        void SetTimeoutInterval(const int i);

        virtual void CheckConnection() = 0;
        virtual bool ConnectionIsOpen() = 0;

        virtual void SendData(const std::string& s) = 0;
    };

    // ------------------------------------------------------------------------------------
    /*
    class TCPSender : public NetworkSender, public std::enable_shared_from_this<TCPSender>
    {
    private:

        volatile bool _quitListening;
        std::shared_ptr<tcp_socket> _sendSocket;

        int _keepaliveTimeoutVal;
        unsigned _listeningPort;

        time_t _timedOutAt;
        std::atomic<time_t> _keepaliveLastTime;

        void ReadFromPeer();

    public:
        TCPSender(const std::string& ip, 
                  const std::string& portTo, 
                  const uint16_t keepalivePortFrom,
                  std::shared_ptr<io_service> ios, 
                  const IP_Type ip_version);

        ~TCPSender();

        void SetKeepAliveTimeSeconds(const int i);
        
        void CheckConnection();
        bool ConnectionIsOpen();

        void SendData(const std::string& s);
    };
    */
    // ------------------------------------------------------------------------------------

    class UDPSender : public NetworkSender
    {
    private:
        std::shared_ptr<udp_socket> _socket;
        udp::endpoint _endpoint;
    public:
        UDPSender(const std::string& ip, const std::string& port, std::shared_ptr<io_service> ios, const IP_Type ip_version);
        ~UDPSender();
        
        
        void CheckConnection();
        bool ConnectionIsOpen();

        void SendData(const std::string& s);
        
    };
}