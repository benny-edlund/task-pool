#pragma once

#include <arpa/inet.h>
#include <boost/pool/pool_alloc.hpp>
#include <string>
#include <task_pool/pool.h>
#include <vector>

namespace http {

class tcp_server
{
public:
    tcp_server( std::string ip_address, unsigned short port );
    ~tcp_server();
    tcp_server( tcp_server const& ) = delete;
    tcp_server& operator=( tcp_server const& ) = delete;
    tcp_server( tcp_server&& ) noexcept        = delete;
    tcp_server& operator=( tcp_server&& ) noexcept = delete;

    bool start_server();
    void serve_forever();
    void shutdown();
    int  accept_connection();

private:
    std::string                                           m_ip_address;
    unsigned short                                        m_port;
    int                                                   m_socket;
    sockaddr_in                                           m_socketAddress;
    unsigned int                                          m_socketAddress_len;
    be::task_pool_t< boost::fast_pool_allocator< char > > m_pool;
};

} // namespace http
