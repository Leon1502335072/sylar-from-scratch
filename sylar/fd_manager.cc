/**
 * @file fd_manager.cc
 * @brief 文件句柄管理类实现
 * @details 只管理socket fd，记录fd是否为socket，用户是否设置非阻塞，系统是否设置非阻塞，send/recv超时时间
 *          提供FdManager单例和get/del方法，用于创建/获取/删除fd
 * @version 0.1
 * @date 2021-06-21
 */
#include "fd_manager.h"
#include "hook.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

// 对一个socketfd进行构造
FdCtx::FdCtx(int fd)
    :m_isInit(false)          //是否初始化
    ,m_isSocket(false)        //是否是socket
    ,m_sysNonblock(false)     //是否hook非阻塞
    ,m_userNonblock(false)    //是否用户主动设置了非阻塞
    ,m_isClosed(false)        //是否关闭
    ,m_fd(fd)                 //文件描述符
    ,m_recvTimeout(-1)        //读超时时间毫秒数
    ,m_sendTimeout(-1)        //写超时时间毫秒数
{      
    init();                   //初始化函数
}

FdCtx::~FdCtx() {}

bool FdCtx::init() 
{
    //如果已经初始化 返回真
    if(m_isInit) 
    {
        return true;
    }
    m_recvTimeout = -1;
    m_sendTimeout = -1;
    //文件信息结构体
    struct stat fd_stat;  
    //获取文件、设备、管道或套接字的文件属性 文件描述符fd是打开的，并将它们放在fd_stat中。
    if(-1 == fstat(m_fd, &fd_stat)) 
    {
        m_isInit = false;
        m_isSocket = false;
    } 
    else 
    {
        m_isInit = true;
        m_isSocket = S_ISSOCK(fd_stat.st_mode); 
        //st_mode文件对应的模式，文件，目录等
        //通过st_mode属性我们可以判断给定的文件是一个普通文件还是一个目录,连接等等.
        //S_ISSOCK用于判断文件是否是一个SOCKET文件.
    }

    if(m_isSocket) //是socket文件
    {
        //获取fd上的权限标记
        int flags = fcntl_f(m_fd, F_GETFL, 0);
        //该fd没有设置O_NONBLOCK属性 没有就添加非阻塞且不暴露给用户知道
        if(!(flags & O_NONBLOCK)) 
        {
            fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
        }
        m_sysNonblock = true;
    } 
    else //不是socket文件 则m_sysNonblock = false
    {
        m_sysNonblock = false;
    }
    //这里设置为false是因为在用户调socket函数时会紧接着把这个fd加入到集合，此时用户还没来得及调fcntl修改fd的非阻塞属性
    //所以直接赋值false，后面如果用户调了fcntl并设置了非阻塞 那么会修改这个值为true
    m_userNonblock = false; 
    m_isClosed = false;
    return m_isInit;
}

void FdCtx::setTimeout(int type, uint64_t v) 
{
    if(type == SO_RCVTIMEO) 
    {   
        //读超时设置
        m_recvTimeout = v;
    } 
    else 
    { 
        //写超时设置
        m_sendTimeout = v;
    }
}

uint64_t FdCtx::getTimeout(int type) 
{
    if(type == SO_RCVTIMEO) 
    {
        return m_recvTimeout;
    } 
    else 
    {
        return m_sendTimeout;
    }
}

//fd管理类FdManager的构造，初始化了一个64大小的vector用来存储fd
FdManager::FdManager() 
{
    m_datas.resize(64);
}

FdCtx::ptr FdManager::get(int fd, bool auto_create) 
{
    //在FdManager的vector中是将fd作为下标索引来查找的 O(1)
    if(fd == -1) 
    {
        return nullptr;
    }

    //访问这个vector加读锁
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_datas.size() <= fd) 
    {
        //集合中没有 而且又不自动创建 所以返回nullptr
        if(auto_create == false) 
        {
            return nullptr;
        }
    } 
    else // ((int)m_datas.size()>fd)
    {
        //数组中有 或者是非自动创建
        if(m_datas[fd] || !auto_create) 
        {
            return m_datas[fd];
        }
    }
    lock.unlock();

    //数组中没有这个fd 而且是自动创建
    //插入新元素 vector加写锁
    RWMutexType::WriteLock lock2(m_mutex);
    FdCtx::ptr ctx(new FdCtx(fd));
    if(fd >= (int)m_datas.size()) 
    {
        m_datas.resize(fd * 1.5); //空间不够 扩容
    }
    m_datas[fd] = ctx;
    return ctx;
}

void FdManager::del(int fd) 
{
    //删除一个fd 加写锁
    RWMutexType::WriteLock lock(m_mutex);
    if((int)m_datas.size() <= fd) 
    {
        return;
    }
    m_datas[fd].reset(); //释放指针
}

}
