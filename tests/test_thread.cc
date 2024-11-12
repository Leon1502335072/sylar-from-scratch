/**
 * @file test_thread.cc
 * @brief 线程模块测试
 * @version 0.1
 * @date 2021-06-15
 */
#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int count = 0;
sylar::Mutex s_mutex;

void func1(void *arg) 
{
    SYLAR_LOG_INFO(g_logger) << "name:" << sylar::Thread::GetName() //静态成员方法 可以通过类名作用域直接调用
        << " this.name:" << sylar::Thread::GetThis()->getName()
        << " thread name:" << sylar::GetThreadName()
        << " id:" << sylar::GetThreadId()
        << " this.id:" << sylar::Thread::GetThis()->getId();
    SYLAR_LOG_INFO(g_logger) << "arg: " << *(int*)arg;
    for(int i = 0; i < 10000; i++) 
    {
        //初始化一个锁（是一个结构体，在构造里上锁 在析构里解锁）
        sylar::Mutex::Lock lock(s_mutex);
        ++count;
    }
}

int main(int argc, char *argv[]) 
{
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    std::vector<sylar::Thread::ptr> thrs;
    int arg = 123456;
    for(int i = 0; i < 3; i++) 
    {
        // 带参数的线程用std::bind进行参数绑定
        sylar::Thread::ptr thr(new sylar::Thread(std::bind(func1, &arg), "thread_" + std::to_string(i)));
        thrs.push_back(thr);
    }

    SYLAR_LOG_INFO(g_logger) << "count:" << count;
    
    for (int i = 0; i < 3; i++) {
        thrs[i]->join();
    }

    SYLAR_LOG_INFO(g_logger) << "count = " << count;
    return 0;
}

