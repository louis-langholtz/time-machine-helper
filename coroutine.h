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

    TaskType get_return_object() {
        return {std::coroutine_handle<promise_type>::from_promise(
            static_cast<promise_type&>(*this))};
    }

    std::suspend_never initial_suspend() { return {}; }

    struct final_awaiter
    {
        bool await_ready() noexcept {
            return false;
        }
        void await_resume() noexcept {
        }
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_type> h) noexcept
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

    final_awaiter final_suspend() noexcept {
        return {};
    }

    // Uses code from https://en.cppreference.com/w/cpp/language/coroutines
    void unhandled_exception() {
        exception = std::current_exception();;
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

    bool await_ready()
    {
        return this->value_to_await.has_value();
    }

    void await_suspend(std::coroutine_handle<> h)
    {
        this->previous = h;
    }

    AwaitType await_resume()
    {
        assert(this->value_to_await.has_value());
        auto tmp = *this->value_to_await;
        this->value_to_await.reset();
        return tmp;
    }

    void set_value(AwaitType value)
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
    coroutine_task& operator=(const coroutine_task&) = delete;

    coroutine_task(coroutine_task&& other) noexcept:
        h_(std::exchange(other.h_, {}))
    {
    }

    coroutine_task& operator=(coroutine_task&& other) noexcept
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
        bool await_ready() {
            return false;
        }
        ReturnType await_resume() {
            return coro.promise().value_to_return;
        }
        void await_suspend(std::coroutine_handle<> h)
        {
            coro.promise().previous = h;
        }
        std::coroutine_handle<promise_type> coro;
    };
    awaiter operator co_await() { return awaiter{h_}; }

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
