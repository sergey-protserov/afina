#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <afina/execute/Command.h>
#include <cstring>
#include <mutex>
#include <protocol/Parser.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps) : _socket(s), _alive(false), _ps(ps) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    bool isAlive();

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    static constexpr int EVENT_READ = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLET | EPOLLONESHOT;
    static constexpr int EVENT_WRITE = EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLET | EPOLLONESHOT;
    // когда ставить оба R и W? По идее, нужно либо R, либо RW. Обдумать эту идею.
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    bool _alive;
    std::mutex _m_state;

    // from mt_blocking import *
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    // переехали из локальной переменной сюда, т.к. есть суть состояние
    char client_buffer[4096];
    int readed_bytes;
    // ответы
    std::vector<std::string> _responses;
    // storage
    std::shared_ptr<Afina::Storage> _ps;
    int _bytes_written;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
