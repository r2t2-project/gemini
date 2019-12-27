#pragma once

#include <deque>
#include <string>

#include "secure_socket.hh"

class NBSecureSocket : public SecureSocket
{
public:
  enum class State
  {
    not_connected = 0,

    /* connect() */
    needs_connect,
    needs_ssl_read_to_connect,
    needs_ssl_write_to_connect,

    /* accept() */
    needs_accept,
    needs_ssl_read_to_accept,
    needs_ssl_write_to_accept,

    needs_ssl_write_to_write,
    needs_ssl_write_to_read,
    needs_ssl_read_to_write,
    needs_ssl_read_to_read,
    ready,
    closed
  };

  enum class Mode
  {
    not_set,
    connect,
    accept
  };

private:
  Mode mode_{ Mode::not_set };
  State state_{ State::not_connected };

  std::deque<std::string> write_buffer_{};
  std::string read_buffer_{};

public:
  NBSecureSocket( SecureSocket&& sock )
    : SecureSocket( std::move( sock ) )
  {}

  void connect();
  void accept();

  void continue_SSL_connect();
  void continue_SSL_accept();
  void continue_SSL_write();
  void continue_SSL_read();

  std::string ezread();
  void ezwrite( const std::string& msg ) { write_buffer_.emplace_back( msg ); };
  void ezwrite( std::string&& msg ) { write_buffer_.emplace_back( move( msg ) ); };
  unsigned int buffer_bytes() const;

  void clear_buffer();

  bool something_to_write() const { return ( write_buffer_.size() > 0 ); }
  bool something_to_read() const { return ( read_buffer_.size() > 0 ); }

  State state() const { return state_; }
  Mode mode() const { return mode_; }

  bool ready() const { return state_ == State::ready; }

  bool connected() const
  {
    return ( state_ != State::needs_connect ) and ( state_ != State::needs_ssl_read_to_connect ) and
           ( state_ != State::needs_ssl_write_to_connect );
  }

  bool accepted() const
  {
    return ( state_ != State::needs_accept ) and ( state_ != State::needs_ssl_read_to_accept ) and
           ( state_ != State::needs_ssl_write_to_accept );
  }
};
