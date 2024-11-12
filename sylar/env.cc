/**
 * @file env.cc
 * @brief 环境变量管理接口实现
 * @version 0.1
 * @date 2021-06-13
 * @todo 命令行参数解析应该用getopt系列接口实现，以支持选项合并和--开头的长选项
 */
#include "env.h"
#include "sylar/log.h"
#include <string.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include "config.h"
/*
    在Linux系统中，/proc 目录是一个虚拟文件系统（也称为进程文件系统），
    它包含了系统和进程的实时信息。这些信息不是存储在磁盘上的文件，而是
    在系统运行时动态生成的。
    /proc 目录中的文件和目录可以为我们提供有关正在运行的进程、系统内存、
    CPU状态等信息，这些信息对于系统管理员和开发者来说非常有用。例如，要
    查看所有正在运行的进程的信息，你可以查看 /proc 目录中的 PID 目录，
    其中 PID 是每个进程的数字标识符
*/

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

bool Env::init(int argc, char **argv) 
{
    char link[1024] = {0};
    char path[1024] = {0};
    // getpid（）返回调用进程的进程ID（PID）
    sprintf(link, "/proc/%d/exe", getpid()); 

    // readlink()会将参数link的符号链接内容存储到参数path的内存空间
    // 获取可执行程序的绝对路径 /Coroutines/sylar-from-scratch/bin/test_xxx  -> 存在path数组
    readlink(link, path, sizeof(path)); 

    // 当前正在运行的可执行文件的绝对路径 /Coroutines/sylar-from-scratch/bin/test_xxx
    m_exe = path;

    auto pos = m_exe.find_last_of("/"); // 返回最后一个 / 的位置
    m_cwd    = m_exe.substr(0, pos) + "/"; // m_cwd = /Coroutines/sylar-from-scratch/bin/

    m_program = argv[0]; // 可能是（ ./xxx ）
    // ./test_xxx -config /path/to/config -file xxxx -d 
    const char *now_key = nullptr;
    for (int i = 1; i < argc; ++i) 
    {
        if (argv[i][0] == '-') 
        {
            if (strlen(argv[i]) > 1) 
            {
                if (now_key) 
                {
                    add(now_key, "");
                }
                now_key = argv[i] + 1;
            } 
            else 
            {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                                          << " val=" << argv[i];
                return false;
            }
        } 
        else 
        {
            if (now_key) 
            {
                add(now_key, argv[i]);
                now_key = nullptr;
            } 
            else 
            {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                                          << " val=" << argv[i];
                return false;
            }
        }
    }
    // 最后一个参数key没有value
    if (now_key) 
    {
        add(now_key, "");
    }
    return true;
}

void Env::add(const std::string &key, const std::string &val) 
{
    RWMutexType::WriteLock lock(m_mutex);
    m_args[key] = val;
}

bool Env::has(const std::string &key) 
{
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

void Env::del(const std::string &key) 
{
    RWMutexType::WriteLock lock(m_mutex);
    m_args.erase(key);
}

std::string Env::get(const std::string &key, const std::string &default_value) 
{
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_value;
}

void Env::addHelp(const std::string &key, const std::string &desc) 
{
    // 先移除在添加，即使没有这个key 调removeHelp也不会出问题
    removeHelp(key);
    RWMutexType::WriteLock lock(m_mutex);
    m_helps.push_back(std::make_pair(key, desc));
}

void Env::removeHelp(const std::string &key) 
{
    RWMutexType::WriteLock lock(m_mutex);
    for (auto it = m_helps.begin(); it != m_helps.end();)      
    {
        if (it->first == key) 
        {
            // 需要注意的是vector的erase方法返回的是删除元素的下一个迭代器的位置
            it = m_helps.erase(it);
        } 
        else 
        {
            ++it;
        }
    }
}

void Env::printHelp() 
{
    RWMutexType::ReadLock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    // 打印所有的帮助提示符 setw(5)产生空格符 -> 在"-"前面产生4个空格符
    for (auto &i : m_helps) 
    {
        std::cout << std::setw(5) << "-" << i.first << " : " << i.second << std::endl;
    }
}

bool Env::setEnv(const std::string &key, const std::string &val) 
{
    return !setenv(key.c_str(), val.c_str(), 1);
}

std::string Env::getEnv(const std::string &key, const std::string &default_value) 
{
    const char *v = getenv(key.c_str());
    if (v == nullptr) 
    {
        return default_value;
    }
    return v;
}

std::string Env::getAbsolutePath(const std::string &path) const 
{
    if (path.empty()) 
    {
        return "/";
    }
    if (path[0] == '/') 
    {
        return path;
    }
    return m_cwd + path;
}

std::string Env::getAbsoluteWorkPath(const std::string& path) const 
{
    if(path.empty()) 
    {
        return "/";
    }
    if(path[0] == '/') 
    {
        return path;
    }
    static sylar::ConfigVar<std::string>::ptr g_server_work_path =
        sylar::Config::Lookup<std::string>("server.work_path");
    return g_server_work_path->getValue() + "/" + path;
}

std::string Env::getConfigPath() 
{
    return getAbsolutePath(get("c", "conf"));
}

} // namespace sylar
