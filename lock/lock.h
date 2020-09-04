#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        sem_init(&m_sem, 0, 0);
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait() //v操作，-1
    {
        return (sem_wait(&m_sem) == 0);
    }
    bool post() //p操作，+1
    {
        return (sem_post(&m_sem) == 0);
    }

private:
    sem_t m_sem;
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