/**
 * @file mutex.cc
 * @brief 信号量实现
 * @version 0.1
 * @date 2021-06-09
 */

#include "mutex.h"

namespace sylar {

Semaphore::Semaphore(uint32_t count) 
{   
    //第二个参数：0 表示线程同步 1表示进程同步 count是信号量的初始值
    if(sem_init(&m_semaphore, 0, count)) 
    {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() 
{
    sem_destroy(&m_semaphore);
}


void Semaphore::wait() 
{
    if(sem_wait(&m_semaphore)) 
    {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify() 
{
    if(sem_post(&m_semaphore)) 
    {
        throw std::logic_error("sem_post error");
    }
}

} // namespace sylar
