#ifndef AFINA_NETWORK_COROUTINE_SERVER_H
#define AFINA_NETWORK_COROUTINE_SERVER_H

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <afina/Storage.h>
#include <afina/logging/Service.h>
#include <spdlog/logger.h>

#include "Connection.h"
#include "Utils.h"
#include <afina/coroutine/Engine.h>
#include <afina/execute/Command.h>
#include <afina/network/Server.h>
#include <protocol/Parser.h>

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace Coroutine {

/**
 * # Network resource manager implementation
 * Epoll & Coroutine based server
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl, bool local);
    ~ServerImpl();

    // See Server.h
    void Start(uint16_t port, uint32_t acceptors, uint32_t workers) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

protected:
    void OnRun();

private:
    static constexpr int EVENT_READ =
        EPOLLIN | EPOLLRDHUP | EPOLLERR |
        EPOLLHUP; // убрал LET и ONESHOT. Почему в Single Thread не нужен LET? Почему не нужен ONESHOT - очевидно.
    static constexpr int EVENT_WRITE = EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP;

    // logger to use
    std::shared_ptr<spdlog::logger> _logger;

    // Coroutine engine
    Afina::Coroutine::Engine _engine;

    // Network thread
    std::thread _thread;

    std::mutex _m;
    // List of connections
    Connection *conns;

    // Socket to accept new connection on, shared between acceptors
    int _server_socket;

    // EPOLL instance shared between workers
    int _data_epoll_fd;

    // Curstom event "device" used to wakeup workers
    int _event_fd;

    // Whether network is running
    bool _running = false;

private:
    // Coroutine-aware variants of standard functions
    ssize_t _read(int fd, void *buf, size_t count, Connection *conn);
    ssize_t _write(int fd, const void *buf, size_t count, Connection *conn);
    int _accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen, Connection *conn);

    // Idle func for coroutine engine
    // I will get here each time there
    // are no coroutines to be run
    void _idle_func();

    // Block on epoll: effectively, file a request to epoll
    // and give up current coroutine execution
    // epoll_wait will be called by _idle_func when the time
    // is right (that is, there is no coroutine to be
    // executed)
    void _block_on_epoll(int fd, uint32_t events, Connection *conn);

    // Function to handle client connection
    // It will be run as coroutine for each new
    // connection
    void Worker(int client_socket);

    void del_conn_from_list(Connection *cur_conn);
};

} // namespace Coroutine
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_COROUTINE_SERVER_H
