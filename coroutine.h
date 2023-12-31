#ifndef COROUTINE_H
#define COROUTINE_H

#include <cassert>
#include <coroutine>
#include <optional>

template <class TaskType, class ReturnType>
struct returning_promise {
    ReturnType value_to_return;
    std::coroutine_handle<> previous;
    std::exception_ptr exception;

    using promise_type = typename TaskType::promise_type;

    auto get_return_object() -> TaskType {
        return {std::coroutine_handle<promise_type>::from_promise(
            static_cast<promise_type&>(*this))};
    }

    auto initial_suspend() -> std::suspend_never { return {}; }

    struct final_awaiter
    {
        auto await_ready() noexcept -> bool {
            return false;
        }

        auto await_resume() noexcept -> void {
        }

        auto await_suspend(std::coroutine_handle<promise_type> h) noexcept
            -> std::coroutine_handle<>
        {
            // From https://en.cppreference.com/w/cpp/coroutine/noop_coroutine:
            // final_awaiter::await_suspend is called when the execution
            // of the current coroutine (referred to by 'h') is about to
            // finish. If the current coroutine was resumed by another
            // coroutine via co_await get_task(), a handle to that coroutine
            // has been stored as h.promise().previous. In that case,
            // return the handle to resume the previous coroutine.
            // Otherwise, return noop_coroutine(), whose resumption does
            // nothing.
            if (auto previous = h.promise().previous; previous) {
                return previous;
            }
            return std::noop_coroutine();
        }
    };

    auto final_suspend() noexcept -> final_awaiter {
        return {};
    }

    // Uses code from https://en.cppreference.com/w/cpp/language/coroutines
    void unhandled_exception() {
        exception = std::current_exception();
    }

    template<std::convertible_to<ReturnType> From>
    void return_value(From&& from)
    {
        value_to_return = std::forward<From>(from);
    }
};

template <class AwaitType>
struct await_handle {
    std::optional<AwaitType> value_to_await;
    std::coroutine_handle<> previous;

    auto await_ready() -> bool
    {
        return this->value_to_await.has_value();
    }

    void await_suspend(std::coroutine_handle<> h)
    {
        this->previous = h;
    }

    auto await_resume() -> AwaitType
    {
        assert(this->value_to_await.has_value());
        return *std::exchange(this->value_to_await, {});
    }

    auto set_value(AwaitType value) -> void
    {
        this->value_to_await = std::move(value);
        if (this->previous && !this->previous.done()) {
            this->previous.resume();
        }
    }
};

template <class ReturnType>
class coroutine_task {
public:
    using promise_type = returning_promise<coroutine_task, ReturnType>;

    coroutine_task() = default;

    coroutine_task(std::coroutine_handle<promise_type> h) : h_(h)
    {
    }

    coroutine_task(const coroutine_task&) = delete;
    auto operator=(const coroutine_task&) -> coroutine_task& = delete;

    coroutine_task(coroutine_task&& other) noexcept:
        h_(std::exchange(other.h_, {}))
    {
    }

    auto operator=(coroutine_task&& other) noexcept -> coroutine_task&
    {
        this->h_ = std::exchange(other.h_, {});
        return *this;
    }

    ~coroutine_task()
    {
        if(this->h_)
        {
            this->h_.destroy();
            this->h_ = {};
        }
    }

    struct awaiter
    {
        auto await_ready() -> bool {
            return false;
        }
        auto await_resume() -> ReturnType {
            return coro.promise().value_to_return;
        }
        void await_suspend(std::coroutine_handle<> h)
        {
            coro.promise().previous = h;
        }
        std::coroutine_handle<promise_type> coro;
    };

    auto operator co_await() -> awaiter {
        return awaiter{h_};
    }

    auto operator()() -> ReturnType
    {
        if (h_) {
            if (h_.promise().exception) {
                std::rethrow_exception(h_.promise().exception);
            }
            return std::exchange(h_.promise().value_to_return, {});
        }
        return {};
    }

private:
    std::coroutine_handle<promise_type> h_;
};

#endif // COROUTINE_H
