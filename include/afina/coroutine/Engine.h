#ifndef AFINA_COROUTINE_ENGINE_H
#define AFINA_COROUTINE_ENGINE_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <setjmp.h>
#include <tuple>

namespace Afina {
namespace Coroutine {

/**
 * # Entry point of coroutine library
 * Allows to run coroutine and schedule its execution. Not threadsafe
 */
class Engine final {
public:
    struct context;
    /**
     * A single coroutine instance which could be scheduled for execution
     * should be allocated on heap
     */
    typedef struct context {
        // coroutine stack start address
        char *Low = nullptr;

        // coroutine stack end address
        char *High = nullptr;

        // coroutine stack copy buffer
        std::tuple<char *, size_t> Stack = std::make_tuple(nullptr, 0);

        // Saved coroutine context (registers)
        jmp_buf Environment;

        // To include routine in the different lists, such as "alive", "blocked", e.t.c
        struct context *prev = nullptr;
        struct context *next = nullptr;

        // Whether this coroutine is blocked
        // Required, for example, to handle the following situation:
        // yield() called. If no good candidates, can we return to the
        // calling routine, or idle action should be performed instead?
        bool blocked = false;
    } context;

    /**
     * Where coroutines stack begins
     */
    // TOASK TODO ALARM ATTENTION!!!!!
    // Я переместил этот атрибут ниже по тексту в определении класса, и он стал зануляться при вызове Store из Run!!!
    // O_O!!!!
    // ЧТО ЭТО ПОЧЕМУ ЭТО АААААААА??
    char *StackBottom;

private:
    std::function<void()> idle_func;

    /**const int&
     * Current coroutine
     */
    context *cur_routine;

    /**
     * List of routines ready to be scheduled. Note that suspended routine ends up here as well
     */
    context *alive;

    /**
     * List of routines that are blocked (on IO)
     */
    context *blocked;

    /**
     * Context to be returned finally
     */
    context *idle_ctx;

protected:
    /**
     * Save stack of the current coroutine in the given context
     */
    void Store(context &ctx);

    /**
     * Restore stack of the given context and pass control to coroutinne
     */
    void Restore(context &ctx);

    /**
     * Suspend current coroutine execution and execute given context
     */
    void Enter(context &ctx);

    /**
     * Move coroutine from one list to another
     * This function assumes, that routine is in fromlist and not in tolist (not sure about the latter)
     */
    void MoveCoroutine(context *&fromlist, context *&tolist,
                       context *routine); // константность routine? ссылка routine?

public:
    Engine(std::function<void()> _idle_func)
        : StackBottom(0), idle_func(_idle_func), cur_routine(nullptr), alive(nullptr), blocked(nullptr),
          idle_ctx(nullptr) {}
    Engine() = delete;
    Engine(Engine &&) = delete;
    Engine(const Engine &) = delete;

    /**
     * Check if we must wait for something before
     * engine can continue processing it's coroutines.
     * It effectively means, that all the coroutines are
     * blocked
     * TODO: Returns true, if: no alive coroutines, but exist blocked coroutines
     * TODO: returns false if there are no alive and no blocked coroutines
     * In fact, this is the situation, when the engine has stopped
     * But should I check it more carefully?
     */
    bool is_all_blocked() const;

    /**
     * Get current coroutine pointer
     * It is used when waiting in epoll: once we finished
     * waiting, we get this pointer back to wake the corresponding
     * routine
     */
    context *get_cur_routine() const;

    /**
     * Gives up current routine execution and let engine to schedule other one. It is not defined when
     * routine will get execution back, for example if there are no other coroutines then executing could
     * be trasferred back immediately (yield turns to be noop).
     *
     * Also there are no guarantee what coroutine will get execution, it could be caller of the current one or
     * any other which is ready to run
     */
    void yield();

    /**
     * Suspend current routine and transfers control to the given one, resumes its execution from the point
     * when it has been suspended previously.
     *
     * If routine to pass execution to is not specified runtime will try to transfer execution back to caller
     * of the current routine, if there is no caller then this method has same semantics as yield
     */
    void sched(void *routine);

    /**
     * Block current coroutine
     * In fact, move it to "blocked" list, mark as blocked
     * and reset it's "events" bits. Just in case.
     */
    void Block();

    /**
     * Wake given coroutine (move from blocked to alive),
     * mark as not blocked
     */
    void Wake(context *ctx);

