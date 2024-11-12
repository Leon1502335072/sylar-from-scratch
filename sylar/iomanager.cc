/**
 * @file iomanager.cc
 * @brief IO协程调度器实现
 * @version 0.1
 * @date 2021-06-16
 */

#include <unistd.h>    // for pipe()
#include <sys/epoll.h> // for epoll_xxx()
#include <fcntl.h>     // for fcntl()
#include "iomanager.h"
#include "log.h"
#include "macro.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

enum EpollCtlOp {};

static std::ostream &operator<<(std::ostream &os, const EpollCtlOp &op) 
{
    switch ((int)op) 
    {
    #define XX(ctl) \
        case ctl:   \
            return os << #ctl;
            XX(EPOLL_CTL_ADD);
            XX(EPOLL_CTL_MOD);
            XX(EPOLL_CTL_DEL);
    #undef XX
        default:
            return os << (int)op;
    }
}

static std::ostream &operator<<(std::ostream &os, EPOLL_EVENTS events) 
{
    if (!events) 
    {
        return os << "0";
    }
    bool first = true;
#define XX(E)          \
    if (events & E) {  \
        if (!first) {  \
            os << "|"; \
        }              \
        os << #E;      \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event) 
{
    switch (event) 
    {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            SYLAR_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetEventContext(EventContext &ctx) 
{
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

//triggerEvent只是把触发的事件的协程或者回调函数添加到任务队列等待调度协程的调度
void IOManager::FdContext::triggerEvent(IOManager::Event event) 
{
    // 待触发的事件必须已被注册过
    SYLAR_ASSERT(events & event);
    /**
     *  清除该事件，表示不再关注该事件了
     * 也就是说，注册的IO事件是一次性的，如果想持续关注某个socket fd的读写事件，那么每次触发事件之后都要重新添加
     */
    
    // 将fd_ctx的events上的该事件清除掉
    events = (Event)(events & ~event);
    // 调度对应的协程
    EventContext &ctx = getEventContext(event);
    if (ctx.cb) 
    {
        ctx.scheduler->schedule(ctx.cb);
    } 
    else 
    {
        ctx.scheduler->schedule(ctx.fiber);
    }
    resetEventContext(ctx);
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
                    : Scheduler(threads, use_caller, name) 
{
    //创建一个epoll对象
    m_epfd = epoll_create(5000);
    SYLAR_ASSERT(m_epfd > 0);
    //创建一个管道 m_tickleFds[0]读端 m_tickleFds[1]写端 成功返回0
    int rt = pipe(m_tickleFds);
    SYLAR_ASSERT(!rt);

    // 关注pipe读句柄的可读事件，用于tickle协程
    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events  = EPOLLIN | EPOLLET; //添加读事件和边沿触发模式
    event.data.fd = m_tickleFds[0];

    // 管道0端的fd添加非阻塞方式，配合边缘触发
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!rt);

    //将管道的读描述符加⼊epoll多路复⽤，如果管道可读，idle中的epoll_wait会返回
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);

    contextResize(32);
    // 这⾥直接开启了Schedluer的start函数，也就是说IOManager创建即可调度协程
    start();
}


/*
    接下来是IOManager的析构函数实现和stopping重载。对于IOManager的析构，⾸先要等Scheduler调度完所有的
    任务，然后再关闭epoll句柄和pipe句柄，然后释放所有的FdContext；对于stopping，IOManager在判断是否可
    退出时，还要加上所有IO事件都完成调度的条件
*/
IOManager::~IOManager() 
{
    //在析构里调stop方法，让所有的任务都会被完成才结束
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) 
    {
        if (m_fdContexts[i]) 
        {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) 
{
    m_fdContexts.resize(size);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) 
    {
        if (!m_fdContexts[i]) 
        {
            m_fdContexts[i]     = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

/**
* @brief 向传进来的fd上添加传进来的事件event
* @details fd描述符发⽣了event事件时执⾏cb函数
* @param[in] fd socket句柄
* @param[in] event 事件类型
* @param[in] cb 事件回调函数，如果为空，则默认把当前协程作为回调执⾏体
* @return 添加成功返回0,失败返回-1
 */
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) 
{
    // 找到fd对应的FdContext，如果不存在，那就分配一个
    FdContext *fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } 
    else 
    {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 同一个fd不允许重复添加相同的事件
    // 给正在操作的fd上锁，更小粒度的锁
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLIKELY(fd_ctx->events & event)) 
    {
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                                  << " event=" << (EPOLL_EVENTS)event
                                  << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有指针存储FdContext的位置
    // fd_ctx->events? 判断此时的fd上面是否有事件 有时候初始化的fd上面什么事件也没有 那么就是add 如果该fd上面已有别的事件 那么就是mod
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events   = EPOLLET | fd_ctx->events | event; //此时只是将事件添加到监听列表，fd_ctx内部还没添加之和事件
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent); // 成功返回0
    if (rt) 
    {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
                                  << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    // 待执行IO事件数加1
    ++m_pendingEventCount;

    // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    fd_ctx->events                     = (Event)(fd_ctx->events | event);
    //这里返回的就是要向这个fd 中添加的事件的上下文，因为fd_ctx内部已经生成了两种事件read和write的事件上下文 可能事件结构体还是空还没初始化
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event); 
    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) 
    {
        event_ctx.cb.swap(cb);
    } 
    else 
    {
        //没有该事件的回调函数 则将当前运行的协程对象赋值给他内部的协程
        event_ctx.fiber = Fiber::GetThis();
        SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::RUNNING, "state=" << event_ctx.fiber->getState());
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) 
{
    // 找到fd对应的FdContext ->找到文件描述符fd对应的文件描述符FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) 
    {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    // 给fd加锁
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    // 该fd上没有要删除的指定的event
    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) 
    {
        return false;
    }

    // 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 待执行事件数减1
    --m_pendingEventCount;
    // 重置该fd对应的event事件上下文
    fd_ctx->events                     = new_events;
    //这个event就是要从fd上删除的事件，既然要删除那么该事件上的上下文（调度器，fiber，cb）要清空
    //第一步得到要删除的事件的上下文：调度器，fiber，cb
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) 
{
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) 
    {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    // fd上没有该事件 返回false
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) 
    {
        return false;
    }

    // 删除事件
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 删除之前触发一次事件
    fd_ctx->triggerEvent(event);
    // 活跃事件数减1
    --m_pendingEventCount;
    return true;
}

bool IOManager::cancelAll(int fd) 
{
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) 
    {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    // fd上没有事件 返回fales
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!fd_ctx->events) 
    {
        return false;
    }

    // 删除全部事件
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 触发全部已注册的事件
    if (fd_ctx->events & READ) 
    {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) 
    {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager *IOManager::GetThis() 
{
    // 父转子
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

/**
 * 通知调度协程、也就是Scheduler::run()从idle中退出
 * Scheduler::run()每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle
 * 如果没有调度线程处理于idle状态，那也就没必要发通知了
 */
void IOManager::tickle() 
{
    SYLAR_LOG_DEBUG(g_logger) << "tickle";
    // 判断的是其他线程上有无正在运行的idle协程 此处的逻辑就是：如果任务队列还有任务且此时还有idle协程在运行，即别的线程的idle协程陷在
    // epoll_wait里，此时立即写管道让epoll_wait返回，如果没有idle协程在运行则说明其他线程都在运行工作协程或者是自己的调度协程，那也就
    // 没必要通知了 
    if(!hasIdleThreads()) 
    {
        return;
    }
    //有任务->向管道的写端写入一个字节用于触发epoll_wait 
    int rt = write(m_tickleFds[1], "T", 1);
    SYLAR_ASSERT(rt == 1);
}

bool IOManager::stopping() 
{
    uint64_t timeout = 0;
    return stopping(timeout);
}

bool IOManager::stopping(uint64_t &timeout) 
{
    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    // 增加定时器功能后，还应该保证没有剩余的定时器待触发
    // Scheduler::stopping() m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
    timeout = getNextTimer();
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

/**
 * 调度器无调度任务时会阻塞idle协程上，对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * IO事件对应的回调函数
 */
void IOManager::idle() 
{
    SYLAR_LOG_DEBUG(g_logger) << "idle";

    // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理
    const uint64_t MAX_EVNETS = 256;
    epoll_event *events       = new epoll_event[MAX_EVNETS]();
    // 方便events数组释放，使用智能指针（其实不使用这个智能指针，只是在离开idle的时候可以自动的释放掉这个数组）
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) { delete[] ptr; });

    while (true) 
    {
        // 获取下一个定时器的超时时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;
        if( SYLAR_UNLIKELY(stopping(next_timeout))) //调stopping同时会获得第一个定时器超时时间
        {
            SYLAR_LOG_DEBUG(g_logger) << "name=" << getName() << "idle stopping exit";

            // ☆☆☆☆☆☆☆☆☆☆☆

            // 定时器集合为空，IO事件做完，scheduler::stopping()为真 然后就从这个位置跳出循环，idle协程结束状态为term
            break;
        }

        // 阻塞在epoll_wait上，等待事件发生或定时器超时
        int rt = 0;
        do{
            // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时，避免定时器超时时间太大时，epoll_wait一直阻塞
            static const int MAX_TIMEOUT = 5000;
            if(next_timeout != ~0ull) //下一个超时时间不是最大值
            {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } 
            else 
            {
                next_timeout = MAX_TIMEOUT;
            }
            //在超时时间内获取有响应的事件 并将其存储在events数组中 
            //如果在请求的超时毫秒内没有文件描述符准备就绪，则返回零
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            if(rt < 0 && errno == EINTR) 
            {
                continue;
            } 
            else 
            {
                break;
            }
        } while(true);

        // 收集所有到时或者超时的定时器的回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) 
        {
            for(const auto &cb : cbs) 
            {
                //把所有超时的定时器的回调加入到任务队列
                schedule(cb);
            }
            cbs.clear();
        }
        
        // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        for (int i = 0; i < rt; ++i) 
        {
            //struct event-> events, data(data又是一个联合体，他的ptr指针保存着fd_ctx的指针)
            epoll_event &event = events[i];
            if (event.data.fd == m_tickleFds[0]) 
            {
                // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
                    ;
                continue;
            }

            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
             */ 
            if (event.events & (EPOLLERR | EPOLLHUP)) 
            {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) 
            {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) 
            {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) 
            {
                continue;
            }

            // 剔除已经发生的事件，将剩下的事件重新加入epoll_wait
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) 
            {
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                          << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                                          << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            // 处理已经发生的事件，也就是让调度器调度指定的函数或协程
            if (real_events & READ) 
            {
                fd_ctx->triggerEvent(READ);//这里的triggerEvent函数只是把对应的fiber重新加入调度，要执行的话还是要等到idle协程退出
                --m_pendingEventCount;
            }
            if (real_events & WRITE) 
            {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        /**
         * 一旦处理完所有的事件，idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有新任务要调度
         * 上面triggerEvent实际也只是把对应的fiber重新加入调度，要执行的话还要等idle协程退出！！！
         */ 
        Fiber::ptr cur = Fiber::GetThis(); //返回当前正在执行的协程，也就是idle协程 此时idle协程的引用计数+1
        auto raw_ptr   = cur.get(); //返回该idle协程的裸指针
        cur.reset(); //引用计数-1

        //当前idle协程让出执行权 调度协程被resume
        raw_ptr->yield();
    } // end while(true)
}

void IOManager::onTimerInsertedAtFront() 
{
    tickle();
}

} // end namespace sylar