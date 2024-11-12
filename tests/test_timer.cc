/**
 * @file test_timer.cc
 * @brief IO协程测试器定时器测试
 * @version 0.1
 * @date 2021-06-19
 */

#include "sylar/sylar.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static int timeout = 1000;
static sylar::Timer::ptr s_timer;

void timer_callback() 
{
    //SYLAR_LOG_INFO(g_logger) << "timer callback, timeout = " << timeout;
    timeout += 1000;
    SYLAR_LOG_INFO(g_logger) << "timer callback, timeout = " << timeout;
    if(timeout < 5000) 
    {
        s_timer->reset(timeout, true);
    } 
    else 
    {
        //输出的日志表明 取消定时器不会再执行定时器的回调
        s_timer->cancel();
    }
}

void test_timer() 
{
    sylar::IOManager iom;

    SYLAR_LOG_INFO(g_logger) << "add one";
    // 创建循环定时器并加入到定时器集合
    s_timer = iom.addTimer(1000, timer_callback, true);

    SYLAR_LOG_INFO(g_logger) << "add two";
    // 单次定时器
    iom.addTimer(500, []{
        SYLAR_LOG_INFO(g_logger) << "500ms timeout";
    });

    SYLAR_LOG_INFO(g_logger) << "add three";
    iom.addTimer(5000, [] {
        SYLAR_LOG_INFO(g_logger) << "5000ms timeout";
    });
}

int main(int argc, char *argv[]) 
{
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());
    SYLAR_LOG_INFO(g_logger) << "test begin!";
    
    test_timer();

    SYLAR_LOG_INFO(g_logger) << "test end!";

    return 0;
}