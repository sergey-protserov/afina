#include <afina/coroutine/Engine.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char FrameStartsHere; // фактически перед этой переменной может располагаться CoroutineStackSize, компилятор может
                          // изменить порядок. На самом деле, это не важно. Важно, что я сохраню всё, что необходимо - а
                          // это выполнено.
    char *CoroutineStackEnd = &FrameStartsHere;
    if (CoroutineStackEnd > StackBottom) {
        ctx.High = CoroutineStackEnd;
        ctx.Low = StackBottom;
    } else {
        ctx.High = StackBottom;
        ctx.Low = CoroutineStackEnd;
    }
    size_t CoroutineStackSize = ctx.High - ctx.Low;
    char *&CtxBuffer = std::get<0>(ctx.Stack);
    size_t &CtxSize = std::get<1>(ctx.Stack);
    if (CoroutineStackSize > CtxSize) {
        if (CtxBuffer != nullptr) {
            delete CtxBuffer;
        }
        CtxSize = CoroutineStackSize;
        CtxBuffer = new char[CoroutineStackSize]; // TOASK: char а не какой-нибудь std::byte? Последний появился в C++17
    }
    std::memcpy(CtxBuffer, ctx.Low, CoroutineStackSize);
}

void Engine::Restore(context &ctx) {
    char FrameStartsHere;
    while ((ctx.Low <= &FrameStartsHere) && (ctx.High >= &FrameStartsHere)) { // TOASK: подходит для обоих возможных
        // направлений роста стека? По идее, да,
        // если Restore не может быть вызвана
        // раньше, чем Start. По идее, это так.
        // Я могу перетереть this на стеке. По идее, он мне нигде не нужен?
        Restore(ctx); // уезжаем дальше по стеку, чтобы не перетереть фрейм этой функции
        // На самом деле, фрейм этой функции мне не нужен. Важно вот что: если я раскатаю сохранённый стек поверх фрейма
        // этой функции, а longjmp отработает как вызов функции (иногда он есть суть макрос, но не всегда), я этот
        // свежераскатанный стек сломаю.
    }
    std::memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.High - ctx.Low);
    longjmp(ctx.Environment, 1);
}

void Engine::Enter(context &ctx) {
    if ((cur_routine != nullptr) && (cur_routine != idle_ctx)) {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
    }
    cur_routine = &ctx;
    Restore(ctx);
}

void Engine::yield() { // TOASK: одна и та же корутина может дважды входить в список alive? Если да, мой код ошибочен.
    context *cand_coroutine = alive;
    if (cand_coroutine != nullptr) {
        if (cand_coroutine == cur_routine) {
            cand_coroutine = cand_coroutine->next;
            if (cand_coroutine != nullptr) {
                Enter(*cand_coroutine);
            }
        } else {
            Enter(*cand_coroutine);
        }
    }
    // in Engine.cpp if we failed to switch to another coroutine,
    // we simply returned to the caller
    // but what if it is blocked or invalid?
    // In this case we will return to some idle function and wait...
    if ((cur_routine != nullptr) && !(cur_routine->blocked)) {
        return;
    }
    if (cur_routine != idle_ctx) {
        Enter(*idle_ctx);
    }
    // After this, idle_ctx will call yield one more time, and previous
    // cur_routine is no longer a candidate, because it is either not alive
    // or blocked
    // If no coroutines to launch, this check
    // > if (cur_routine && !(cur_routine->blocked)) {
    // will be true for idle_ctx, it will return and get to run
    // idle_func
    // In fact, even if this check fails,
    // check "cur_routine != idle_ctx" is sure to fail,
    // and we continue to idle_func anyway
}

void Engine::sched(void *routine_) {
    if (routine_ == nullptr) {
        if (cur_routine != nullptr) {
            return;
        } else {
            yield();
        }
    } else if (routine_ != cur_routine) {
        Enter(*static_cast<context *>(routine_));
    }
}

void Engine::MoveCoroutine(context *&fromlist, context *&tolist, context *routine) {
    if (fromlist == tolist) {
        return;
    }
    if (routine->next != nullptr) {
        routine->next->prev = routine->prev;
    }
    if (routine->prev != nullptr) {
        routine->prev->next = routine->next;
    }
    if (routine == fromlist) {
        fromlist = fromlist->next;
    }
    routine->next = tolist;
    tolist = routine;
    if (routine->next != nullptr) {
        routine->next->prev = routine;
    }
}

void Engine::Block() {
    if (cur_routine->blocked) {
        throw std::runtime_error("Attempt to block already blocked coroutine");
    }
    cur_routine->blocked = true;
    MoveCoroutine(alive, blocked, cur_routine);
    yield();
}

void Engine::Wake(context *ctx) {
    if (!(ctx->blocked)) {
        throw std::runtime_error("Attempt to wake non-blocked coroutine");
    }
    ctx->blocked = false;
    MoveCoroutine(blocked, alive, ctx);
}

void Engine::WakeAll(void) {
    for (context *ctx = blocked; ctx != nullptr; ctx = blocked) {
        Wake(ctx);
    }
}

bool Engine::is_all_blocked() const { return !alive && blocked; }

Engine::context *Engine::get_cur_routine() const { return cur_routine; }

} // namespace Coroutine
} // namespace Afina
