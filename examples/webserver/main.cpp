#include "server.h"
#include <CLI/CLI.hpp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fmt/core.h>

static std::unique_ptr< http::tcp_server > s_server; // NOLINT
void                                       exit_handler( int /*signal*/ )
{
    s_server->shutdown();
    exit( 1 ); // NOLINT
}

int main( int argc, char** argv )
{
    try
    {
        CLI::App app;

        std::string ip_address;
        app.add_option( "ip", ip_address, "IP Address your server will run from" )->required();
        unsigned short port = 8081; // NOLINT
        app.add_option( "-p", port, "Port number" );
        CLI11_PARSE( app, argc, argv );
        s_server.reset( new ( std::nothrow ) http::tcp_server( ip_address, port ) ); // NOLINT
        if ( !s_server )
        {
            return 1;
        }
        signal( SIGINT, exit_handler ); // NOLINT
        s_server->serve_forever();
        return 0;
    }
    catch ( std::exception const& e )
    {
        fmt::print( "Uncaught exception {}", e.what() );
        return 1;
    }
}