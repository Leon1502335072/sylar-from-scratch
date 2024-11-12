/**
 * @file fiber.cpp
 * @brief 协程实现
 * @version 0.1
 * @date 2021-06-15
 */

#include <atomic>
#include "fiber.h"
#include "config.h"
#include "log.h"
#include "macro.h"
#include "scheduler.h"

namespace sylar 
{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};

/// 全局静态变量，用于统计当前的协程数
static std::atomic<uint64_t> s_fiber_count{0};

/// 线程局部变量，当前线程正在运行的协程
static thread_local Fiber *t_fiber = nullptr;

/// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_thread_fiber = nullptr;

/// 协程栈大小，可通过配置文件获取，默认128k
static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

/**
 * @brief malloc栈内存分配器
 */
class MallocStackAllocator {
public:
    //分配size大小的栈内存，并返回首地址（指针）
    static void *Alloc(size_t size) { return malloc(size); }
    //释放那块内存
    static void Dealloc(void *vp, size_t size) { return free(vp); }
};

//指定别名 （协程栈内存分配器）
using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() 
{
    if (t_fiber) 
    {
        return t_fiber->getId();
    }
    return 0;
}

/**
 * @brief 构造函数
 * @attention ⽆参构造函数只⽤于创建线程的第⼀个协程，也就是线程主函数对应的协程，也就是主协程
 * 这个协程只能由GetThis()⽅法调⽤，所以定义成私有⽅法
**/
Fiber::Fiber() 
{
    SetThis(this);     //t_fiber = this; 
    m_state = RUNNING;

    // 获取当前上下文并保存到m_ctx中，此时创建的是主协程，因为没有栈和函数
    if (getcontext(&m_ctx)) 
    {
        SYLAR_ASSERT2(false, "getcontext");
    }

    ++s_fiber_count;
    m_id = s_fiber_id++; // 协程id从0开始，用完加1，所以主协程id为0，工作协程id从1开始

    //写debug日志
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() main id = " << m_id;
}

void Fiber::SetThis(Fiber *f) 
{ 
    t_fiber = f; 
}

/**
 * 获取当前协程，同时充当初始化当前线程主协程的作用，这个函数在使用协程之前要调用一下
 */
Fiber::ptr Fiber::GetThis() 
{
    //当前协程存在，返回一个指向该协程的智能指针（当前协程可能是主协程也可能是子协程）
    if (t_fiber) 
    {
        return t_fiber->shared_from_this();
    }

    //都没有则创建主协程  调了上面的无参构造
    Fiber::ptr main_fiber(new Fiber()); 

    //封装原来的assert函数，如果条件为假 写日志并退出
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;
    return t_fiber->shared_from_this();
}

/**
 * 带参数的构造函数用于创建其他协程，需要分配栈
 * cb：协程入口函数
 * stacksize：分配的栈的大小，默认是128k
 */
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler)
            :m_id(s_fiber_id++), 
             m_cb(cb), 
             m_runInScheduler(run_in_scheduler) 
{
    //每创建一个协程，协程数+1
    ++s_fiber_count;
    //设置协程栈的大小
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
    //申请协程的栈内存
    m_stack     = StackAllocator::Alloc(m_stacksize);
    
    //getcontext 初始化ucp结构体，将当前上下文保存在ucp中。成功时，返回0，错误返回-1，并设置errno
    if (getcontext(&m_ctx)) 
    {
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link          = nullptr; 
    m_ctx.uc_stack.ss_sp   = m_stack;       //设置该协程的栈
    m_ctx.uc_stack.ss_size = m_stacksize;   //栈大小

    //makecontext先让ucp和MainFunc函数绑定，此时MainFunc函数并未运行 也就说这个协程只是创建出来了 还没工作 resume之后才能运行
    //协程创建成功之后的默认状态是ready的
    //makecontext主要是用来给上下文设置入口函数的
    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() id = " << m_id;
}

/**
 * 线程的主协程析构时需要特殊处理，因为主协程没有分配栈和cb
 */
Fiber::~Fiber() 
{
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber() id = " << m_id;
    --s_fiber_count;
    if (m_stack) 
    {
        // 有栈，说明是子协程，需要确保子协程一定是结束状态
        // 后面的调度协程也有栈 只是栈的大小为0
        SYLAR_ASSERT(m_state == TERM);
        StackAllocator::Dealloc(m_stack, m_stacksize);
        SYLAR_LOG_DEBUG(g_logger) << "dealloc stack, id = " << m_id;
    } 
    else 
    {
        // 没有栈，说明是线程的主协程
        SYLAR_ASSERT(!m_cb);              // 主协程没有cb
        SYLAR_ASSERT(m_state == RUNNING); // 主协程一定是执行状态

        Fiber *cur = t_fiber; // 当前协程就是自己
        if (cur == this) 
        {
            SetThis(nullptr); //将当前协程地址置空
        }
    }
}

/**
 * 这里为了简化状态管理，强制只有TERM状态的协程才可以重置，但其实刚创建好但没执行过的协程也应该允许重置的
 */
void Fiber::reset(std::function<void()> cb) 
{
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM);
    m_cb = cb; //直接将传进来的cb赋值给m_cb

    //成功返回0，失败返回-1，并设置err_no
    if (getcontext(&m_ctx)) 
    {
        SYLAR_ASSERT2(false, "getcontext");
    }
    
    //复用以前的栈
    m_ctx.uc_link          = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = READY;
}

