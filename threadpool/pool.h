#ifndef POOL_H
#define POOL_H

#include "../lock/lock.h"
#include <iostream>
#include <list>
#include <pthread.h>
#include <vector>
using namespace std;

/*
一个线程池类应该拥有的：
    线程池数组，用于初始化多个线程
    请求队列，将请求入队
    将请求入队的append方法
    线程执行的函数worker和run，各工作线程在run函数处进行竞争互斥锁
    监听线程和工作线程对请求队列进行操作时，需要加锁
    信号量，记录是否有请求要处理，没请求时，在wait阻塞。
    补充：
    corePoolSize                线程池的基本大小
    maximumPoolSize             线程池最大大小
    keepAliveTime               线程活动保持时间
    TimeUnit                    线程活动保持时间的单位：月/日/秒/毫秒
    runnableTaskQueue           任务队列
    ArrayBlockingQueue          一个基于数组结构的有界阻塞队列，此队列按 FIFO（先进先出）原则对元素进行排序。
    LinkedBlockingQueue         一个基于链表结构的无界阻塞队列（但可以指定队列大小，从而成为有界），此队列按FIFO （先进先出） 排序元素，吞吐量通常要高于ArrayBlockingQueue
    SynchronousQueue            一个不存储元素的阻塞队列。每个插入操作必须等到另一个线程调用移除操作，否则插入操作一直处于阻塞状态，吞吐量通常要高于LinkedBlockingQueue
    RejectedExecutionHandler    饱和策略：当队列和线程池都满了，说明线程池处于饱和状态，那么必须采取一种策略处理提交的新任务。这个策略默认情况下是AbortPolicy，表示无法处理新任务时抛出异常
    关闭线程池
    中断线程池
    销毁线程
    线程复用
    监控线程池:
                taskCount               线程池需要执行的任务数量
                completedTaskCount      线程池在运行过程中已完成的任务数量。小于或等于taskCount
                largestPoolSize         线程池曾经创建过的最大线程数量。通过这个数据可以知道线程池是否满过。如等于线程池的最大大小，则表示线程池曾经满了
                getPoolSize             线程池的线程数量
                getActiveCount          获取活动的线程数

*/
template <typename T>
class pool              // 线程池对象
{
private:
    list<T *> request_list;        // request list
    /*
        在linux的实现中pthread_t被定义为 "unsigned long int"
        在win中是一个结构体
    */
    vector<pthread_t> thread_list; // thread list
    locker m_lock;                 // lock for working thread
    sem m_sem;                     // sem for main thread and working thread
    int m_thread_num;
    int m_max_request;

private:
    static void *worker(void *arg); // 工作线程的执行函数，因此是静态函数，对象通过参数传入
    void run();

public: 
    pool(int thread_num = 10, int max_request = 100);
    ~pool();
    bool append(T *requset);        // 添加请求
};

template <typename T>
pool<T>::pool(int thread_num, int max_request): m_thread_num(thread_num), m_max_request(max_request)
/*
    线程构造函数: 根据线程数量和请求数量创建线程池
*/
{
    thread_list = vector<pthread_t>(thread_num, 0);     // 创建线程ID列表，初始化为0
    //cout << "create thread_list" << endl;
    //start working threads
    for (int i = 0; i < m_thread_num; ++i)
    {
        //cout << "creat thread:" << i <<"is:"<<thread_list[i]<< endl;
        /*
            创建POSIX线程，&thread_list[i]为创建的线程标识符指针，线程属性对象默认为NULL，线程运行函数起始地址为worker，运行函数的参数为this
        */
        if (pthread_create(&thread_list[i], NULL, worker, this) != 0)
        // 当线程创建不成功时报错
        {
            cout << "Some thing goes wrong when creating thread!" << endl;
        }

        if (pthread_detach(thread_list[i])!= 0)
        // pthread_detach()即主线程与子线程分离，子线程结束后，资源自动回收
        {
            cout << "Thread detach failed!" << endl;
        }
    }
    cout << "ThreadPool Created Successfully" << endl;
}

template <typename T>
pool<T>::~pool() {}

template <typename T>
bool pool<T>::append(T *requset)
{

    //cout << "want to get lock" << endl;
    m_lock.dolock();    // 加锁
    if (request_list.size() > m_max_request)
    // 请求数量超过最大数量限制
    {
        m_lock.unlock();
        //cout << "too many request_list" << endl;
        return false;
    }
    request_list.push_back(requset);
    m_lock.unlock();
    m_sem.post();
    //cout << "http request appended" << endl;
    return true;
}

template <typename T>
void* pool<T>::worker(void *arg)
{
    pool<T> *pool_ptr = static_cast<pool<T> *>(arg);
    pool_ptr->run();
    return pool_ptr;
}

template <typename T>
void pool<T>::run()
{
    while (1)
    {
        m_sem.wait();    // 信号量
        m_lock.dolock(); // 加锁，竞争条件
        //cout << "request num :  " << request_list.size() << endl;
        if (request_list.size() <= 0)
        {
            m_lock.unlock();
        }
        else
        {
            T* request = request_list.front();
            request_list.pop_front(); // 请求队列出队
            // cout << "some thread get the request" << endl;
            m_lock.unlock();
            if (!request)
            {
                continue;
            }
            request->process();    // ???
            // delete request;// 非常奇怪
            // request = nullptr;
        }
    }
}
#endif