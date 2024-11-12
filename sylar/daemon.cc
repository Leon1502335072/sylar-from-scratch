/**
 * @file daemon.cc
 * @brief 守护进程启动实现
 * @version 0.1
 * @date 2021-12-09
 */
#include "daemon.h"
#include "log.h"
#include "config.h"
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static sylar::ConfigVar<uint32_t>::ptr g_daemon_restart_interval
    = sylar::Config::Lookup("daemon.restart_interval", (uint32_t)5, "daemon restart interval");

std::string ProcessInfo::toString() const 
{
    std::stringstream ss;
    ss << "[ProcessInfo parent_id=" << parent_id
       << " main_id=" << main_id
       << " parent_start_time=" << sylar::Time2Str(parent_start_time)
       << " main_start_time=" << sylar::Time2Str(main_start_time)
       << " restart_count=" << restart_count << "]";
    return ss.str();
}

static int real_start(int argc, char** argv,
                     std::function<int(int argc, char** argv)> main_cb) 
{
    return main_cb(argc, argv);
}

static int real_daemon(int argc, char** argv,
                     std::function<int(int argc, char** argv)> main_cb) 
{
    // 1表示不会改变当前守护进程的工作目录，0表示关闭标准输入、标准输出和标准错误(子进程也不会输出到标准输出)
    daemon(1, 1);
    // 获取当前进程的pid
    ProcessInfoMgr::GetInstance()->parent_id = getpid();
    // 创建时间
    ProcessInfoMgr::GetInstance()->parent_start_time = time(0);
    while(true) 
    {
        pid_t pid = fork();
        if(pid == 0) //子进程返回
        {
            // 单例模式，表示当前只有一个进程信息结构体，里面包含父进程id，子进程id，各自创建的时间等等
            ProcessInfoMgr::GetInstance()->main_id = getpid();
            ProcessInfoMgr::GetInstance()->main_start_time  = time(0);
            SYLAR_LOG_INFO(g_logger) << "process start pid=" << getpid();
            return real_start(argc, argv, main_cb);
        } 
        else if(pid < 0) // 出错
        {
            SYLAR_LOG_ERROR(g_logger) << "fork fail return=" << pid
                << " errno=" << errno << " errstr=" << strerror(errno);
            return -1;
        } 
        else //父进程返回则一直监视子进程
        {
            int status = 0;
            waitpid(pid, &status, 0);
            if(status) 
            {
                SYLAR_LOG_ERROR(g_logger) << "child crash pid=" << pid
                    << " status=" << status;
                
            } 
            else 
            {
                SYLAR_LOG_INFO(g_logger) << "child finished pid=" << pid;
                break;
            }
            ProcessInfoMgr::GetInstance()->restart_count += 1;
            sleep(g_daemon_restart_interval->getValue());
        }
    }
    return 0;
}

int start_daemon(int argc, char** argv
                 ,std::function<int(int argc, char** argv)> main_cb
                 ,bool is_daemon) 
{
    // 如果不是守护进程就执行回调main_cb
    if(!is_daemon) 
    {
        return real_start(argc, argv, main_cb);
    }
    return real_daemon(argc, argv, main_cb);
}

}
