#ifndef AFINA_NETWORK_COROUTINE_SERVER_H
#define AFINA_NETWORK_COROUTINE_SERVER_H

#include <afina/coroutine/Engine_Epoll.h>
#include <afina/execute/Command.h>
#include <afina/network/Server.h>
#include <protocol/Parser.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <iostream>

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

    // Port to listen for new connections, permits access only from
    // inside of accept_thread
    // Read-only
    uint16_t listen_port;

    // Socket to accept new connection on, shared between acceptors
    int _server_socket;

    // Network thread
    std::thread _thread;

    // Coroutine-aware variants of standard functions
    ssize_t _read(int fd, void *buf, size_t count);
    ssize_t _write(int fd, const void *buf, size_t count);
    int _accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

    // EPOLL instance shared between workers
    int _data_epoll_fd;

    // Curstom event "device" used to wakeup workers
    int _event_fd;

    // Whether network is running
    bool _running = false;

    // Idle func for coroutine engine
    // I will get here each time there
    // are no coroutines to be run
    void _idle_func();

    // Block on epoll: effectively, file a request to epoll
    // and give up current coroutine execution
    // epoll_wait will be called by _idle_func when the time
    // is right (that is, there is no coroutine to be
    // executed)
    void _block_on_epoll(int fd, uint32_t events);

    // Function to handle client connection
    // It will be run as coroutine for each new
    // connection
    void Connection(int client_socket);
};

} // namespace Coroutine
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_COROUTINE_SERVER_H
