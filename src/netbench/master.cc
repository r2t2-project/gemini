#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "util/address.hh"
#include "util/eventloop.hh"
#include "util/socket.hh"

using namespace std;

void usage( const char* argv0 )
{
  cerr << "Usage: " << argv0 << " PUBLIC-IP PUBLIC-PORT" << endl;
}

void program_body( const int, const char* argv[] )
{
  EventLoop event_loop;

  const string ip { argv[1] };
  const uint16_t port = static_cast<uint16_t>( stoi( argv[2] ) );
  const Address listen_address { ip, port };

  TCPSocket listen_sock;
  listen_sock.set_reuseaddr();
  listen_sock.set_blocking( false );
  listen_sock.bind( listen_address );
  listen_sock.listen();

  auto listener_callback = [&]() {
    TCPSocket sock = listen_sock.accept();
    cerr << "connected: " << sock.peer_address().to_string() << endl;
  };

  event_loop.add_rule( "TCPListen",
                       listen_sock,
                       Direction::In,
                       listener_callback,
                       []() { return true; },
                       []() { throw runtime_error( "listener failed." ); } );

  while ( event_loop.wait_next_event( -1 ) != EventLoop::Result::Exit )
    ;
}

int main( int argc, const char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort();
    }

    if ( argc != 3 ) {
      usage( argv[0] );
      return EXIT_FAILURE;
    }

    program_body( argc, argv );
  } catch ( const exception& e ) {
    cerr << "Exception: " << e.what() << endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
