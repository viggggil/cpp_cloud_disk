#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "../lock/locker.h"

#include <list>
#include <pthread.h>
#include <stdexcept>
#include <functional>

template <typename T>
class ThreadPool {
public:
    explicit ThreadPool(int thread_num = 8, int max_requests = 10000)
        : thread_num_(thread_num),
          max_requests_(max_requests),
          stop_(false),
          threads_(nullptr),
          db_pool_(nullptr) {
        if (thread_num_ <= 0 || max_requests_ <= 0) {
            throw std::invalid_argument("thread_num and max_requests must be positive");
        }

        threads_ = new pthread_t[thread_num_];
        for (int i = 0; i < thread_num_; ++i) {
            if (pthread_create(threads_ + i, nullptr, worker, this) != 0) {
                delete[] threads_;
                threads_ = nullptr;
                throw std::runtime_error("pthread_create failed");
            }
            if (pthread_detach(threads_[i]) != 0) {
                delete[] threads_;
                threads_ = nullptr;
                throw std::runtime_error("pthread_detach failed");
            }
        }
    }

    ~ThreadPool() {
        stop_ = true;
        for (int i = 0; i < thread_num_; ++i) {
            queue_stat_.post();
        }
        delete[] threads_;
    }

    bool append(T* request) {
        if (request == nullptr) {
            return false;
        }

        queue_locker_.lock();
        if (static_cast<int>(work_queue_.size()) >= max_requests_) {
            queue_locker_.unlock();
            return false;
        }
        work_queue_.push_back({request, nullptr});
        queue_locker_.unlock();
        queue_stat_.post();
        return true;
    }

    bool append(T* request, const std::function<void(T*, void*)>& custom_handler) {
        if (request == nullptr || !custom_handler) {
            return false;
        }

        queue_locker_.lock();
        if (static_cast<int>(work_queue_.size()) >= max_requests_) {
            queue_locker_.unlock();
            return false;
        }
        work_queue_.push_back({request, custom_handler});
        queue_locker_.unlock();
        queue_stat_.post();
        return true;
    }

    void bind_db_pool(void* db_pool) { db_pool_ = db_pool; }
    void* db_pool() const { return db_pool_; }

    void set_default_handler(const std::function<void(T*, void*)>& handler) {
        default_handler_ = handler;
    }

    void set_before_task(const std::function<void(T*, void*)>& hook) { before_task_ = hook; }
    void set_after_task(const std::function<void(T*, void*)>& hook) { after_task_ = hook; }

    int size() const { return thread_num_; }

private:
    struct TaskItem {
        T* request;
        std::function<void(T*, void*)> handler;
    };

    static void* worker(void* arg) {
        ThreadPool* pool = static_cast<ThreadPool*>(arg);
        pool->run();
        return pool;
    }

    void run() {
        while (true) {
            queue_stat_.wait();
            if (stop_) {
                break;
            }

            queue_locker_.lock();
            if (work_queue_.empty()) {
                queue_locker_.unlock();
                continue;
            }
            TaskItem item = work_queue_.front();
            work_queue_.pop_front();
            queue_locker_.unlock();

            T* request = item.request;
            if (request == nullptr) {
                continue;
            }

            if (before_task_) {
                before_task_(request, db_pool_);
            }

            if (item.handler) {
                item.handler(request, db_pool_);
            } else if (default_handler_) {
                default_handler_(request, db_pool_);
            } else {
                request->process();
            }

            if (after_task_) {
                after_task_(request, db_pool_);
            }
        }
    }

    int thread_num_;
    int max_requests_;
    bool stop_;
    pthread_t* threads_;

    std::list<TaskItem> work_queue_;
    Locker queue_locker_;
    Semaphore queue_stat_;

    // Reserved for DB integration (MySQL pool or other metadata storage backend).
    void* db_pool_;
    std::function<void(T*, void*)> default_handler_;
    std::function<void(T*, void*)> before_task_;
    std::function<void(T*, void*)> after_task_;
};

#endif
