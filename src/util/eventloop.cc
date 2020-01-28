#include "eventloop.hh"
#include "exception.hh"
#include "socket.hh"
#include "timer.hh"

#include <iomanip>
#include <iostream>
#include <sstream>

using namespace std;

unsigned int EventLoop::FDRule::service_count() const
{
  return direction == Direction::In ? fd.read_count() : fd.write_count();
}

void EventLoop::add_rule( const string& name,
                          const FileDescriptor& fd,
                          const Direction direction,
                          const CallbackT& callback,
                          const InterestT& interest,
                          const CallbackT& cancel )
{
  _rule_info.push_back( { name, {} } );
  _fd_rules.push_back( { fd.duplicate(), direction, callback, interest, cancel, _rule_info.size() - 1 } );
}

void EventLoop::add_rule( const string& name, const CallbackT& callback, const InterestT& interest )
{
  _rule_info.push_back( { name, {} } );
  _non_fd_rules.push_back( { callback, interest, _rule_info.size() - 1 } );
}

EventLoop::Result EventLoop::wait_next_event( const int timeout_ms )
{
  // first, handle the non-file-descriptor-related rules
  {
    unsigned int iterations = 0;
    while ( true ) {
      ++iterations;
      bool rule_fired = false;
      for ( auto& this_rule : _non_fd_rules ) {
        if ( this_rule.interest() ) {
          if ( iterations > 128 ) {
            throw runtime_error( "EventLoop: busy wait detected: rule \"" + _rule_info.at( this_rule.info_index ).name
                                 + "\" is still interested after " + to_string( iterations ) + " iterations" );
          }

          rule_fired = true;
          RecordScopeTimer<Timer::Category::Nonblock> record_timer { _rule_info.at( this_rule.info_index ).timer };
          this_rule.callback();
        }
      }

      if ( not rule_fired ) {
        break;
      }
    }
  }

  // now the file-descriptor-related rules. poll any "interested" file descriptors
  vector<pollfd> pollfds {};
  pollfds.reserve( _fd_rules.size() );
  bool something_to_poll = false;

  // set up the pollfd for each rule
  for ( auto it = _fd_rules.begin(); it != _fd_rules.end(); ) { // NOTE: it gets erased or incremented in loop body
    auto& this_rule = *it;
    if ( this_rule.direction == Direction::In && this_rule.fd.eof() ) {
      // no more reading on this rule, it's reached eof
      this_rule.cancel();
      it = _fd_rules.erase( it );
      continue;
    }

    if ( this_rule.fd.closed() ) {
      this_rule.cancel();
      it = _fd_rules.erase( it );
      continue;
    }

    if ( this_rule.interest() ) {
      pollfds.push_back( { this_rule.fd.fd_num(), static_cast<short>( this_rule.direction ), 0 } );
      something_to_poll = true;
    } else {
      pollfds.push_back( { this_rule.fd.fd_num(), 0, 0 } ); // placeholder --- we still want errors
    }
    ++it;
  }

  // quit if there is nothing left to poll
  if ( not something_to_poll ) {
    return Result::Exit;
  }

  // call poll -- wait until one of the fds satisfies one of the rules (writeable/readable)
  {
    GlobalScopeTimer<Timer::Category::WaitingForEvent> timer;
    if ( 0 == CheckSystemCall( "poll", ::poll( pollfds.data(), pollfds.size(), timeout_ms ) ) ) {
      return Result::Timeout;
    }
  }

  // go through the poll results
  for ( auto [it, idx] = make_pair( _fd_rules.begin(), size_t( 0 ) ); it != _fd_rules.end(); ++idx ) {
    const auto& this_pollfd = pollfds[idx];
    auto& this_rule = *it;

    const auto poll_error = static_cast<bool>( this_pollfd.revents & ( POLLERR | POLLNVAL ) );
    if ( poll_error ) {
      /* see if fd is a socket */
      int socket_error = 0;
      socklen_t optlen = sizeof( socket_error );
      const int ret = getsockopt( this_rule.fd.fd_num(), SOL_SOCKET, SO_ERROR, &socket_error, &optlen );
      if ( ret == -1 and errno == ENOTSOCK ) {
        throw runtime_error( "error on polled file descriptor for rule \"" + _rule_info.at( this_rule.info_index ).name
                             + "\"" );
      } else if ( ret == -1 ) {
        throw unix_error( "getsockopt" );
      } else if ( optlen != sizeof( socket_error ) ) {
        throw runtime_error( "unexpected length from getsockopt: " + to_string( optlen ) );
      } else if ( socket_error ) {
        throw unix_error( "error on polled socket for rule \"" + _rule_info.at( this_rule.info_index ).name + "\"",
                          socket_error );
      }

      this_rule.cancel();
      it = _fd_rules.erase( it );
      continue;
    }

    const auto poll_ready = static_cast<bool>( this_pollfd.revents & this_pollfd.events );
    const auto poll_hup = static_cast<bool>( this_pollfd.revents & POLLHUP );
    if ( poll_hup && this_pollfd.events && !poll_ready ) {
      // if we asked for the status, and the _only_ condition was a hangup, this FD is defunct:
      //   - if it was POLLIN and nothing is readable, no more will ever be readable
      //   - if it was POLLOUT, it will not be writable again
      this_rule.cancel();
      it = _fd_rules.erase( it );
      continue;
    }

    if ( poll_ready ) {
      RecordScopeTimer<Timer::Category::Nonblock> record_timer { _rule_info.at( this_rule.info_index ).timer };
      // we only want to call callback if revents includes the event we asked for
      const auto count_before = this_rule.service_count();
      this_rule.callback();

      if ( count_before == this_rule.service_count() and ( not this_rule.fd.closed() ) and this_rule.interest() ) {
        throw runtime_error( "EventLoop: busy wait detected: rule \"" + _rule_info.at( this_rule.info_index ).name
                             + "\" did not read/write fd and is still interested" );
      }
    }

    ++it; // if we got here, it means we didn't call _fd_rules.erase()
  }

  return Result::Success;
}

constexpr double THOUSAND = 1000.0;

string EventLoop::summary() const
{
  ostringstream out;

  out << "EventLoop timing summary\n------------------------\n\n";

  for ( const auto& rule : _rule_info ) {
    const auto& name = rule.name;
    const auto& timer = rule.timer;

    out << "   " << name << ": ";
    out << string( 32 - name.size(), ' ' );
    out << Timer::pp_ns( timer.total_ns );

    out << "     [max=" << Timer::pp_ns( timer.max_ns ) << "]";
    out << " [count=" << timer.count << "]";
    out << "\n";
  }

  return out.str();
}
