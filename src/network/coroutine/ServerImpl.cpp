#include "ServerImpl.h"

namespace Afina {
namespace Network {
namespace Coroutine {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl, bool local)
    : Server(ps, pl, local), _engine(std::bind(&ServerImpl::_idle_func, this)) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_acceptors, uint32_t n_workers) {
    _logger = pLogging->select("network");
    _logger->info("Start network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Create server socket
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;   // IPv4
    server_addr.sin_port = htons(port); // TCP port number
    if (!_local) {
        server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
    } else {
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Bind to loopback interface only
    }

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket: " + std::string(strerror(errno)));
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, (SO_KEEPALIVE), &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed: " + std::string(strerror(errno)));
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed: " + std::string(strerror(errno)));
    }

    make_socket_non_blocking(_server_socket);
    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed: " + std::string(strerror(errno)));
    }

    // Start IO workers
    _data_epoll_fd = epoll_create1(0);
    if (_data_epoll_fd == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    _event_fd = eventfd(0, EFD_NONBLOCK);
    if (_event_fd == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    _m.lock();
    conns = nullptr;
    _m.unlock();

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = this;
    if (epoll_ctl(_data_epoll_fd, EPOLL_CTL_ADD, _event_fd, &event)) {
        throw std::runtime_error("Failed to add eventfd descriptor to epoll");
    }

    _running = true;
    _thread = std::thread([this] { this->_engine.start(std::bind(&ServerImpl::OnRun, this)); });
}

// See Server.h
void ServerImpl::Stop() {
    _logger->warn("Stop network service");
    _running = false;

    _m.lock();
    for (auto connptr = conns; connptr != nullptr; connptr = connptr->next) {
        connptr->running = false;
    }
    _m.unlock();

    // Wakeup threads that are sleep on epoll_wait
    if (eventfd_write(_event_fd, 1)) {
        throw std::runtime_error("Failed to unlock coroutines");
    }
}

// See Server.h
void ServerImpl::Join() {
    if (_thread.joinable()) {
        _thread.join();
    }
    close(_server_socket);
}

// See ServerImpl.h
void ServerImpl::OnRun() {
    auto cur_rout = _engine.get_cur_routine();
    auto newconn = new Connection;
    newconn->events = 0;
    newconn->running = true;
    newconn->ctx = cur_rout;
    _m.lock();
    conns = newconn;
    _m.unlock();
    while (_running) {
        struct sockaddr in_addr;
        socklen_t in_len;
        in_len = sizeof(in_addr);
        int infd = _accept(_server_socket, &in_addr, &in_len, newconn);
        if (infd == -1) {
            continue;
        }

        // Print host and service info.
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        int retval =
            getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
        if (retval == 0) {
            _logger->info("Accepted connection on descriptor {} (host={}, port={})\n", infd, hbuf, sbuf);
        }

        _engine.run([this, infd]() { this->Worker(infd); });
    }
    del_conn_from_list(newconn);
}

void ServerImpl::Worker(int client_socket) {
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    auto conn = new Connection;
    conn->events = 0;
    conn->running = true;
    conn->ctx = _engine.get_cur_routine();
    _m.lock();
    conn->next = conns;
    conns = conn;
    if (conn->next != nullptr) {
        conn->next->prev = conn;
    }
    _m.unlock();
    try {
        int readed_bytes = -1;
        char client_buffer[4096];
        while (_running && (readed_bytes = _read(client_socket, client_buffer, sizeof(client_buffer), conn)) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);

            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (readed_bytes > 0) { // TODO: while _running && readed_bytes > 0?
                _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }
                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);
                    // Send response
                    result += "\r\n";
                    if (_write(client_socket, result.data(), result.size(), conn) == -1) {
                        break; // TODO: точно? Если мне из epoll пришла ошибка, то стоит ли продолжать общаться с этим
                               // сокетом? Мб вообще выход?
                    }

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }
        if (readed_bytes == 0) {
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", client_socket, ex.what());
    }
    del_conn_from_list(conn);
    close(client_socket);
}

void ServerImpl::_idle_func() {
    const int maxevents = 64; // MAGIC NUMBER
    // unique_ptr до какого-то стандарта для массива всё равно вызывает delete
    // без квадратных скобок, это ошибка.
    struct epoll_event events[maxevents]; // TOASK: какого размера объекты можно размещать на стеке?
    memset(events, 0, sizeof(events[0]) * maxevents);
    int n_events = -1;
    // TOASK: На cppreference написано, что std::array удовлетворяет
    // требованиям contiguous container'а с C++17. Получается, если я хочу
    // передавать _адрес_ области памяти со структурами, std::array для
    // этого не годится? TODO
    // Фактически, std::array вроде бы continuous, но я всё-таки перестрахуюсь.
    while (_engine.is_all_blocked()) {

        n_events = epoll_wait(_data_epoll_fd, events, maxevents, -1);

        if (n_events == -1) {
            throw std::runtime_error("Error while calling epoll_wait in _idle_func");
        }
        for (int i = 0; i < n_events; ++i) {
            // auto ready_routine = static_cast<Coroutine::Engine::context*>(events[i].data.ptr);
            // Could have just set all the necessary fields (in fact, only events), but context
            // is private in Engine, so will use special setter instead
            // вместо nullptr я должен какое-то другое значение передавать
            // почему? я ОБЯЗАН занулять структуру event_fd перед загрузкой в epoll,
            // и если у меня nullptr означает событие от event_fd, я фактически сливаю два случая в один, хотя они
            // разные: отсутствие инициализации и событие от event_fd
            if (events[i].data.ptr == this) { // special value, which means a signal from event_fd, server is stopping
                _engine.WakeAll();            // all the coroutines should wake up and get ready to stop
                continue;
            }
            auto cur_conn = static_cast<Connection *>(events[i].data.ptr);
            auto cur_rout = static_cast<Afina::Coroutine::Engine::context *>(cur_conn->ctx);
            if (cur_conn == nullptr) {
                throw std::runtime_error("1Nullptr in cur_rout->data.ptr");
            }
            cur_conn->events = events[i].events;
            _engine.Wake(cur_rout);
        }
    }
    _engine.yield();
}

void ServerImpl::_block_on_epoll(int fd, uint32_t events, Connection *cur_conn) {
    if (cur_conn == nullptr) {
        throw std::runtime_error("2Nullptr in cur_rout->data.ptr");
    }
    struct epoll_event struct_events;
    memset(&struct_events, 0, sizeof(struct_events));
    struct_events.events = events;
    struct_events.data.ptr = cur_conn;
    if (epoll_ctl(_data_epoll_fd, EPOLL_CTL_ADD, fd, &struct_events)) {
        throw std::runtime_error("Error while calling epoll_ctl in _block_on_epoll");
    }
    cur_conn->events = 0;
    _engine.Block();
    if (epoll_ctl(_data_epoll_fd, EPOLL_CTL_DEL, fd, &struct_events)) {
        throw std::runtime_error("Error while calling epoll_ctl in _block_on_epoll");
    }
}

ssize_t ServerImpl::_read(int fd, void *buf, size_t count, Connection *conn) {
    if (conn == nullptr) {
        throw std::runtime_error("3Nullptr in cur_rout->data.ptr");
    }
    while (conn->running) {
        ssize_t bytes_read = read(fd, buf, count);
        if (bytes_read > 0) {
            return bytes_read;
        } else {
            _block_on_epoll(fd, EVENT_READ, conn);
            uint32_t events = conn->events;
            if ((events & EPOLLRDHUP) || (events & EPOLLERR) || (events & EPOLLHUP)) {
                return read(fd, buf, count);
            }
        }
    }
    return -1;
}

ssize_t ServerImpl::_write(int fd, const void *buf, size_t count, Connection *conn) {
    if (conn == nullptr) {
        throw std::runtime_error("4Nullptr in cur_rout->data.ptr");
    }
    ssize_t written = 0;
    while (conn->running) {
        written += write(fd, static_cast<const void *>(static_cast<const char *>(buf) + written), count - written);
        if (written < count) {
            _block_on_epoll(fd, EVENT_WRITE, conn);
            uint32_t events = conn->events;
            if ((events & EPOLLERR) || (events & EPOLLHUP)) {
                return -1;
            }
        } else {
            return written;
        }
    }
    return -1;
}

int ServerImpl::_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen, Connection *conn) {
    while (conn->running) {
        int fd = accept4(sockfd, addr, addrlen,
                         SOCK_NONBLOCK | SOCK_CLOEXEC); // TODO: правильно ли я понимаю, что в любой непонятной ситуации
        // следует ставить CLOEXEC?
        if (fd == -1) {
            _block_on_epoll(sockfd, EVENT_READ, conn); // или только IN?
        } else {
            return fd;
        }
    }
    return -1;
}

void ServerImpl::del_conn_from_list(Connection *cur_conn) {
    std::lock_guard<std::mutex> lg(_m);
    if (cur_conn == nullptr) {
        throw std::runtime_error("5Nullptr in cur_rout->data.ptr");
    }
    if (cur_conn->prev != nullptr) {
        cur_conn->prev->next = cur_conn->next;
    }

    if (cur_conn->next != nullptr) {
        cur_conn->next->prev = cur_conn->prev;
    }

    if (conns == cur_conn) {
        conns = cur_conn->next;
    }
    cur_conn->prev = cur_conn->next = nullptr;
    delete cur_conn;
}

} // namespace Coroutine
} // namespace Network
} // namespace Afina
