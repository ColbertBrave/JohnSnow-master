#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        sem_init(&m_sem, 0, 0);  // 初始化一个信号量
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait() //v操作，-1
    {
        // sem_wait阻塞当前线程直到信号量sem的值大于0，解除阻塞后将sem的值减一，表明公共资源经使用后减少
        return (sem_wait(&m_sem) == 0);  
    }
    bool post() //p操作，+1
    {
        // 释放信号量，让信号量的值加1
        return (sem_post(&m_sem) == 0);
    }

private:
    sem_t m_sem;    // 信号量的数据类型为sem_t，它本质上是一个长整型的数
};

class locker
{
private:
    pthread_mutex_t m_lock;     // 创建互斥锁，互斥锁是一个联合体

public:
    locker()
    {
        pthread_mutex_init(&m_lock, 0);  // 初始化互斥锁，此时处于未锁定状态
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_lock);     // 释放互斥锁
    }
    bool dolock()
    {
        return (pthread_mutex_lock(&m_lock) == 0);    // 锁定互斥锁，阻塞调用
    }
    bool unlock()
    {
        return (pthread_mutex_unlock(&m_lock) == 0);    // 解除互斥锁
    }
};
#endif