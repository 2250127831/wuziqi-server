/**
 * @file thread_pool.h
 * @brief 线程池封装
 * 
 * =====================================================
 * 什么是线程池？
 * =====================================================
 * 
 * 场景：如果有 1000 个任务，每个任务需要 100ms
 * 
 * ❌ 不用线程池：
 *   - 创建 1000 个线程 → 每个线程创建/销毁开销 10ms
 *   - 总时间 = 1000 × (100 + 10) = 110 秒
 *   - 线程切换开销巨大！
 * 
 * ✅ 用线程池（假设 4 个线程）：
 *   - 预创建 4 个线程
 *   - 1000 个任务放入队列
 *   - 4 个线程从队列取任务执行
 *   - 总时间 = 1000 ÷ 4 × 100 = 25 秒
 * 
 * =====================================================
 * 线程池的工作模式
 * =====================================================
 * 
 *   ┌─────────────────────────────────────────────────┐
 *   │              任务队列 tasks_                    │
 *   │  [task1] [task2] [task3] [task4] [task5] ...   │
 *   └─────────────────────────────────────────────────┘
 *                         ↑
 *                         │ notify_one()
 *                         ↓
 *   ┌───────────┐ ┌───────────┐ ┌───────────┐ ┌───────────┐
 *   │ 线程 #1  │ │ 线程 #2  │ │ 线程 #3  │ │ 线程 #4  │
 *   │ 执行 task1│ │ 执行 task2│ │ 等待中...│ │ 执行 task3│
 *   └───────────┘ └───────────┘ └───────────┘ └───────────┘
 *                         
 * 核心概念：
 * - 生产者：提交任务（enqueue）
 * - 消费者：工作线程从队列取任务执行
 * - 队列：缓冲生产者和消费者之间的任务
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <functional>          // std::function
#include <vector>              // std::vector
#include <queue>               // std::queue
#include <thread>              // std::thread
#include <mutex>               // std::mutex
#include <condition_variable>  // std::condition_variable
#include <atomic>              // std::atomic

/**
 * @class ThreadPool
 * @brief 线程池管理器
 * 
 * 使用 C++ 标准库的 thread + mutex + condition_variable 实现
 * 
 * 架构图：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │              ThreadPool                     │
 *   ├─────────────────────────────────────────────┤
 *   │                                             │
 *   │  ┌─ 任务队列 tasks_ ─┐                     │
 *   │  │  queue<function<void()>>               │
 *   │  │  [task1] [task2] [task3] ...           │
 *   │  └─────────────────────┘                   │
 *   │                                             │
 *   │  ┌─ 工作线程 workers_ ─┐                  │
 *   │  │  vector<thread>                            │
 *   │  │  [thread#1] [thread#2] ...               │
 *   │  └──────────────────────┘                  │
 *   │                                             │
 *   │  ┌─ 同步原语 ────────┐                     │
 *   │  │  mutex_: 保护任务队列                    │
 *   │  │  condition_: 任务通知                    │
 *   │  │  stop_: 停止标志                         │
 *   │  └─────────────────────┘                    │
 *   │                                             │
 *   │  ┌─ 统计信息 ────────┐                     │
 *   │  │  active_count_: 正在执行的任务数        │
 *   │  └─────────────────────┘                   │
 *   └─────────────────────────────────────────────┘
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数 - 创建工作线程
     * @param num_threads 线程数量
     * 
     * 创建 num_threads 个工作线程
     * 每个线程立即开始运行 workerThread()
     */
    explicit ThreadPool(size_t num_threads);
    
    /**
     * @brief 析构函数 - 停止线程池
     * 
     * 会等待所有任务执行完再退出
     */
    ~ThreadPool();

    /**
     * @brief 提交任务
     * @param task 要执行的任务（lambda/function）
     * 
     * 使用模板，支持任意可调用对象：
     * - lambda 表达式
     * - std::function
     * - 普通函数指针
     * - 成员函数指针
     * 
     * 示例：
     * @code
     *   pool.enqueue([]() { std::cout << "任务1\n"; });
     *   
     *   pool.enqueue([](int a, int b) { 
     *       return a + b; 
     *   }, 10, 20);
     * @endcode
     */
    template<typename F>
    void enqueue(F&& task) {
        {
            // 1. 获取锁，保护任务队列
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            // 2. 把任务放入队列
            // std::forward<F>(task) 是完美转发
            // 保持 task 的左值/右值属性
            tasks_.emplace(std::forward<F>(task));
        }
        // 锁在这里自动释放！
        
        // 3. 通知一个工作线程有新任务
        condition_.notify_one();
    }

    /**
     * @brief 提交任务（带返回值的版本）
     * @param task 要执行的任务
     * @param args 任务参数
     * @return std::future<返回值类型>
     * 
     * 使用 std::async 在线程池中执行任务
     * 可以获取任务的返回值
     * 
     * 示例：
     * @code
     *   auto future = pool.enqueue([]() -> int {
     *       return 42;
     *   });
     *   int result = future.get();  // 阻塞直到完成
     * @endcode
     */
    template<typename F, typename... Args>
    auto enqueue(F&& task, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        // 创建一个打包任务
        auto packaged_task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(task), std::forward<Args>(args)...)
        );
        
        // 包装成无返回值的任务
        std::function<void()> wrapper = [packaged_task]() {
            (*packaged_task)();
        };
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.emplace(wrapper);
        }
        
        condition_.notify_one();
        
        return packaged_task->get_future();
    }

    /**
     * @brief 停止线程池
     * 
     * 1. 设置 stop_ 标志为 true
     * 2. 通知所有线程
     * 3. 等待所有线程结束
     */
    void shutdown();

    /**
     * @brief 获取正在执行的任务数
     * @return 活跃任务数
     */
    size_t getActiveCount() const;

    /**
     * @brief 获取队列中的任务数
     * @return 等待执行的任务数
     */
    size_t getQueueSize() const;

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 工作线程的主循环
     * 
     * 每个工作线程都会执行这个函数：
     * 1. 获取锁
     * 2. 等待条件：有新任务 或 停止
     * 3. 取出一个任务
     * 4. 释放锁
     * 5. 执行任务
     * 
     * 循环直到收到停止信号且队列为空
     */
    void workerThread();

    // ==================== 成员变量 ====================
    
    size_t pool_size_;                          // 线程池大小（线程数）
    std::vector<std::thread> workers_;          // 工作线程列表
    
    std::queue<std::function<void()>> tasks_;   // 任务队列（FIFO）
    
    mutable std::mutex queue_mutex_;             // 保护任务队列的锁
    std::condition_variable condition_;          // 条件变量（任务通知）
    
    std::atomic<bool> stop_{false};             // 停止标志（原子操作）
    std::atomic<size_t> active_count_{0};       // 正在执行的任务数
};

// =====================================================
// 线程池使用场景
// =====================================================
/*
┌─────────────────────────────────────────────────────────────────┐
│                      什么时候用线程池？                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ✅ 适合的场景：                                                  │
│                                                                 │
│  1. 并行处理：                                                  │
│     - 图片处理：同时处理多张图片                                 │
│     - 数据计算：多线程同时计算                                   │
│                                                                 │
│  2. I/O 密集型任务：                                            │
│     - 网络请求：同时发起多个 HTTP 请求                           │
│     - 文件读写：同时读写多个文件                                 │
│                                                                 │
│  3. 异步任务：                                                  │
│     - 发送邮件：后台发送，不阻塞主线程                           │
│     - 记录日志：写入日志文件                                     │
│                                                                 │
│  ❌ 不适合的场景：                                                │
│                                                                 │
│  1. CPU 密集型任务过多：                                         │
│     - 如果线程数 = CPU 核心数，效果最好                          │
│     - 线程数过多会导致上下文切换开销                             │
│                                                                 │
│  2. 任务之间有强依赖：                                           │
│     - 任务 B 需要等任务 A 完成                                   │
│     - 这种情况下多线程可能没有帮助                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
*/

#endif // THREAD_POOL_H
