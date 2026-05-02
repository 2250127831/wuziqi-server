/**
 * @file thread_pool.cpp
 * @brief 线程池实现
 * 
 * 使用 C++ 标准库实现：
 * - std::thread: 创建线程
 * - std::mutex: 互斥锁，保护共享数据
 * - std::condition_variable: 条件变量，线程间通信
 * - std::atomic: 原子变量，无需锁的操作
 */

#include "thread_pool.h"
#include "logger.h"
#include <chrono>  // std::this_thread::sleep_for

// =====================================================
// 第一部分：构造和析构
// =====================================================

/**
 * @brief 构造函数 - 创建工作线程
 * @param num_threads 线程数量
 * 
 * 流程：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │  for i = 0 to num_threads:                  │
 *   │    workers_.emplace_back(                  │
 *   │      &ThreadPool::workerThread, this)      │
 *   │  创建线程，立即执行 workerThread()          │
 *   └─────────────────────────────────────────────┘
 */
ThreadPool::ThreadPool(size_t num_threads) : pool_size_(num_threads) {
    LOG_INFO("创建线程池，线程数: %zu", num_threads);
    
    // 创建 num_threads 个工作线程
    for (size_t i = 0; i < num_threads; ++i) {
        // emplace_back 原地构造，比 push_back 更高效
        // &ThreadPool::workerThread 是成员函数指针
        // this 是传递给 workerThread 的参数
        workers_.emplace_back(&ThreadPool::workerThread, this);
    }
}

/**
 * @brief 析构函数 - 停止线程池
 */
ThreadPool::~ThreadPool() {
    shutdown();
}

// =====================================================
// 第二部分：停止线程池
// =====================================================

/**
 * @brief 停止线程池
 * 
 * 流程：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │  1. stop_.exchange(true)                   │
 *   │     - 设置 stop_ 为 true                    │
 *   │     - 返回旧值（如果已经是 true，说明已停止）│
 *   │                                             │
 *   │  2. condition_.notify_all()                 │
 *   │     - 唤醒所有等待中的线程                   │
 *   │     - 让它们退出等待循环                     │
 *   │                                             │
 *   │  3. for worker in workers_:                │
 *   │     worker.join()                          │
 *   │     - 等待每个线程结束                      │
 *   │     - 确保资源被正确清理                     │
 *   └─────────────────────────────────────────────┘
 */
