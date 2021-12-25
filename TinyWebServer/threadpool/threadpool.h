#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

//将类成员变量声明为static，则为静态成员变量，与一般的成员变量不同，无论建立多少对象，都只有一个静态成员变量的拷贝，静态成员变量属于一个类，所有对象共享
//线程处理函数和运行函数设置为私有属性
template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    //静态成员函数可以直接访问静态成员变量，不能直接访问普通成员变量，但可以通过参数传递的方式访问
    //普通成员函数可以访问普通成员变量，也可以访问静态成员变量
    //静态成员函数没有this指针.非静态数据成员为对象单独维护，但静态成员函数为共享函数，无法区分是哪个对象，因此不能访问普通成员变量，也没有this指针
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number(里面存放的应该是线程ID)
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理(信号量)
    connection_pool *m_connPool;  //数据库连接池
    int m_actor_model;          //模型切换
    bool m_stop;                //是否结束线程标志(我加的)
};


//构造函数中创建线程池，pthread_create()中将类的对象作为参数传递给静态函数work()，在静态函数中引用这个对象，并调用其动态方法run()
//具体的，类对象传递时用this指针，传递给静态函数后，将其转换为线程池类，并调用私有成员函数run()。

template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 1000) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)//如果线程数或者请求队列的最大请求数小于等于0则抛出异常
        throw std::exception();
    m_threads = new pthread_t[m_thread_number]; //创建一个线程数组
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)//第一个参数时新创建的线程ID指向的内存单元
        {
            delete[] m_threads;
            throw std::exception();
        }
        //将工作线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i]))//设置为分离线程
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

//向请求队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)//当前请求队列的大小大于线程池最大的请求数，那么不能向请求队列添加请求
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;//这是什么类型的任务。读为0，写为1
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//信号量加1
    return true;
}


template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//worker()的参数实际是一个线程池
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

//工作线程从请求队列中取出某个任务进行处理，一直循环
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();//信号量为0阻塞。直到成功使信号量减1
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                if (request->read_once())//读请求
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())//写请求
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            //connectionRAII初始化时会获得一个mysql连接，析构时会将连接放回连接池
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