    /**
     * Wake all the coroutines in blocked
     * Required when the server is going to stop.
     */
    void WakeAll(void);

    /**
     * Entry point into the engine. Prepare all internal mechanics and starts given function which is
     * considered as main.
     *
     * Once control returns back to caller of start all coroutines are done execution, in other words,
     * this function doesn't return control until all coroutines are done.
     */
    void start(std::function<void()> main) {
        // To acquire stack begin, create variable on stack and remember its address
        char StackStartsHere; // TOASK: а нормально, что после него ещё идут pc, idle_ctx и т.д.?
        // Разве все эти переменные не должны быть в начале фрейма start, чтобы после StackBottom больше ничего
        // не было?
        // ОТВЕТ: idle_ctx - поле класса, лежит в области по указателю this, а он заведомо выше по стеку.
        // pc сразу передаётся в sched как копия
        // можем запросто перезаписать, не страшно. Обдумать и уточнить.
        this->StackBottom = &StackStartsHere;

        // Start routine execution
        void *pc = run(main);
        idle_ctx = new context();

        if (setjmp(idle_ctx->Environment) > 0) {
            // Here: correct finish of the coroutine section
            yield();
        } else if (pc != nullptr) {
            Store(*idle_ctx);
            sched(pc);
        }

        // TODO: idle_func должен ТОЛЬКО будить корутины, тогда его можно разместить после
        // yield в if setjmp:
        // Но тогда ещё нужен цикл. Если после idle_func не появилось живых корутин - выходим и
        // уничтожаем всё (см ниже)
        idle_func();

        // Shutdown runtime
        delete std::get<0>(idle_ctx->Stack);
        delete idle_ctx;       // не, а вот этот указатель мы не сломаем?
        this->StackBottom = 0; // TOASK: зачем?
    }

    /**
     * Register new coroutine. It won't receive control until scheduled explicitely or implicitly. In case of some
     * errors function returns -1
     */
    void *run(std::function<void()> func) {
        if (this->StackBottom == 0) {
            // Engine wasn't initialized yet
            return nullptr;
        }

        // New coroutine context that carries around all information enough to call function
        context *pc = new context();

        // Store current state right here, i.e just before enter new coroutine, later, once it gets scheduled
        // execution starts here. Note that we have to acquire stack of the current function call to ensure
        // that function parameters will be passed along
        if (setjmp(pc->Environment) > 0) {
            // Created routine got control in order to start execution. Note that all variables, such as
            // context pointer, arguments and a pointer to the function comes from restored stack

            // invoke routine
            // TODO: обработка ошибок - try catch, вот это вот всё?
            func();

            // Routine has completed its execution, time to delete it. Note that we should be extremely careful in where
            // to pass control after that. We never want to go backward by stack as that would mean to go backward in
            // time. Function run() has already return once (when setjmp returns 0), so return second return from run
            // would looks a bit awkward
            if (pc->prev != nullptr) {
                pc->prev->next = pc->next;
            }

            if (pc->next != nullptr) {
                pc->next->prev = pc->prev;
            }

            if (alive == cur_routine) {
                alive = alive->next;
            }

            // current coroutine finished, and the pointer is not relevant now
            cur_routine = nullptr;
            pc->prev = pc->next = nullptr;
            if (std::get<0>(pc->Stack) != nullptr) {
                delete std::get<0>(pc->Stack);
            }
            delete pc;

            // We cannot return here, as this function "returned" once already, so here we must select some other
            // coroutine to run. As current coroutine is completed and can't be scheduled anymore, it is safe to
            // just give up and ask scheduler code to select someone else, control will never returns to this one
            Restore(*idle_ctx);
        }

        // setjmp remembers position from which routine could starts execution, but to make it correctly
        // it is neccessary to save arguments, pointer to body function, pointer to context, e.t.c - i.e
        // save stack.
        Store(*pc);

        // Add routine as alive double-linked list
        pc->next = alive;
        pc->blocked = false; // TODO: спросить - нужна ли эта инициализация, или оно само из определения структуры?
        alive = pc;
        if (pc->next != nullptr) {
            pc->next->prev = pc;
        }

        return pc;
    }
};

} // namespace Coroutine
} // namespace Afina

#endif // AFINA_COROUTINE_ENGINE_H
