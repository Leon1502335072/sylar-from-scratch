#include "hook.h"
#include <dlfcn.h>

#include "config.h"
#include "log.h"
#include "fiber.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "macro.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
namespace sylar {

static sylar::ConfigVar<int>::ptr g_tcp_connect_timeout =
    sylar::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

//当前线程是否启⽤hook
static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)

void hook_init() 
{
    static bool is_inited = false;
    if(is_inited) //已经初始化过了 直接返回
    {
        return;
    }
//sleep_f = (sleep_fun)dlsym(RTLD_NEXT, sleep); RTLD_NEXT 表示返回第一个匹配到的sleep函数 后面即使有也不会被替换
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX
}

static uint64_t s_connect_timeout = -1;

//hook初始化结构体
struct _HookIniter 
{
    _HookIniter() 
    {
        //hook_init() 放在⼀个静态对象的构造函数中调⽤，这表示在main函数运⾏之前就会获取各个符号的地址并保存在全局变量中。
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue(); //5s
        //添加变量的监听函数，功能是设置新的连接超时时间
        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value)
        {
                SYLAR_LOG_INFO(g_logger) << "tcp connect timeout changed from "
                                         << old_value << " to " << new_value;
                s_connect_timeout = new_value;
        });
    }
};

//声明一个静态的全局变量 在进入main函数之前会先把它创建出来 也就是在main函数之前进行了初始化的构造 执行了hook_init方法
//这样就实现了我们的目的，在程序进来之前就实现了将他的hook函数和我们声明的变量关联起来
static _HookIniter s_hook_initer;

//当前线程是否hook
bool is_hook_enable() 
{
    return t_hook_enable;
}

void set_hook_enable(bool flag) 
{
    t_hook_enable = flag;
}

}

struct timer_info 
{
    int cancelled = 0;
};

template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
        uint32_t event, int timeout_so, Args&&... args) 
{   //没有启用hook
    if(!sylar::t_hook_enable) 
    {
        //返回原来老接口 std::forward<Args>(args)... -> 展开参数args
        return fun(fd, std::forward<Args>(args)...);
    }

    //GetInstance 返回单例的指针 这里就是fdmgr的指针 得到fd的fd_ctx
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    //不存在就认为不是socketfd 就按照他原来老的接口走
    if(!ctx) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }

    if(ctx->isClose()) 
    {
        errno = EBADF;
        return -1;
    }

    //不是socket 或则用户已主动设置非阻塞 返回老接口
    if(!ctx->isSocket() || ctx->getUserNonblock()) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }
    
    //是socket_fd 且用户没有主动设置非阻塞 走下面的流程，但其实在初始化fd的时候我们已经给这个fd添加了非阻塞属性 只是用户不知道而已

    //获取超时时间
    uint64_t to = ctx->getTimeout(timeout_so);
    //创建一个超时的条件
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
    //此时fd暗地里已添加O_NONBLOCK
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    while(n == -1 && errno == EINTR) //EINTR 中断
    {
        n = fun(fd, std::forward<Args>(args)...);
    }
    //不成功肯定会返回-1 且是再试一次EAGAIN 即说明读缓冲区此时还没有数据
    if(n == -1 && errno == EAGAIN) 
    {
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        sylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo); //条件定时器的条件

        //1、添加条件定时器
        if(to != (uint64_t)-1) 
        {
            timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                //winfo是weak_ptr 要通过lock来拿到winfo所指的对象 然后进行访问
                auto t = winfo.lock();
                if(!t || t->cancelled) //条件t不存在 或者t已取消
                {
                    return;
                }
                t->cancelled = ETIMEDOUT; //这里是设置超时标志
                iom->cancelEvent(fd, (sylar::IOManager::Event)(event));
            }, winfo);
        }

        //2、向fd添加事件 没有传回调 默认将当前协程最为处理对象
        int rt = iom->addEvent(fd, (sylar::IOManager::Event)(event));
        if(SYLAR_UNLIKELY(rt)) 
        {
            SYLAR_LOG_ERROR(g_logger) << hook_fun_name << " addEvent("
                << fd << ", " << event << ")";
            if(timer) 
            {
                timer->cancel();
            }
            return -1;
        } 
        else 
        {
            sylar::Fiber::GetThis()->yield();           
            // 从这点有两种情况可以使协程resume：1、确实可以进行io操作了，2、条件定时器到时执行了该定时器的回调函数
            if(timer) 
            {
                timer->cancel();
            }
            if(tinfo->cancelled) 
            {
                errno = tinfo->cancelled;
                return -1;
            }
            goto retry;
        }
    }
    
    return n;
}


