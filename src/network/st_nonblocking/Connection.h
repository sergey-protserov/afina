#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <sys/epoll.h>
#include <protocol/Parser.h>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace STnonblock {

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
    static constexpr int EVENT_READ = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLET;
    static constexpr int EVENT_WRITE = EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLET;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    bool _alive;

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

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
