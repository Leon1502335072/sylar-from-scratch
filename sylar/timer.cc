#include "timer.h"
#include "util.h"
#include "macro.h"

namespace sylar {
//按超时时间从小到大排序
bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const 
{
    if(!lhs && !rhs) 
    {
        return false;
    }
    if(!lhs) 
    {
        return true;
    }
    if(!rhs) 
    {
        return false;
    }
    if(lhs->m_next < rhs->m_next) 
    {
        return true;
    }
    if(lhs->m_next > rhs->m_next) 
    {
        return false;
    }
    // 时间相同 地址较小的那个排在前面
    return lhs.get() < rhs.get();
}


Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
            : m_recurring(recurring), m_ms(ms), m_cb(cb), m_manager(manager)     
{
    //当前时间(毫秒级) + 超时时间
    m_next = sylar::GetElapsedMS() + m_ms;
}

// 该构造只会传入一个到时的时间，其他属性都没赋值
Timer::Timer(uint64_t next) :m_next(next)  {}

//取消定时器且不会执行定时器上的回调函数
bool Timer::cancel() 
{
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) 
    {
        //回调赋空
        m_cb = nullptr;
        //在set集合中找这个定时器
        auto it = m_manager->m_timers.find(shared_from_this());
        //找到删除
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

//刷新定时器 就是将当前时间+间隔时间m_ms，再加入定时器集合
bool Timer::refresh() 
{
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) 
    {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    //该定时器不在集合里 返回false
    if(it == m_manager->m_timers.end()) 
    {
        return false;
    }
    //先删除在插入
    m_manager->m_timers.erase(it);
    m_next = sylar::GetElapsedMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

//重置定时器
bool Timer::reset(uint64_t ms, bool from_now) 
{
    if(ms == m_ms && !from_now) 
    {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) 
    {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    //不在集定时器合里 返回false
    if(it == m_manager->m_timers.end()) 
    {
        return false;
    }
    //在集合里 先删，
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if(from_now) //如果是从当前开始算
    {
        start = sylar::GetElapsedMS();
    } 
    else //从原来的开始时间
    {
        start = m_next - m_ms;
    }
    m_ms = ms;
    m_next = start + m_ms;
    m_manager->addTimer(shared_from_this(), lock);
    return true;

}

//TimerManager的构造就是获取当前时间保存在m_previouseTime
TimerManager::TimerManager() 
{
    m_previouseTime = sylar::GetElapsedMS();
}

TimerManager::~TimerManager() {}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) 
{                         
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    //外部的add是增加一个定时器，此处的add是将定时器添加到manager
    addTimer(timer, lock);
    return timer;
}

//辅助函数 查看传进来的条件是否还有效
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) 
{
    //访问weak_ptr指向的内存std::shared_ptr sptr = wptr.lock();
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) //条件存在 执行cb
    {
        cb();
    }
}

// 条件定时器的回调先检查条件是否有效，再执行传进来的cb
Timer::ptr TimerManager::addConditionTimer(uint64_t ms, 
                                           std::function<void()> cb,
                                           std::weak_ptr<void> weak_cond,
                                           bool recurring ) 
{
    //给定时器ms添加一个执行函数OnTimer，OnTimer自己绑定weak_cond和cb两个参数
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

// 返回定时器集合中第一个定时器到时的时间间隔
uint64_t TimerManager::getNextTimer() 
{
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    if(m_timers.empty()) 
    {
        //没有定时任务 返回最大值（0取反就是最大）
        return ~0ull;
    }
    //取集合中的第一个元素（即最快要超时的定时器）
    const Timer::ptr& next = *m_timers.begin();
    //获取当前的时间
    uint64_t now_ms = sylar::GetElapsedMS();
    //如果当前时间大于等于next定时器的时间说明由于一些原因没有在定时器到时时执行对应的cb以及删除对用的定时器
    if(now_ms >= next->m_next) 
    {
        return 0;
    } 
    else  //否则定时器next还未超时，计算等待的时间
    {
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs) 
{
    //拿到当前时间
    uint64_t now_ms = sylar::GetElapsedMS();
    //已超时的定时器数组
    std::vector<Timer::ptr> expired;
    { //这点加大括号的原因就是制造出一个作用域，除了这个作用域锁自动释放
        RWMutexType::ReadLock lock(m_mutex);
        if(m_timers.empty()) 
        {
            return;
        }
    }
    RWMutexType::WriteLock lock(m_mutex);
    if(m_timers.empty()) 
    {
        return;
    }
    bool rollover = false;
    if(SYLAR_UNLIKELY(detectClockRollover(now_ms))) 
    {
        // 使用clock_gettime(CLOCK_MONOTONIC_RAW)，应该不可能出现时间回退的问题
        rollover = true;
    }
    //系统时间未被修改且第一个都没超时 自然后面都没超时 返回空列表
    if(!rollover && ((*m_timers.begin())->m_next > now_ms)) 
    {
        return;
    }

    //根据当前时间创建一个定时器，入口函数为nullptr 主要是用来比较用的 不会插入到集合中
    Timer::ptr now_timer(new Timer(now_ms));
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer); //返回大于或等于now_timer的第一个元素位置（迭代器）
    while(it != m_timers.end() && (*it)->m_next == now_ms) 
    {
        ++it;
    }
    //将所有过期的定时器都放到vector里
    expired.insert(expired.begin(), m_timers.begin(), it);
    //集合中删除这些定时器
    m_timers.erase(m_timers.begin(), it);

    //扩展cbs数组的大小，将超时的定时器的回调函数对象放进数组
    //同时判断这个定时器是否会循环，如果是则需要重新设置他的精确执行时间 然后再添加到定时器管理器
    cbs.reserve(expired.size());
    for(auto& timer : expired) 
    {
        cbs.push_back(timer->m_cb);
        //如果是循环定时器，再把它放回去
        if(timer->m_recurring) 
        {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } 
        else 
        {
            timer->m_cb = nullptr;
        }
    }
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) 
{
    //两步操作 插入并获取插入的位置
    //set 的insert操作成功的话返回一个pair类型的值，first->插入位置，second->是否插入成功
    auto it = m_timers.insert(val).first;
    bool at_front = (it == m_timers.begin()) && !m_tickled; // m_tickled 在 getNextTimer()刷新 
    if(at_front) 
    {
        m_tickled = true;
    }
    lock.unlock();

    //如果插在第一个位置
    if(at_front) 
    {
        // 纯虚函数 在iomanager中实现，就是调了一下tickle函数
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) 
{
    bool rollover = false;
    if(now_ms < m_previouseTime && now_ms < (m_previouseTime - 60 * 60 * 1000)) 
    {
            
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

//有返回真
bool TimerManager::hasTimer() 
{
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}
