/**
 * @file mutex.h
 * @brief 信号量，互斥锁，读写锁，范围锁模板，自旋锁，原子锁
 * @version 0.1
 * @date 2021-06-09
 */
#ifndef __SYLAR_MUTEX_H__
#define __SYLAR_MUTEX_H__

#include <thread>
#include <functional>
#include <memory>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <atomic>
#include <list>

#include "noncopyable.h"

namespace sylar {

/**
 * @brief 信号量
 */
class Semaphore : Noncopyable {
public:
    /**
     * @brief 构造函数
     * @param[in] count 信号量值的大小
     */
    Semaphore(uint32_t count = 0);

    /**
     * @brief 析构函数
     */
    ~Semaphore();

    /**
     * @brief 信号量-1
     */
    void wait();

    /**
     * @brief 信号量+1
     */
    void notify();
private:
    sem_t m_semaphore;
};

/**
 * @brief 局部锁的模板实现
 */
template<class T>
struct ScopedLockImpl {
public:
    /**
     * @brief 构造函数 在构造函数里直接加锁
     * @param[in] mutex Mutex
     */
    ScopedLockImpl(T& mutex):m_mutex(mutex) 
    {
        //m_mutex.lock();
        lock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ScopedLockImpl() 
    {
        unlock();
    }

    /**
     * @brief 加锁
     */
    void lock() 
    {
        if(!m_locked) 
        {
            m_mutex.lock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() 
    {
        if(m_locked) 
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// T类型的互斥量mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部读锁模板实现
 */
template<class T>
struct ReadScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    ReadScopedLockImpl(T& mutex):m_mutex(mutex)     
    {
        lock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ReadScopedLockImpl() 
    {
        unlock();
    }

    /**
     * @brief 上读锁
     */
    void lock() 
    {
        if(!m_locked) 
        {
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    /**
     * @brief 释放锁
     */
    void unlock() 
    {
        if(m_locked) 
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部写锁模板实现
 */
template<class T>
struct WriteScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    WriteScopedLockImpl(T& mutex):m_mutex(mutex) 
    {
        //m_mutex.wrlock();
        lock();
        m_locked = true;
    }

    /**
     * @brief 析构函数
     */
    ~WriteScopedLockImpl() 
    {
        unlock();
    }

    /**
     * @brief 上写锁
     */
    void lock() 
    {
        if(!m_locked) 
        {
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() 
    {
        if(m_locked) 
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// Mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 互斥量（也叫锁 但是这个锁只有两种状态 要么锁住 要么没锁 当多个线程可读时如果加这个互斥量会影响并发度）
 */
class Mutex : Noncopyable {
public:
    /// 局部锁 mutex::Lock lock;
    typedef ScopedLockImpl<Mutex> Lock;

    /**
     * @brief 构造函数（初始化互斥量/锁）
     */
    Mutex() 
    {
        pthread_mutex_init(&m_mutex, nullptr);
    }

    /**
     * @brief 析构函数（销毁互斥量资源或者说是锁资源）
     */
    ~Mutex() 
    {
        pthread_mutex_destroy(&m_mutex);
    }

    /**
     * @brief 加锁
     */
    void lock() 
    {
        pthread_mutex_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() 
    {
        pthread_mutex_unlock(&m_mutex);
    }
private:
    /// 互斥量（锁）
    pthread_mutex_t m_mutex;
};

/**
 * @brief 空锁(用于调试)
 */
class NullMutex : Noncopyable{
public:
    /// 局部锁
    typedef ScopedLockImpl<NullMutex> Lock;

    /**
     * @brief 构造函数
     */
    NullMutex() {}

    /**
     * @brief 析构函数
     */
    ~NullMutex() {}

    /**
     * @brief 加锁
     */
    void lock() {}

    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 读写互斥量 （进一步提高并发度 即多个线程读是允许的）
 */
class RWMutex : Noncopyable{
public:

    /// 局部读锁
    typedef ReadScopedLockImpl<RWMutex> ReadLock;

    /// 局部写锁
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    /**
     * @brief 构造函数
     */
    RWMutex() 
    {
        pthread_rwlock_init(&m_lock, nullptr);
    }
    
    /**
     * @brief 析构函数
     */
    ~RWMutex() 
    {
        pthread_rwlock_destroy(&m_lock);
    }

    /**
     * @brief 上读锁
     */
    void rdlock() 
    {
        pthread_rwlock_rdlock(&m_lock);
    }

    /**
     * @brief 上写锁
     */
    void wrlock() 
    {
        pthread_rwlock_wrlock(&m_lock);
    }

    /**
     * @brief 解锁
     */
    void unlock() 
    {
        pthread_rwlock_unlock(&m_lock);
    }
private:
    /// 读写锁
    pthread_rwlock_t m_lock;
};

/**
 * @brief 空读写锁(用于调试)
 */
class NullRWMutex : Noncopyable {
public:
    /// 局部读锁
    typedef ReadScopedLockImpl<NullMutex> ReadLock;
    /// 局部写锁
    typedef WriteScopedLockImpl<NullMutex> WriteLock;

    /**
     * @brief 构造函数
     */
    NullRWMutex() {}
    /**
     * @brief 析构函数
     */
    ~NullRWMutex() {}

    /**
     * @brief 上读锁
     */
    void rdlock() {}

    /**
     * @brief 上写锁
     */
    void wrlock() {}
    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 自旋锁
 */
class Spinlock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<Spinlock> Lock;

    /**
     * @brief 构造函数
     */
    Spinlock() 
    {
        pthread_spin_init(&m_mutex, 0);
    }

    /**
     * @brief 析构函数
     */
    ~Spinlock() 
    {
        pthread_spin_destroy(&m_mutex);
    }

    /**
     * @brief 上锁
     */
    void lock() 
    {
        pthread_spin_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() 
    {
        pthread_spin_unlock(&m_mutex);
    }
private:
    /// 自旋锁
    pthread_spinlock_t m_mutex;
};

/**
 * @brief 原子锁
 */
class CASLock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<CASLock> Lock;

    /**
     * @brief 构造函数
     */
    CASLock() 
    {
        m_mutex.clear();
    }

    /**
     * @brief 析构函数
     */
    ~CASLock() {
    }

    /**
     * @brief 上锁
     */
    void lock() 
    {
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }

    /**
     * @brief 解锁
     */
    void unlock() 
    {
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }
private:
    /// 原子状态
    volatile std::atomic_flag m_mutex;
};

} // namespace sylar

#endif // __SYLAR_MUTEX_H__
