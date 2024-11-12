/**
 * @file thread.cc
 * @brief 线程封装实现
 * @version 0.1
 * @date 2021-06-15
 */
#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar {

//要拿到当前线程 所以定义一个线程局部变量来拿到当前线程 通过GetThis方法返回（t_thread就指向当前的线程）
//即使不是通过线程类生成的线程即主线程 那么主线程的t_thread也是存在的，因为是全局的线程局部变量 主线程一产生 主线程的t_thead就存在了
static thread_local Thread *t_thread          = nullptr;
//当前线程名称
static thread_local std::string t_thread_name = "UNKNOW";

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread *Thread::GetThis() 
{
    return t_thread;
}

const std::string &Thread::GetName() 
{
    return t_thread_name;
}

void Thread::SetName(const std::string &name) 
{
    if (name.empty()) 
    {
        return;
    }
    if (t_thread) 
    {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

// 参数1：函数对象，参数2：线程名称
Thread::Thread(std::function<void()> cb, const std::string &name)
                : m_cb(cb), m_name(name) 
{  
    if (name.empty()) 
    {
        m_name = "UNKNOW";
    }
    //这里的run函数是在thread类内的 通过传参的方式将this指针传进去，在run里this->cb()执行
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) 
    {
        SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
                                  << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    //获取线程运行起来的信号量 如果为0 则会阻塞在这里
    //即当一个线程真正运行起来之后才会退出这个构造函数
    m_semaphore.wait();
}

// pthread_detach函数用于将一个线程标记为分离状态，
// 从而使得线程的资源在其终止时可以自动释放，而不需
// 要其他线程调用pthread_join函数来等待其终止。这样可以避免出现僵尸线程（zombie thread），从而简化了对线程资源的管理。
Thread::~Thread() 
{
    if (m_thread) 
    {
        pthread_detach(m_thread);
    }
}

void Thread::join() 
{
    if (m_thread) 
    {
        //即pthread_join()的作用可以这样理解：主线程等待子线程的终止。
        //也就是在子线程调用了pthread_join()方法后面的代码只有等到子线程结束了才能执行。
        //pthread_join()两个作用：1、主线程等待子线程结束 2、主线程回收子线程资源
        int rt = pthread_join(m_thread, nullptr);
        if (rt) 
        {
            SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
                                      << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

/*
    当一个线程绑定run为回调函数并且运行run时 此时才是真正跑到这个线程里面去了!!!!
*/
void *Thread::run(void *arg) 
{
    Thread *thread = (Thread *)arg;
    //到真正的线程里的时候 此时这个线程也会有自己的线程局部变量就是t_thrad 此时这个值就应该指向自己了
    t_thread       = thread;
    //名字也只是同理
    t_thread_name  = thread->m_name;
    //线程id也是同理
    thread->m_id   = sylar::GetThreadId();
    //给线程命名
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());
    //创建一个函数对象cb 通过swap方法让cb获得m_cb的控制权 此时m_cb为空
    std::function<void()> cb;
    cb.swap(thread->m_cb);

    thread->m_semaphore.notify();//释放一个信号量 表示该线程已经运行起来了 post()
    //执行cb
    cb();
    return 0;
}

} // namespace sylar
