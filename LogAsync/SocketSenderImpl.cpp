#include "SocketSenderImpl.h"

#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <thread>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>


namespace NetworkIO
{
    // ------------------------------------------------------------------------------------
    // Network sender base class
    // ------------------------------------------------------------------------------------

    NetworkSender::NetworkSender(const std::string& ip, const std::string& port, std::shared_ptr<io_service> ios, const IP_Type ip_version) :
        LogSocket(),
        _ipaddr(ip),
        _port(port),
        _timeout_interval_seconds(DEFAULT_KEEPALIVE_PING_DURATION),
        _ioservice(ios),
        _ip_version(ip_version)
    {}

    void NetworkSender::SetTimeoutInterval(const int i)
    {
        _timeout_interval_seconds = i;
    }

    NetworkSender::~NetworkSender()
    {
        _localQuitLogging = true;
    }

    void NetworkSender::HandleQueue(const std::vector<LogData>& toLog)
    {
        // Skip network logging if network is down
        CheckConnection();
		std::string tmp;
        if (!_localQuitLogging && ConnectionIsOpen())
        {
            for (const auto& elem : toLog)
            {
                if (LogBase::MeetsLoggingCriteria(elem) && !_localQuitLogging && ConnectionIsOpen())
                {
                    // Ensure the message fits in a single udp/tcp message.
					tmp.clear();
                    _config.AppendLogToString(elem, tmp);
                    if (tmp.size() > 65535) { tmp.resize(65535); }
                    SendData(tmp);
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // TCP specific stuff, a bit more intensive than UDP due to the need to keep track of
    // more internal state regarding the connection.
    // ------------------------------------------------------------------------------------
    /*
    TCPSender::TCPSender(const std::string& ip, 
                         const std::string& port, 
                         const uint16_t keepalivePortFrom, 
                         std::shared_ptr<io_service> ios, 
                         const IP_Type ip_version) :
        NetworkSender(ip, port, ios, ip_version),
        _quitListening(false),
        _sendSocket(nullptr),
        _keepaliveTimeoutVal(keepalivePortFrom),
        _listeningPort(keepalivePortFrom),
        _timedOutAt(0),
        _keepaliveLastTime(0)
    {}

    TCPSender::~TCPSender()
    {
        _localQuitLogging = true;
        _quitListening = true;

        if (_sendSocket) 
        { 
            _sendSocket->close(); 
            _sendSocket = nullptr;
        }
    }

    void TCPSender::SetKeepAliveTimeSeconds(const int i)
    {
        _keepaliveTimeoutVal = std::max<int>(1, _keepaliveTimeoutVal);
    }

    bool TCPSender::ConnectionIsOpen()
    {
        return _ioservice && _sendSocket && _sendSocket->is_open();
    }

    void TCPSender::ReadFromPeer()
    {
        if (_localQuitLogging || _quitListening) { return; }
        auto buffer = std::make_shared<std::string>(64, '\0');

        boost::asio::async_read(*_sendSocket, 
                                boost::asio::buffer(&(*buffer)[0], buffer->size()),
                                [this, buffer](boost::system::error_code ec, std::size_t length)
                                {
                                    std::cout << "Reply is: " << buffer << std::endl;

                                    if (length)
                                    {
                                        std::cerr << "Received a ping: " << *buffer << std::endl;
                                        _keepaliveLastTime = chrono_clock::to_time_t(chrono_clock::now());
                                        ReadFromPeer();
                                    }
                                    else
                                    {
                                        std::cerr << "Connection to peer " << _sendSocket->remote_endpoint() << " lost?" << std::endl;
                                        _keepaliveLastTime = 0;
                                    }
                                });


    }

    void TCPSender::CheckConnection()
    {
        const auto now = chrono_clock::now();
        const time_t now_t = chrono_clock::to_time_t(now);
        const auto lastTime = _keepaliveLastTime.load();

        // If the current time is past the last time we've received a "heartbeat" message from the
        // server, then we'll need to consider the connection dead and stop sending.

        if (lastTime + _keepaliveTimeoutVal < now_t || !ConnectionIsOpen())
        {
            // Did we just time out?
            if (_timedOutAt == 0)
            { 
                std::cerr << "Connection has expired to " << _ipaddr << ":" << _port << std::endl;
                _timedOutAt = now_t;

                if (_sendSocket)
                {
                    _sendSocket->close();
                    _sendSocket = nullptr;
                    _quitListening = true;
                }
            }

            // Or have we been down for a long enough time to try reconnecting again?
            else if (_timedOutAt + _timeout_interval_seconds < now_t)
            {
                std::cerr << "Retrying connection to " << _ipaddr << ":" << _port << std::endl;
                _sendSocket = std::make_shared<tcp_socket>(*_ioservice);

                boost::system::error_code ec;
                if (ec) { std::cerr << "Error trying to reuse socket with ip " << _ipaddr << " port " << _port << std::endl; }
                
                try
                {
                    // Reconnect the sender.
                    tcp::resolver resolver(*_ioservice);
                    boost::asio::connect(*_sendSocket, resolver.resolve({_ipaddr, _port}));
                    _keepaliveLastTime = now_t;
                    _timedOutAt = 0;    
                    _quitListening = false;

                    std::thread(&TCPSender::ReadFromPeer, this).detach();
                }
                catch (boost::system::system_error const& e)
                {
                    std::cerr << "Reconnection unsuccessful, trying again in " << _timeout_interval_seconds << "seconds; " << e.what() << std::endl;
                    _timedOutAt = now_t;
                    _sendSocket->close();
                    _sendSocket = nullptr;
                }
            }    
        }
    }


    void TCPSender::SendData(const std::string& s)
    {
        if (ConnectionIsOpen())
        {
            boost::asio::async_write(*_sendSocket, boost::asio::buffer(s.data(), s.size()),
                                     [this](boost::system::error_code ec, std::size_t length)
                                        {
                                            if (ec)    { std::cerr << "Error sending async TCP data: " << ec.message() << std::endl; }
                                        });
        }
    }
    */
    // ------------------------------------------------------------------------------------
    // UDP sender, pretty basic and simple!
    // ------------------------------------------------------------------------------------

    UDPSender::UDPSender(const std::string& ip, const std::string& port, std::shared_ptr<io_service> ios, const IP_Type ip_version) :
        NetworkSender(ip, port, ios, ip_version),
        _socket(nullptr),
        _endpoint()
    {}

    UDPSender::~UDPSender()
    {
        _localQuitLogging = true;
        if (_socket) { _socket->close(); }
        _socket = nullptr;
    }

    void UDPSender::SendData(const std::string& s)
    {
        _socket->async_send_to(boost::asio::buffer(&s[0], s.size()),
                               _endpoint,
                               [this](boost::system::error_code ec, std::size_t len)
                                {
                                    if (ec) { std::cerr << "Error sending async UDP data: " << ec.message() << std::endl; }
                                });
    }

    bool UDPSender::ConnectionIsOpen()
    {
        return _ioservice && _socket && _socket->is_open();
    }

    void UDPSender::CheckConnection()
    {
        if (!ConnectionIsOpen())
        {
            std::cerr << "Retrying connection to " << _ipaddr << ":" << _port << std::endl;

            udp::resolver resolver(*_ioservice);
            _endpoint = *resolver.resolve({_ipaddr, _port});

            if (_socket) { _socket->close(); }
            _socket = std::make_shared<udp_socket>(*_ioservice);

            if (_ip_version == IP_Type::IP_V4) { _socket->open(udp::v4()); }
            else if (_ip_version == IP_Type::IP_V6) { _socket->open(udp::v6()); }
        }
    }
}