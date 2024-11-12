/**
 * @file scheduler.cc
 * @brief 协程调度器实现
 * @version 0.1
 * @date 2021-06-15
 */
#include "scheduler.h"
#include "macro.h"
#include "hook.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 当前线程的调度器，同一个调度器下的所有线程共享同一个实例（每个线程可以通过这个拿到调度器的指针）
static thread_local Scheduler *t_scheduler = nullptr;

/// 当前线程的调度协程，每个线程都独有一份
static thread_local Fiber *t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name) 
{
    //用于判断threads是否合法（条件为假发生断言）
    SYLAR_ASSERT(threads > 0);

    m_useCaller = use_caller;
    m_name      = name;

    // 生成该调度器的线程加入到调度线程
    if (use_caller) 
    {
        --threads;
        //use_caller线程创建一个主协程
        sylar::Fiber::GetThis();
        
        //这个GetThis()和上面的不同，这个是获取调度器的指针，在构造函数退出之前是没有调度器的，所以会返回一个空指针
        //所以use_caller为false时 t_scheduler是为nullptr的
        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;

        /**
         * caller线程的主协程不会被线程的调度协程run进行调度，而且，线程的调度协程停止时，应该返回caller线程的主协程
         * 在user caller情况下，把caller线程的主协程暂时保存起来，等调度协程结束时，再resume caller协程
         */

        //use_caller线程中的调度协程 Fiber(cb, stacksize, isschedul) 此时use_caller线程的调度协程并没有跑起来，他是在stop方法中开始的
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));

        sylar::Thread::SetName(m_name);
        t_scheduler_fiber = m_rootFiber.get();
        //调度器所在的线程id
        m_rootThread      = sylar::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    } 
    else 
    {
        //调度器所在的线程id
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

//获取当前线程调度器指针
Scheduler* Scheduler::GetThis() 
{ 
    return t_scheduler; 
}

//是use_caller时返回的是调度协程，不是use_caller时返回的是主协程也是调度协程
Fiber* Scheduler::GetMainFiber() 
{ 
    return t_scheduler_fiber;
}

//设置当前线程的协程调度器
void Scheduler::setThis() 
{
    t_scheduler = this;
}

Scheduler::~Scheduler() 
{
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::~Scheduler()";
    SYLAR_ASSERT(m_stopping);
    if (GetThis() == this) //这里的this就是调度器指针
    {
        t_scheduler = nullptr;
    }
}

//协程调度器的start方法实现，这里主要初始化调度线程池，如果只使⽤caller线程进⾏调度，那这个⽅法啥也不做:

void Scheduler::start() 
{
    SYLAR_LOG_DEBUG(g_logger) << "start";
    // mutex -> MutexType 在mutex里ScopedLockImpl<Mutex>（这是一个模板结构体） -> Lock（结构体对象，构造里上锁，析构里解锁）
    MutexType::Lock lock(m_mutex);
    if (m_stopping) 
    {
        SYLAR_LOG_ERROR(g_logger) << "Scheduler is stopped";
        return;
    }
    // start方法开始前 线程池就应该是空的
    SYLAR_ASSERT(m_threads.empty());

    m_threads.resize(m_threadCount);
    for (size_t i = 0; i < m_threadCount; i++) 
    {
                               //Thread(cb, threadName)
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                      m_name + "_" + std::to_string(i)));
        //加入到线程id数组
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

bool Scheduler::stopping() 
{
    MutexType::Lock lock(m_mutex);
    //正在停止 and 任务队列为空 and 活跃的线程数为0
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle() 
{ 
    SYLAR_LOG_DEBUG(g_logger) << "ticlke"; 
}

// 当stopping为真 idle协程的state变为term
void Scheduler::idle() 
{
    SYLAR_LOG_DEBUG(g_logger) << "idle";
    while (!stopping()) 
    {
        sylar::Fiber::GetThis()->yield();
    }
}

/*
    当主线程也参与调度时即user_caller为true
    stop⽅法，在使⽤了caller线程的情况下，调度器依赖stop⽅法来执⾏caller线程的调度协程，如果
    调度器只使⽤了caller线程来调度，那调度器真正开始执⾏调度的位置就是这个stop⽅法。
*/

//线程池中的线程如果没事做的话应该做一个循环等待 等待的方法就在stop中做
void Scheduler::stop() 
{
    SYLAR_LOG_DEBUG(g_logger) << "stop";
    if (stopping()) 
    {
        return;
    }
    m_stopping = true;

    /// 如果use caller，那只能由caller线程发起stop
    if (m_useCaller) 
    {
        SYLAR_ASSERT(GetThis() == this);
    } 
    else // 当use_caller为false的时候 此时caller线程的t_scheduler=nullptr 所以GetThis()返回的是空
    {
        SYLAR_ASSERT(GetThis() != this);
    }

    //通知各个调度线程把任务队列的任务全部做完
    for (size_t i = 0; i < m_threadCount; i++) 
    {
        tickle();
    }

    //通知一下use_caller线程处理一下任务
    if (m_rootFiber) 
    {
        tickle();
    }

    /// 在use caller情况下，调度器协程结束时，应该返回caller协程
    if (m_rootFiber) 
    {
        m_rootFiber->resume();
        SYLAR_LOG_DEBUG(g_logger) << "m_rootFiber end";
    }

    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for (auto &i : thrs) 
    {
        i->join();//设置线程分离
    }
}
/**
 *  接下来是调度协程的实现，内部有⼀个while(true)循环，不停地从任务队列取任务并执⾏，由于Fiber类改造过，
    每个被调度器执⾏的协程在结束时都会回到调度协程，所以这⾥不⽤担⼼跑⻜问题，当任务队列为空时，代码会进
    idle协程，但idle协程啥也不做直接就yield了，状态还是READY状态，所以这⾥其实就是个忙等待，CPU占⽤率爆
    炸，只有当调度器检测到停⽌标志时，idle协程才会真正结束，调度协程也会检测到idle协程状态为TERM，并且随
    之退出整个调度协程。这⾥还可以看出⼀点，对于⼀个任务协程，只要其从resume中返回了，那不管它的状态是
    TERM还是READY，调度器都不会⾃动将其再次加⼊调度，因为前⾯说过，⼀个成熟的协程是要学会⾃我管理的
**/

//协程调度函数->调度协程
void Scheduler::run() 
{
    SYLAR_LOG_DEBUG(g_logger) << "run";
    //设置当前线程的hook状态
    set_hook_enable(true);
    
    //将调度器指针保存到每个线程中 t_scheduler = this;
    setThis();
    //当前线程是不是调度器所在的线程
    if (sylar::GetThreadId() != m_rootThread) 
    {
        //当前线程不是调度器所在的线程，则设置该线程正在运行的协程为当前线程的调度协程（也确实如此，因为当前线程正在运行run方法）
        t_scheduler_fiber = sylar::Fiber::GetThis().get();
    }

    //设置idle协程
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber; //如果任务传进来的是一个函数对象，则把这个函数对象包装进这个新的协程里

    ScheduleTask task;
    while (true) 
    {
        task.reset();
        bool tickle_me = false; // 是否tickle其他线程进行任务调度（处理任务）
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            // 遍历所有调度任务 （遍历任务队列list）
            while (it != m_tasks.end()) 
            {
                //it->thread是那个任务所指定的线程号，-1表示任一线程都可以执那个任务，不等于-1表示需要指定的线程来处理这个任务
                //该任务指定了某个线程来完成，且这个线程不是当前线程
                if (it->thread != -1 && it->thread != sylar::GetThreadId()) 
                {
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，然后跳过这个任务，继续下一个
                    ++it;
                    tickle_me = true;
                    continue;
                }

                // 找到一个未指定线程，或是指定了当前线程的任务
                SYLAR_ASSERT(it->fiber || it->cb);

                // if (it->fiber) {
                //     // 任务队列时的协程一定是READY状态，谁会把RUNNING或TERM状态的协程加入调度呢？
                //     SYLAR_ASSERT(it->fiber->getState() == Fiber::READY);
                // }

                // [BUG FIX]: hook IO相关的系统调用时，在检测到IO未就绪的情况下，会先添加对应的读写事件，再yield当前协程，等IO就绪后再resume当前协程
                // 多线程高并发情境下，有可能发生刚添加事件就被触发的情况，如果此时当前协程还未来得及yield，则这里就有可能出现协程状态仍为RUNNING的情况
                // 这里简单地跳过这种情况，以损失一点性能为代价，否则整个协程框架都要大改
                
                //该fiber正在运行状态
                if(it->fiber && it->fiber->getState() == Fiber::RUNNING) 
                {
                    ++it;
                    continue;
                }
                
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }
            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }

        if (tickle_me) 
        {
            tickle();
        }

        if (task.fiber) //协程对象
        {
            // resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成了，活跃线程数减一
            task.fiber->resume();
            --m_activeThreadCount;
            task.reset(); 
        } 
        else if (task.cb) //函数对象->重新把他包装成协程对象
        {   
            //cb_fiber是新创建的协程指针 所以cb_fiber的值为空
            if (cb_fiber) 
            {
                //设置该协程的入口函数为task.cb，其实就是将函数对象包装成了协程对象
                cb_fiber->reset(task.cb);
            } 
            else 
            {
                cb_fiber.reset(new Fiber(task.cb));
            }
            task.reset();
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        } 
        else 
        {
            // 进到这个分支情况一定是任务队列空了，调度idle协程即可
            if (idle_fiber->getState() == Fiber::TERM) 
            {
                // 如果调度器没有调度任务，那么idle协程会不停地resume/yield，不会结束，如果idle协程结束了，那一定是调度器停止了
                SYLAR_LOG_DEBUG(g_logger) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::run() exit";
}

} // end namespace sylar