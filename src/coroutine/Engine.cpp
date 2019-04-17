#include <afina/coroutine/Engine.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char FrameStartsHere;
    char *CoroutineStackEnd = &FrameStartsHere;
    if (CoroutineStackEnd > StackBottom) {
        ctx.High = CoroutineStackEnd;
        ctx.Low = StackBottom;
    } else {
        ctx.High = StackBottom;
        ctx.Low = CoroutineStackEnd;
    }
    uint32_t CoroutineStackSize = ctx.High - ctx.Low; // TOASK: какой тип использовать?
    char *&CtxBuffer = std::get<0>(ctx.Stack);
    uint32_t &CtxSize = std::get<1>(ctx.Stack);
    if (CoroutineStackSize > CtxSize) {
        delete CtxBuffer;
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

} // namespace Coroutine
} // namespace Afina
