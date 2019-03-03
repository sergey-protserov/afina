#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Afina {
namespace Concurrency {

/**
 * # Thread pool
 */
class Executor {
public:
    Executor(const std::string &name, int low_watermark, int high_watermark, int max_queue_size,
             std::chrono::milliseconds idle_time);

    ~Executor() { this->Stop(false); };

private:
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

public:
    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false);

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types &&... args);

    // TOASK: как правильно удалять конструкторы/operator=?
    // переносить в private или =delete?
private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    friend void perform(Executor *executor);

private:
    /**
     * Mutex to protect state below from concurrent modification
     */
    std::mutex _mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable _empty_condition;

    /**
     * Conditional variable to await for server stop
     */
    std::condition_variable _stop_condition;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> _tasks;

    /**
     * Flag to stop bg threads
     */
    State _state;

    /**
     * Name of this threadpool
     */
    std::string _name;

    /**
     * Minimal amount of threads to be present
     */
    int _low_watermark;

    /**
     * Maximum amount of threads to be present
     */
    int _high_watermark;

    /**
     * Maximum amount of tasks in the queue
     */
    int _max_queue_size;

    /**
     * If there are more than _low_watermark
     * threads, they wait for a new task
     * for _idle_time milliseconds before
     * disappearing
     */
    std::chrono::milliseconds _idle_time;

    /**
     * Current amount of active workers
     */
    int _cur_workers;

    /**
     * Current amount of free workers
     */
    int _free_workers;
};

void perform(Executor *executor);

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
