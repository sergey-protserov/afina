#include <afina/concurrency/Executor.h>
#include <iostream>

namespace Afina {
namespace Concurrency {

Executor::Executor(const std::string &name, int low_watermark, int high_watermark, int max_queue_size,
                   std::chrono::milliseconds idle_time)
    : _state(Executor::State::kRun), _name(name), _low_watermark(low_watermark), _high_watermark(high_watermark),
      _max_queue_size(max_queue_size), _idle_time(idle_time), _cur_workers(0), _free_workers(0) {
    std::unique_lock<std::mutex> lock(_mutex);
    std::thread tmp;
    for (int i = 0; i < _low_watermark; ++i) {
        tmp = std::thread(perform, this);
        tmp.detach();
    }
    _cur_workers = _low_watermark;
    _free_workers = _low_watermark;
};

void perform(Executor *executor) {
    auto task_or_stop = [executor]() {
        return !(executor->_tasks.empty()) || (executor->_state != Executor::State::kRun);
    };
    std::unique_lock<std::mutex> lock(executor->_mutex);
    while (executor->_state == Executor::State::kRun) {
        if (executor->_cur_workers > executor->_low_watermark) {
            if (!(executor->_empty_condition.wait_for(lock, executor->_idle_time, task_or_stop))) {
                // TODO: Здесь есть race condition? Мьютекс в этот момент захвачен?
                break;
            }
        } else {
            executor->_empty_condition.wait(lock, task_or_stop);
        }
        if (executor->_state != Executor::State::kRun) {
            break;
        }
        executor->_free_workers -= 1;
        std::function<void()> task = (executor->_tasks).back();
        executor->_tasks.pop_back();
        lock.unlock();
        try {
            (task)();
        } catch (const std::exception &e) {
            std::cout << "Something went wrong: " << e.what() << std::endl;
        } catch (...) {
            std::cout << "Something went totally wrong" << std::endl;
        }
        lock.lock();
        executor->_free_workers += 1;
    }
    executor->_cur_workers -= 1;
    executor->_free_workers -= 1;
    if ((executor->_state == Executor::State::kStopping) && (executor->_cur_workers == 0)) {
        executor->_state = Executor::State::kStopped;
        executor->_stop_condition.notify_one();
    }
}

template <typename F, typename... Types> bool Executor::Execute(F &&func, Types &&... args) {
    // Prepare "task"
    auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

    std::unique_lock<std::mutex> lock(_mutex);
    if ((_state != State::kRun) || (_tasks.size() >= _max_queue_size)) {
        return false;
    }

    // Enqueue new task
    _tasks.push_back(std::move(exec));
    if ((_free_workers == 0) && (_cur_workers < _high_watermark)) {
        std::thread tmp(perform, this);
        tmp.detach();
        _cur_workers += 1;
        _free_workers += 1;
    }
    // TOASK: нормально ли отпускать его до notify? По идее, так быстрее, ведь на том конце
    // не придётся виснуть на мьютексе, сработает концепция futex
    lock.unlock();
    _empty_condition.notify_one();
    return true;
}

void Executor::Stop(bool await) {
    if (_state == Executor::State::kStopped) {
        return;
    }
    std::unique_lock<std::mutex> lock(_mutex);
    _state = Executor::State::kStopping;
    if (_cur_workers == 0) {
        _state = Executor::State::kStopped;
        return;
    }
    _empty_condition.notify_all();
    if (await == true) {
        _stop_condition.wait(lock, [this]() { return (this->_state == Executor::State::kStopped); });
    }
}

} // namespace Concurrency
} // namespace Afina