void ThreadPool::shutdown() {
    // Step 1: 设置停止标志
    // exchange() 是原子操作：
    // - 设置新值
    // - 返回旧值
    if (stop_.exchange(true)) {
        return;  // 已经是 true，说明已经调用过 shutdown
    }

    // Step 2: 唤醒所有等待中的线程
    // 条件变量的 wait() 会阻塞线程
    // notify_all() 让所有线程都从 wait() 中醒来
    condition_.notify_all();

    // Step 3: 等待所有线程结束
    for (auto& worker : workers_) {
        // join() 会阻塞，直到线程执行完毕
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    LOG_INFO("线程池已关闭");
}

// =====================================================
// 第三部分：工作线程主循环 ⭐核心
// =====================================================

/**
 * @brief 工作线程的主循环
 * 
 * 这是线程池最核心的部分！
 * 每个工作线程都会执行这个函数
 * 
 * 流程图：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  while (true):                                  │   │
 *   │  │                                                 │   │
 *   │  │  ┌─────────────────────────────────────────┐   │   │
 *   │  │  │  获取锁 unique_lock<mutex>              │   │   │
 *   │  │  └─────────────────────────────────────────┘   │   │
 *   │  │                                                 │   │
 *   │  │  ┌─────────────────────────────────────────┐   │   │
 *   │  │  │  等待条件:                              │   │   │
 *   │  │  │    stop_ || !tasks_.empty()             │   │   │
 *   │  │  │                                         │   │   │
 *   │  │  │  当 stop_=true 且 tasks_为空时退出      │   │   │
 *   │  │  │  当 stop_=false 且有任务时继续          │   │   │
 *   │  │  │  当 stop_=true 且有任务时继续           │   │   │
 *   │  │  └─────────────────────────────────────────┘   │   │
 *   │  │                                                 │   │
 *   │  │  if stop_ && tasks_.empty():                  │   │
 *   │  │      释放锁                                    │   │
 *   │  │      return  // 退出线程                       │   │
 *   │  │                                                 │   │
 *   │  │  if !tasks_.empty():                          │   │
 *   │  │      task = tasks_.front()                    │   │
 *   │  │      tasks_.pop()                             │   │
 *   │  │      active_count_++                          │   │
 *   │  │  释放锁                                        │   │
 *   │  │                                                 │   │
 *   │  │  ┌─────────────────────────────────────────┐   │   │
 *   │  │  │  if task:                              │   │   │
 *   │  │  │      try:                              │   │   │
 *   │  │  │          task()  // 执行任务           │   │   │
 *   │  │  │      catch:                            │   │   │
 *   │  │  │          记录异常日志                   │   │   │
 *   │  │  │      active_count_--                  │   │   │
 *   │  │  └─────────────────────────────────────────┘   │   │
 *   │  │                                                 │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */
void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;

        {
            // Step 1: 获取锁
            // unique_lock 比 lock_guard 更灵活
            // 支持 condition_variable 的 wait()
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Step 2: 等待条件
            // condition_.wait() 会原子地：
            // 1. 释放锁（让其他线程可以获取锁）
            // 2. 等待 notify_one() 或 notify_all()
            // 3. 重新获取锁
            // 4. 检查 lambda 条件
            // 
            // lambda 返回 true = 退出等待
            // lambda 返回 false = 继续等待
            condition_.wait(lock, [this] {
                // 退出等待的条件：
                // 1. stop_ = true（线程池正在关闭）
                // 2. tasks_ 不为空（有新任务）
                return stop_.load() || !tasks_.empty();
            });

            // Step 3: 检查是否应该退出
            // 如果 stop_=true 且队列已空，线程退出
            if (stop_.load() && tasks_.empty()) {
                return;  // 线程结束
            }

            // Step 4: 从队列取出任务
            if (!tasks_.empty()) {
                // 取队首任务
                task = std::move(tasks_.front());
                // 移除队首
                tasks_.pop();
                // 增加活跃任务计数
                active_count_++;
            }
            
            // 锁在这里自动释放
        }

        // Step 5: 执行任务（锁外执行，不阻塞其他线程）
        if (task) {
            try {
                // 调用任务函数
                task();
            } catch (const std::exception& e) {
                // 捕获标准异常
                LOG_ERROR("线程池任务执行异常: %s", e.what());
            } catch (...) {
                // 捕获所有其他异常
                LOG_ERROR("线程池任务执行未知异常");
            }
            // 任务完成，减少活跃计数
            active_count_--;
        }
    }
}

// =====================================================
// 第四部分：状态查询
// =====================================================

/**
 * @brief 获取正在执行的任务数
 * @return 活跃任务数
 * 
 * 使用 atomic 的 load() 读取
 * 无需加锁
 */
size_t ThreadPool::getActiveCount() const {
    return active_count_.load();
}

/**
 * @brief 获取队列中的任务数
 * @return 等待执行的任务数
 * 
 * 需要加锁，因为 queue 不是线程安全的
 */
size_t ThreadPool::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

// =====================================================
// 第五部分：深入理解
// =====================================================

