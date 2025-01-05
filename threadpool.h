#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <assert.h>
//定义了这个pool之后，就会立马开启线程开八个线程从任务队列中找任务来做。你只需要调用这个里面的addtask就可以往里面扔函数，然后他就能自己做了。
//任务队列是封装了一个pool对象来实现的。


class ThreadPool {
public:
    ThreadPool() = default;//声明默认的构造函数还是存在的
    ThreadPool(ThreadPool&&) = default;//声明移动构造函数也是默认的
    // 尽量用make_shared代替new，如果通过new再传递给shared_ptr，内存是不连续的，会造成内存碎片化
    explicit ThreadPool(int threadCount = 8) : pool_(std::make_shared<Pool>()) { // make_shared:传递右值，功能是在动态内存中分配一个对象并初始化它，返回指向此对象的shared_ptr
        assert(threadCount > 0);//返回一个pool类型？这个pool类型里面还有任务属性
        for(int i = 0; i < threadCount; i++) {
            std::thread([this]() {//用lambda函数作为工作函数，不断再task队列里去取任务来做
                std::unique_lock<std::mutex> locker(pool_->mtx_);//对线程池加锁
                while(true) {
                    if(!pool_->tasks.empty()) {
                        auto task = std::move(pool_->tasks.front());    // 左值变右值,资产转移
                        pool_->tasks.pop();
                        locker.unlock();    // 因为已经把任务取出来了，所以可以提前解锁了
                        task();
                        locker.lock();      // 马上又要取任务了，上锁
                    } else if(pool_->isClosed) {
                        break;
                    } else {
                        pool_->cond_.wait(locker);    // 等待,如果任务来了就notify的
                    }
                    
                }
            }).detach();//detach可以使得当前线程脱离父线程，自己来决定自己的生死
        }
    }

    ~ThreadPool() {
        if(pool_) {
            std::unique_lock<std::mutex> locker(pool_->mtx_);
            pool_->isClosed = true;
        }
        pool_->cond_.notify_all();  // 唤醒所有的线程
    }

    template<typename T>
    void AddTask(T&& task) {//将函数指针传入到这个addtask里面
        std::unique_lock<std::mutex> locker(pool_->mtx_);
        pool_->tasks.emplace(std::forward<T>(task));
        pool_->cond_.notify_one();
    }

private:
    // 用一个结构体封装起来，方便调用
    struct Pool {//主要是来记录这个任务的。
        std::mutex mtx_;//有这个池的锁
        std::condition_variable cond_;//有使用这个池对应的条件变量
        bool isClosed;
        std::queue<std::function<void()>> tasks; // 任务队列，函数类型为void()
    };
    std::shared_ptr<Pool> pool_;
};

#endif
