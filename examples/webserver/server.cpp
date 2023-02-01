#include "server.h"
#include "task_pool/traits.h"
#include <boost/pool/pool_alloc.hpp>
#include <cstring>
#include <exception>
#include <experimental/string_view>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <iterator>
#include <memory>
#include <sys/socket.h>
#include <sys/types.h>
#include <task_pool/pipes.h>
#include <unistd.h>
#include <utility>
#include <vector>

static constexpr int BUFFER_SIZE = 30720;
using string_view                = std::experimental::string_view;
using Data       = std::vector< std::uint8_t, boost::pool_allocator< std::uint8_t > >;
using SocketData = std::pair< int, Data >;

/**
 * This example is based on https://github.com/OsasAzamegbe/http-server please visit to view the
 * original
 */

void log_message_( string_view filename, int fileline, std::string const& msg )
{
    fmt::print(
        "[{:%Y-%m-%d %H:%M:%S} {}:{}] {}",
        fmt::localtime( std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() ) ),
        filename,
        fileline,
        msg );
}

// NOLINTNEXTLINE
#define LOGGER( i_fmt_file, i_fmt_line, ... )                                                 \
    []( char const* i_fmt_filename,                                                           \
        int         i_fmt_fileline,                                                           \
        auto&&      i_fmt_format,                                                             \
        auto&&... i_fmt_args ) {                                                              \
        constexpr std::size_t log_fmt_args_count = sizeof...( i_fmt_args );                   \
        if ( log_fmt_args_count )                                                             \
        {                                                                                     \
            log_message_(                                                                     \
                i_fmt_filename, i_fmt_fileline, fmt::format( i_fmt_format, i_fmt_args... ) ); \
        }                                                                                     \
        else                                                                                  \
        {                                                                                     \
            log_message_( i_fmt_filename, i_fmt_fileline, std::string( i_fmt_format ) );      \
        }                                                                                     \
    }( i_fmt_file, i_fmt_line, __VA_ARGS__ )

// NOLINTNEXTLINE
#define CONSOL_LOG( ... ) LOGGER( __FILE__, __LINE__, __VA_ARGS__ );

namespace http {

SocketData receive_data( int socket )
{
    std::array< char, BUFFER_SIZE > buffer{ 0 };
    ssize_t bytesReceived = read( socket, static_cast< void* >( buffer.data() ), BUFFER_SIZE );
    if ( bytesReceived < 0 )
    {
        CONSOL_LOG( string_view( "Error read from socket [{}]\n" ), strerror( errno ) );
        return {};
    };

    return std::make_pair(
        socket,
        Data( std::make_move_iterator( buffer.begin() ),
              std::make_move_iterator( buffer.begin() + sizeof( char ) * static_cast< std::size_t >(
                                                                             bytesReceived ) ) ) );
}

Data default_response()
{
    static std::string const htmlFile =
        "<!DOCTYPE html><html lang=\"en\"><body><h1> HOME </h1><p> Hello from your Server :) "
        "</p></body></html>";

    std::string response =
        fmt::format( "HTTP/1.1 200 OK\nContent-Type: text/html\nContent-Length: {}\n\n{}\n",
                     htmlFile.size(),
                     htmlFile );
    return { std::make_move_iterator( response.begin() ),
             std::make_move_iterator( response.end() ) };
}

SocketData parse_request( SocketData data )
{
    std::string msg( std::make_move_iterator( data.second.begin() ),
                     std::make_move_iterator( data.second.end() ) );
    CONSOL_LOG( msg );
    return { data.first, default_response() };
}

int send_response( SocketData data, be::stop_token abort )
{
    long bytesSent = 0;

    auto iter = data.second.begin();
    do
    {
        bytesSent = write( data.first,
                           &( *iter ),
                           static_cast< std::size_t >( std::distance( iter, data.second.end() ) ) );
        if ( bytesSent < 0 )
        {
            CONSOL_LOG( string_view( "Error occured sending response to client: [{}]\n" ),
                        strerror( errno ) );
        }
        std::advance( iter, bytesSent );
    } while ( bytesSent > 0 && !abort );

    return data.first;
}

void close_connection( int socket )
{
    close( socket );
    CONSOL_LOG( string_view( "Connection closed\n" ) );
}

tcp_server::tcp_server( std::string ip_address, unsigned short port )
    : m_ip_address( std::move( ip_address ) )
    , m_port( port )
    , m_socket()
    , m_socketAddress{}
    , m_socketAddress_len( sizeof( m_socketAddress ) )
    , m_pool( boost::fast_pool_allocator< char >() )
{
    m_socketAddress.sin_family      = AF_INET;
    m_socketAddress.sin_port        = htons( m_port );
    m_socketAddress.sin_addr.s_addr = inet_addr( m_ip_address.c_str() );
}

tcp_server::~tcp_server()
{
    close( m_socket );
}

// clang-format off
void tcp_server::serve_forever()
{
    if ( !start_server() )
    {
        return;
    }
    auto abort = m_pool.get_stop_token();
    while ( !abort )
    {
        // blocking call on main
        auto socket = accept_connection();
        // offload response to thread
        auto work = m_pool | [=] { return socket; }  
                           | &receive_data 
                           | &parse_request 
                           | &send_response;
        // pipes block on destroy so last job we manually submit and disgard the future
        auto end = m_pool.submit( &close_connection, std::move( work ) ); 
    }
}
// clang-format on

void tcp_server::shutdown()
{
    CONSOL_LOG( string_view( "Shutting down\n" ) );
    m_pool.abort();
}

bool tcp_server::start_server()
{
    m_socket = socket( AF_INET, SOCK_STREAM, 0 );
    if ( m_socket < 0 )
    {
        CONSOL_LOG( string_view( "Cannot create socket [{}]\n" ), strerror( errno ) );
        return false;
    }
    // NOLINTNEXTLINE
    if ( bind( m_socket, reinterpret_cast< sockaddr* >( &m_socketAddress ), m_socketAddress_len ) <
         0 )
    {
        CONSOL_LOG( string_view( "Cannot bind server to port [{}]\n" ), strerror( errno ) );
        return false;
    }
    CONSOL_LOG( string_view( "Server running on {}:{}\n" ), m_ip_address, m_port );
    return true;
}

int tcp_server::accept_connection()
{
    static constexpr int s_max_connections = 20;
    if ( listen( m_socket, s_max_connections ) < 0 )
    {
        CONSOL_LOG( string_view( "Cannot listen on port [{}]\n" ), strerror( errno ) );
        return 0;
    }
    auto socket = // NOLINTNEXTLINE
        accept( m_socket, reinterpret_cast< sockaddr* >( &m_socketAddress ), &m_socketAddress_len );

    if ( socket < 0 )
    {
        CONSOL_LOG( string_view( "Server failed to accept incoming connection [{}]\n" ),
                    strerror( errno ) );
        return 0;
    }
    return socket;
}

} // namespace http