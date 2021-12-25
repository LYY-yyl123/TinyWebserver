#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
//信号量相关类
class sem
{
public:
    //sem_init初始化一个未命名的信号量
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)//不允许多进程之间使用同一信号量，信号量初始值为0
        {
            throw std::exception();
        }
    }

    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)//初始化信号量为nums
        {
            throw std::exception();
        }
    }

    //信号量析构函数
    ~sem()
    {
        sem_destroy(&m_sem);//sem_destroy()用于销毁信号量
    }
    
    bool wait()
    {
        return sem_wait(&m_sem) == 0;//sem_wait()以原子方式将信号量减一，信号量为0时，sem_wait阻塞
    }
    bool post()
    {
        return sem_post(&m_sem) == 0;//sem_post()以原子方式将信号量加一，信号量大于1时唤醒调用sem_wait()的线程
    }

private:
    sem_t m_sem;
};

class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        pthread_mutex_lock(m_mutex);//pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        pthread_mutex_unlock(m_mutex);//pthread_mutex_lock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        pthread_mutex_lock(m_mutex);//pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        pthread_mutex_unlock(m_mutex);// pthread_mutex_unlock(&m_mutex)
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;//m_cond_wait()用于等待目标条件变量。该函数调用时需要传入mutex参数(加锁的互斥锁)，函数执行时，先把调用线程放入条件变量的请求队列，然后将互斥锁解锁，当函数成功
    }                                               //返回为0时，互斥锁会被再次锁上，也就是说函数内部会有一次解锁的加锁的操作

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
