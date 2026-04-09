#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <chrono>
#include <functional>

// 简单的升序定时器链表，每个节点一个回调，用于连接空闲超时关闭。

class UtilTimer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Callback = std::function<void()>;

    UtilTimer* prev = nullptr;
    UtilTimer* next = nullptr;
    TimePoint expire;   // 过期时间
    Callback cb;        // 触发时执行的回调
};

class TimerList {
public:
    TimerList() = default;
    ~TimerList();

    // 新建一个定时器，超时 timeout_ms 毫秒后触发回调，返回该定时器指针。
    UtilTimer* add_timer(int timeout_ms, UtilTimer::Callback cb);

    // 将已有定时器的过期时间调整为从现在起 timeout_ms 毫秒后。
    void adjust_timer(UtilTimer* timer, int timeout_ms);

    // 删除并销毁一个定时器（如果传入 nullptr 则直接返回）。
    void del_timer(UtilTimer* timer);

    // 执行到期的定时器回调并销毁对应节点。
    void tick();

private:
    void add_timer(UtilTimer* timer);
    void clear();

    UtilTimer* head_ = nullptr;
    UtilTimer* tail_ = nullptr;
};

#endif