/*
┌─────────────────────────────────────────────────────────────────┐
│                    条件变量 (condition_variable) 详解            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  为什么需要条件变量？                                            │
│                                                                 │
│  如果不用条件变量，工作线程会这样：                               │
│                                                                 │
│  ❌ 错误方式（忙等待，浪费 CPU）：                                 │
│      while (tasks_.empty()) {                                   │
│          // 什么都不做，但占用 CPU！                             │
│          std::this_thread::sleep_for(1ms);  // 勉强能接受       │
│      }                                                           │
│      task = tasks_.front();                                     │
│      tasks_.pop();                                               │
│                                                                 │
│  ✅ 正确方式（阻塞等待，节省 CPU）：                               │
│      condition_.wait(lock, [](){ return !tasks_.empty(); });   │
│      task = tasks_.front();                                     │
│      tasks_.pop();                                               │
│                                                                 │
│  wait() 的工作原理：                                              │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  线程 A：                                                │    │
│  │    lock() → 获取锁                                      │    │
│  │    wait() → 释放锁，然后阻塞等待                         │    │
│  │    ... → 被唤醒后重新获取锁                              │    │
│  │    检查条件 → 如果不满足继续 wait()                       │    │
│  │    unlock() → 释放锁                                    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  线程 B（生产者）：                                       │    │
│  │    lock() → 获取锁                                      │    │
│  │    tasks_.push(task) → 添加任务                         │    │
│  │    notify_one() → 唤醒一个等待中的线程                   │    │
│  │    unlock() → 释放锁                                    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    atomic 原子变量详解                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  std::atomic<bool> vs bool + mutex：                            │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  bool + mutex（需要加锁）：                               │    │
│  │    mutex_.lock();                                       │    │
│  │    if (stop_) { ... }                                  │    │
│  │    mutex_.unlock();                                     │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  atomic<bool>（无需加锁，更高效）：                        │    │
│  │    if (stop_.load()) { ... }                           │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
│  atomic 的特点：                                                 │
│  - 保证操作的原子性（不会被中断）                                 │
│  - 适用于简单类型：bool, int, pointer                            │
│  - 比 mutex 更轻量                                               │
│  - 无需手动加锁/解锁                                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
*/

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：基本使用
void example1() {
    ThreadPool pool(4);  // 4 个工作线程
    
    // 提交简单的 lambda 任务
    pool.enqueue([]() {
        std::cout << "任务执行中，线程ID: " << std::this_thread::get_id() << "\n";
    });
    
    // 提交带参数的任务
    pool.enqueue([](int a, int b) {
        std::cout << a << " + " << b << " = " << a + b << "\n";
    }, 10, 20);
    
    // 等待一段时间，让任务执行
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // shutdown 会等待所有任务完成
}

// 示例 2：带返回值的任务
void example2() {
    ThreadPool pool(4);
    
    // 提交返回值的任务
    auto future1 = pool.enqueue([]() -> int {
        return 42;
    });
    
    auto future2 = pool.enqueue([]() -> std::string {
        return "hello";
    });
    
    // get() 会阻塞直到结果就绪
    int result1 = future1.get();
    std::string result2 = future2.get();
    
    std::cout << result1 << " " << result2 << "\n";
}

// 示例 3：并行计算
void example3() {
    ThreadPool pool(4);
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<int> results(data.size());
    
    // 并行处理每个元素
    for (size_t i = 0; i < data.size(); ++i) {
        pool.enqueue([&data, &results, i]() {
            // 模拟计算
            results[i] = data[i] * data[i];  // 计算平方
        });
    }
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 打印结果
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << data[i] << "^2 = " << results[i] << "\n";
    }
}

// 示例 4：批量 HTTP 请求
void example4(HttpClient& client, ThreadPool& pool) {
    std::vector<std::string> urls = {
        "http://api1.example.com/data",
        "http://api2.example.com/data",
        "http://api3.example.com/data",
        "http://api4.example.com/data"
    };
    
    std::vector<std::future<HttpResponse>> futures;
    
    // 并行发起请求
    for (const auto& url : urls) {
        auto future = pool.enqueue([&client, &url]() {
            return client.get(url);
        });
        futures.push_back(std::move(future));
    }
    
    // 收集结果
    for (auto& future : futures) {
        auto response = future.get();
        if (response.success) {
            std::cout << "请求成功: " << response.body << "\n";
        }
    }
}

// 示例 5：生产者-消费者模式
void example5() {
    ThreadPool pool(4);
    
    // 生产者
    pool.enqueue([]() {
        for (int i = 0; i < 100; ++i) {
            // 生产任务
            pool.enqueue([i]() {
                std::cout << "处理任务 " << i << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
*/
