#include "lst_timer.h"

using Clock = UtilTimer::Clock;
using TimePoint = UtilTimer::TimePoint;

TimerList::~TimerList() {
    clear();
}

void TimerList::clear() {
    UtilTimer* cur = head_;
    while (cur) {
        UtilTimer* next = cur->next;
        delete cur;
        cur = next;
    }
    head_ = tail_ = nullptr;
}

UtilTimer* TimerList::add_timer(int timeout_ms, UtilTimer::Callback cb) {
    if (timeout_ms <= 0) timeout_ms = 1;
    UtilTimer* timer = new UtilTimer;
    timer->expire = Clock::now() + std::chrono::milliseconds(timeout_ms);
    timer->cb = std::move(cb);
    add_timer(timer);
    return timer;
}

void TimerList::add_timer(UtilTimer* timer) {
    if (!timer) return;
    if (!head_) {
        head_ = tail_ = timer;
        timer->prev = timer->next = nullptr;
        return;
    }

    // 如果比头结点还早，插入到头部
    if (timer->expire <= head_->expire) {
        timer->next = head_;
        timer->prev = nullptr;
        head_->prev = timer;
        head_ = timer;
        return;
    }

    // 从头往后找到合适位置（保持升序）
    UtilTimer* cur = head_;
    while (cur->next && cur->next->expire < timer->expire) {
        cur = cur->next;
    }
    timer->next = cur->next;
    timer->prev = cur;
    if (cur->next) {
        cur->next->prev = timer;
    } else {
        tail_ = timer;
    }
    cur->next = timer;
}

void TimerList::adjust_timer(UtilTimer* timer, int timeout_ms) {
    if (!timer) return;
    if (timeout_ms <= 0) timeout_ms = 1;

    // 先从链表中摘除
    if (timer == head_ && timer == tail_) {
        head_ = tail_ = nullptr;
    } else if (timer == head_) {
        head_ = head_->next;
        if (head_) head_->prev = nullptr;
    } else if (timer == tail_) {
        tail_ = tail_->prev;
        if (tail_) tail_->next = nullptr;
    } else {
        if (timer->prev) timer->prev->next = timer->next;
        if (timer->next) timer->next->prev = timer->prev;
    }
    timer->prev = timer->next = nullptr;

    // 更新过期时间并重新插入
    timer->expire = Clock::now() + std::chrono::milliseconds(timeout_ms);
    add_timer(timer);
}

void TimerList::del_timer(UtilTimer* timer) {
    if (!timer) return;
    if (timer == head_ && timer == tail_) {
        head_ = tail_ = nullptr;
    } else if (timer == head_) {
        head_ = head_->next;
        if (head_) head_->prev = nullptr;
    } else if (timer == tail_) {
        tail_ = tail_->prev;
        if (tail_) tail_->next = nullptr;
    } else {
        if (timer->prev) timer->prev->next = timer->next;
        if (timer->next) timer->next->prev = timer->prev;
    }
    delete timer;
}

void TimerList::tick() {
    if (!head_) return;
    const TimePoint now = Clock::now();
    UtilTimer* cur = head_;
    while (cur) {
        if (cur->expire > now) {
            break;  // 后面的都还没到期
        }
        UtilTimer* next = cur->next;

        // 从链表中断开当前节点
        if (cur == head_ && cur == tail_) {
            head_ = tail_ = nullptr;
        } else if (cur == head_) {
            head_ = head_->next;
            if (head_) head_->prev = nullptr;
        } else if (cur == tail_) {
            tail_ = tail_->prev;
            if (tail_) tail_->next = nullptr;
        } else {
            if (cur->prev) cur->prev->next = cur->next;
            if (cur->next) cur->next->prev = cur->prev;
        }
        cur->prev = cur->next = nullptr;

        // 执行回调
        if (cur->cb) {
            cur->cb();
        }

        delete cur;
        cur = next;
    }
}