// 关于协程切换。子协程的resume操作一定是在主协程里执行的，主协程的resume操作一定是在子协程里执行的，这点完美和swapcontext匹配。
// swapcontext(ucontext_t *u1, ucontext_t *u2) 保存当前上下文到u1，同时激活u2



void Fiber::resume() 
{
    SYLAR_ASSERT(m_state != TERM && m_state != RUNNING);
    SetThis(this); //设置自己为当前运行的协程
    m_state = RUNNING;

    //工作协程和调度协程切换（调度协程不会加入调度器）
    if (m_runInScheduler) 
    {
        //调度协程停止，工作协程运行
        if (swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_ctx)) 
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    } 
    else //没参与调度的就是 调度协程，该协程与线程主协程切换
    {
        //从主协程切到调度协程
        if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) 
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

void Fiber::yield() 
{
    // 协程运行完之后会自动yield一次，用于回到主协程，此时状态已为结束状态
    //SYLAR_ASSERT的条件为假 则会写error日志 并退出
    SYLAR_ASSERT(m_state == RUNNING || m_state == TERM);
    //回到主协程
    if(!m_runInScheduler)
    {
        // 从调度协程回来的 shared_ptr的get()函数会返回一个该类型的普通指针
        SetThis(t_thread_fiber.get());
    }
    else
    {
        // 从工作协程回来的
        SetThis(sylar::Scheduler::GetMainFiber());
    }

    //该协程不是运行结束而是被挂起 则设置其状态为等待（ready）
    if (m_state != TERM) 
    {
        m_state = READY;
    }

    // 如果协程参与调度器调度，那么应该和调度器的主协程进行swap，而不是线程主协程
    if (m_runInScheduler) 
    {
        if (swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx))) 
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    } 
    else 
    {
        if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) 
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

/**
 * 这里没有处理协程函数出现异常的情况，同样是为了简化状态管理，并且个人认为协程的异常不应该由框架处理，应该由开发者自行处理
 */

//接下来是协程⼊⼝函数，sylar在⽤户传⼊的协程⼊⼝函数上进⾏了⼀次封装，这个封装类似于线程模块的对线程⼊
//⼝函数的封装。通过封装协程⼊⼝函数，可以实现协程在结束⾃动执⾏yield的操作。
void Fiber::MainFunc() 
{
    // GetThis()的shared_from_this()方法让引用计数加1
    Fiber::ptr cur = GetThis(); //得到指向该协程的指针
    SYLAR_ASSERT(cur);

    cur->m_cb();  //这里真正执行协程函数的入口
    cur->m_cb    = nullptr; //执行完函数指针赋空
    cur->m_state = TERM;  

    // 手动让t_fiber的引用计数减1
    //shared_ptr的reset( )函数的作用是将引用计数减1，停止对指针的共享，除非引用计数为0，否则不会发生删除操作。
    auto raw_ptr = cur.get(); //返回一个裸指针就是为了协程函数执行完调一次yiled
    cur.reset();

    raw_ptr->yield();  //协程结束时主动yield，以回到主线程
}

} // namespace sylar