extern "C" {
//sleep_fun sleep_f = nullptr; 初始化为空(头文件中已经声明过了)
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

unsigned int sleep(unsigned int seconds) 
{
    //当前线程没有启用hook，直接返回老的接口
    if(!sylar::t_hook_enable) 
    {
        return sleep_f(seconds);
    }
    //得到当前线程正在执行的协程
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    //得到当前的IOManager
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    //创建并将定时器添加到集合，回调函数是就是前面的schedule()，就是把该协程重新加到任务队列等待调度
    //bind中前面一段就是类型转换 void(*)(Fiber::ptr, int)&schedule
    iom->addTimer(seconds * 1000, std::bind((void(sylar::Scheduler::*)
                 (sylar::Fiber::ptr, int ))&sylar::IOManager::schedule, iom, fiber, -1));
    //让出执行权
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int usleep(useconds_t usec) 
{
    if(!sylar::t_hook_enable) 
    {
        return usleep_f(usec);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(usec / 1000, std::bind((void(sylar::Scheduler::*)
            (sylar::Fiber::ptr, int ))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) 
{
    if(!sylar::t_hook_enable) 
    {
        return nanosleep_f(req, rem);
    }

    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 /1000;
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(timeout_ms, std::bind((void(sylar::Scheduler::*)
            (sylar::Fiber::ptr, int ))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int socket(int domain, int type, int protocol) 
{
    if(!sylar::t_hook_enable) 
    {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if(fd == -1) 
    {
        return fd;
    }
    sylar::FdMgr::GetInstance()->get(fd, true);
    return fd;
}

int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) 
{
    if(!sylar::t_hook_enable) 
    {
        return connect_f(fd, addr, addrlen);
    }
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    // 该socketfd的上下文不存在 或者该fd已关闭
    if(!ctx || ctx->isClose()) 
    {
        errno = EBADF;
        return -1;
    }
    //如果不为套接字，则调⽤系统的connect函数并返回
    if(!ctx->isSocket()) 
    {
        return connect_f(fd, addr, addrlen);
    }
    //判断fd是否被显式设置为了⾮阻塞模式，如果是则调⽤系统的connect函数并返回。
    if(ctx->getUserNonblock()) 
    {
        return connect_f(fd, addr, addrlen);
    }

    //调⽤系统的connect函数成功返回0，失败返回-1，
    //由于套接字是⾮阻塞的，如果对方服务器没准备好，这⾥会直接返回-1，且errno=EINPROGRESS
    int n = connect_f(fd, addr, addrlen);
    if(n == 0) 
    {
        return 0;
    } 
    else if(n != -1 || errno != EINPROGRESS) 
    {                                        
        return n;
    }
    //EINPROGRESS表示socket为非阻塞套接字，已建立连接请求但没有立即完成，等待对方服务器准备好后（即掉accept函数）就可以建立连接。
    //那么何时才可以知道已经建立连接了？此时只需要给fd注册一个写事件，当fd可写，就表示已经建立连接。

    //此时线程不能阻塞在连接的协程上，要设置一个条件定时器，然后去调度别的协程进行工作
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    sylar::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    //如果超时参数有效，则添加⼀个条件定时器，在定时时间到后通过t->cancelled设置超时标志并触发⼀次WRITE事件
    if(timeout_ms != (uint64_t)-1) 
    {
        //添加一个条件定时器
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
                auto t = winfo.lock();
                if(!t || t->cancelled) 
                {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                iom->cancelEvent(fd, sylar::IOManager::WRITE); //在下面添加触发事件时没有传回调和协程，就是直接默认把当前协程对象传给写事件的上下文
            }, winfo);
    }

    //给fd添加一个写事件，写事件的任务协程就是当前协程
    int rt = iom->addEvent(fd, sylar::IOManager::WRITE);//后面还有第三个参数 不传就默认是当前协程
    if(rt == 0) 
    {
        sylar::Fiber::GetThis()->yield();
        //两种情况会使得调度协程切回到这个任务协程
        //1、就是添加的事件有响应，正常处理就行了
        //2、条件定时器到时，执行条件定时器的回调-> 把条件设置为超时，同时取消这个fd上面对应的事件，sylar的处理是在取消事件后再触发一次这个事件
        //   即schedule(fiber), 这个fiber就是当前运行到此处的fiber 后面调度协程切到这个fiber上 刚好从这个位置开始运行
        if(timer) 
        {
            timer->cancel();
        }
        if(tinfo->cancelled) 
        {
            errno = tinfo->cancelled;
            return -1;
        }
    } 
    else 
    {
        if(timer) 
        {
            timer->cancel();
        }
        SYLAR_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    //这部分主要是调用失败时获取一个错误号，如果成功直接返回0
    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) 
    {
        return -1;
    }
    if(!error) 
    {
        return 0;
    } 
    else 
    {
        errno = error;
        return -1;
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) 
{
    return connect_with_timeout(sockfd, addr, addrlen, sylar::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) 
{
    int fd = do_io(s, accept_f, "accept", sylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
    if(fd >= 0) 
    {
        sylar::FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_f, "read", sylar::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", sylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_f, "sendmsg", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd) 
{
    if(!sylar::t_hook_enable) 
    {
        return close_f(fd);
    }

    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(ctx) 
    {
        auto iom = sylar::IOManager::GetThis();
        if(iom) 
        {
            iom->cancelAll(fd);
        }
        sylar::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);
}

int fcntl(int fd, int cmd, ... /* arg */ ) 
{
    va_list va;
    va_start(va, cmd);
    switch(cmd) 
    {
        //参数F_SETFL表示给socket_fd添加一些权限
        case F_SETFL:
            {
                int arg = va_arg(va, int); // 获取下一个参数 fcntl函数最多有3个参数，所以下面直接end了
                va_end(va);
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()) 
                {
                    return fcntl_f(fd, cmd, arg);
                }
                // 给参数m_userNonblock（用户自己是否设置了非阻塞）赋值 如果用户自己设置了非阻塞 则m_userNonblock为true
                ctx->setUserNonblock(arg & O_NONBLOCK);
                // 其实就是判断所在线程有没有开启hook
                if(ctx->getSysNonblock()) 
                {
                    arg |= O_NONBLOCK;
                } 
                else 
                {
                    arg &= ~O_NONBLOCK;
                }
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETFL:
            {
                //F_GETFL不需要参数 直接结束参数列表
                va_end(va);
                int arg = fcntl_f(fd, cmd);
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()) 
                {
                    return arg;
                }
                if(ctx->getUserNonblock()) 
                {
                    return arg | O_NONBLOCK;
                } 
                else 
                {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
            break;
        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }
}

int ioctl(int d, unsigned long int request, ...) 
{
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if(FIONBIO == request) 
    {
        bool user_nonblock = !!*(int*)arg;
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(d);
        if(!ctx || ctx->isClose() || !ctx->isSocket()) 
        {
            return ioctl_f(d, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(d, request, arg);
}

//setsockopt、getsockopt就是专门用来读取和设置socket文件描述符属性的方法

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) 
{
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) 
{
    if(!sylar::t_hook_enable) 
    {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    // 参数level是被设置的选项的级别，如果想要在套接字级别上设置选项，就必须把level设置为SOL_SOCKET
    if(level == SOL_SOCKET) 
    {
        // SO_RCVTIMEO接收超时时间 SO_SNDTIMEO发送超时时间 超时时间在fd里面
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) 
        {
            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx) 
            {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}
