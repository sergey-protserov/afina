#include "Connection.h"

#include <iostream>

namespace Afina {
namespace Network {
namespace STnonblock {

bool Connection::isAlive() {
    return _alive;
}

// See Connection.h
void Connection::Start() {
    _event.data.ptr = this;
    _event.events = EVENT_READ;
    _alive = true;
    readed_bytes = 0;
    _bytes_written = 0;
    arg_remains = 0;
    command_to_execute = nullptr;
    parser = Protocol::Parser{};
    argument_for_command.clear();
    _responses.clear();
}

// See Connection.h
void Connection::OnError() {
    _alive = false;
    shutdown(_socket, SHUT_RDWR);
}

// See Connection.h
void Connection::OnClose() {
    _alive = false;
    shutdown(_socket, SHUT_RDWR);
}

// See Connection.h
void Connection::DoRead() {
    command_to_execute = nullptr;
    try {
        int bytes_read_now = -1;
        while ((bytes_read_now = read(_socket, client_buffer + readed_bytes, sizeof(client_buffer) - readed_bytes)) >
               0) {
            readed_bytes += bytes_read_now;
            //             _logger->debug("Got {} bytes from socket", readed_bytes);
            while (readed_bytes > 0) {
                //                 _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                        //                         _logger->debug("Found new command: {} in {} bytes", parser.Name(),
                        //                         parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                if (command_to_execute && arg_remains > 0) {
                    //                     _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    //                     _logger->debug("Start command execution");
                    std::string result;
                    command_to_execute->Execute(*_ps, argument_for_command, result);
                    // Send response
                    result += "\r\n";
                    _responses.push_back(result);
                    _event.events = EVENT_READ | EVENT_WRITE;
                    _event.data.ptr = this;

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }
        if (readed_bytes == 0) {
            //             _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        //         _logger->error("Failed to process connection on descriptor {}: {}", client_socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {
    int response_amnt = _responses.size();
    if (response_amnt <= 0) {
        return;
    }
    // TODO: ssize_t и в остальных местах тоже?
    int now_written = -1;
    // TODO: в динамическую память?
    struct iovec iov[response_amnt];
    for (int i = 0; i < response_amnt; ++i) {
        iov[i].iov_base = const_cast<char *>(_responses[i].c_str());
        iov[i].iov_len = _responses[i].size();
    }
    iov[0].iov_base = static_cast<char *>(iov[0].iov_base) + _bytes_written;
    iov[0].iov_len -= _bytes_written;

    now_written = writev(_socket, iov, response_amnt);

    // разбудили, т.к. можно писать. если запись упала - это не EAGAIN, а что-то другое, выход
    if (now_written == -1) {
        OnClose(); // TODO: или OnError? Хотя они одинаковые
        return;
    }

    _bytes_written += now_written;
    int responses_written = 0;
    while ((responses_written < response_amnt) && (_bytes_written >= iov[responses_written].iov_len)) {
        _bytes_written -= iov[responses_written].iov_len;
        responses_written++;
    }
    _responses.erase(_responses.begin(), _responses.begin() + responses_written);
    if (_responses.empty()) {
        _event.events = EVENT_READ;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
