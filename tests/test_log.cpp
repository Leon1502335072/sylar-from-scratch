/**
 * @file test_log.cpp
 * @brief 日志类测试
 * @version 0.1
 * @date 2021-06-10
 */

#include "sylar/sylar.h"

#include <unistd.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT(); // 默认INFO级别

int main(int argc, char *argv[]) 
{
    // 通过argc argv获得正在运行的文件的一些信息，比如文件名，路径...
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    // 加载log.yml文件
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    SYLAR_LOG_FATAL(g_logger) << "fatal msg";
    SYLAR_LOG_ERROR(g_logger) << "err msg";
    SYLAR_LOG_INFO(g_logger) << "info msg";
    SYLAR_LOG_DEBUG(g_logger) << "debug msg"; //默认INFO，DEBUG不会输出

    SYLAR_LOG_FMT_FATAL(g_logger, "fatal %s:%d", __FILE__, __LINE__);
    SYLAR_LOG_FMT_ERROR(g_logger, "err %s:%d", __FILE__, __LINE__);
    SYLAR_LOG_FMT_INFO(g_logger, "info %s:%d", __FILE__, __LINE__);
    SYLAR_LOG_FMT_DEBUG(g_logger, "debug %s:%d", __FILE__, __LINE__); //不会输出
   
    sleep(1);
    sylar::SetThreadName("brand_new_thread");

    g_logger->setLevel(sylar::LogLevel::WARN);
    SYLAR_LOG_FATAL(g_logger) << "fatal msg";
    SYLAR_LOG_ERROR(g_logger) << "err msg";
    SYLAR_LOG_INFO(g_logger) << "info msg"; // 不打印
    SYLAR_LOG_DEBUG(g_logger) << "debug msg"; // 不打印

    //新增一个输出地---到文件
    sylar::FileLogAppender::ptr fileAppender(new sylar::FileLogAppender("./log.txt"));
    g_logger->addAppender(fileAppender);
    SYLAR_LOG_FATAL(g_logger) << "fatal msg";
    SYLAR_LOG_ERROR(g_logger) << "err msg";
    SYLAR_LOG_INFO(g_logger) << "info msg"; // 不打印
    SYLAR_LOG_DEBUG(g_logger) << "debug msg"; // 不打印

    //定义一个日志器 名字test_logger
    sylar::Logger::ptr test_logger = SYLAR_LOG_NAME("test_logger");
    sylar::StdoutLogAppender::ptr appender(new sylar::StdoutLogAppender);
    // 自己重新定义了一个格式   %d-时间：%rms-启动毫秒数 %p级别 %c日志名称 %f文件名：%l行号 %m消息 %n换行
    // 这里%d后面即使没加{%Y-%m%d %H:%M:%S}这种时间格式，在formatterItem的处理时间的子类中会判断，如果没
    // 有传进来具体的时间格式 就会按照{%Y-%m%d %H:%M:%S}这种格式输出时间
    //sylar::LogFormatter::ptr formatter(new sylar::LogFormatter("%d:%rms%T%p%T%c%T%f:%l %m%n"));
    sylar::LogFormatter::ptr formatter(new sylar::LogFormatter("%d%T%f:%l %m%n")); 
    appender->setFormatter(formatter);
    test_logger->addAppender(appender);
    test_logger->setLevel(sylar::LogLevel::WARN);

    SYLAR_LOG_ERROR(test_logger) << "err msg";
    SYLAR_LOG_INFO(test_logger) << "info msg"; // 不打印

    // //自己测试 创建日志器
    sylar::Logger::ptr myloggerQQ = sylar::LoggerMgr::GetInstance()->getLogger("myloggerQQ");
    //添加输出地
    sylar::StdoutLogAppender::ptr stdptr(new sylar::StdoutLogAppender);
    sylar::FileLogAppender::ptr fileptr(new sylar::FileLogAppender("/Coroutines/sylar-from-scratch/mylog.txt"));
    myloggerQQ->addAppender(stdptr);
    myloggerQQ->addAppender(fileptr);
    
    //输出日志
    SYLAR_LOG_INFO(myloggerQQ) << "QQQ----hello world!";
    SYLAR_LOG_INFO(myloggerQQ) << "QQQ----this is my logger!";


    //------------------------------------------------------------------------------

    sylar::Logger::ptr wzllogger = SYLAR_LOG_NAME("wzllogger");
    sylar::StdoutLogAppender::ptr wzlappender(new sylar::StdoutLogAppender);
    sylar::FileLogAppender::ptr wzlappenderfile(new sylar::FileLogAppender("/Coroutines/sylar-from-scratch/newlog.txt"));
    //wzllogger->addAppender(wzlappender);
    wzllogger->addAppender(wzlappenderfile);
    //wzllogger->setLevel(sylar::LogLevel::INFO);

    SYLAR_LOG_ERROR(wzllogger) << "wangzhilei->err msg";
    SYLAR_LOG_INFO(wzllogger) << "wangzhilei->info msg wangzhilei"; 
    SYLAR_LOG_INFO(wzllogger) << "show tables";
    SYLAR_LOG_INFO(wzllogger) << "show databases";
    SYLAR_LOG_INFO(wzllogger) << "select * from l where a=10 for update";
    SYLAR_LOG_FMT_INFO(wzllogger, "%s", "You are beautiful, gay, giving, gentle, idiotically and deliciously feminine");
    SYLAR_LOG_FMT_INFO(wzllogger, "%s", "sexy,wonderfully intelligent,and wonderfully silly as well.");
    SYLAR_LOG_FMT_INFO(wzllogger, "%s", "I want nothing else in this life than to be with you, to listen and watch you,");
    SYLAR_LOG_FMT_INFO(wzllogger, "%s", "your beautiful voice, your beauty,to argue with you, to laugh with you");
    // 输出全部日志器的配置
    g_logger->setLevel(sylar::LogLevel::INFO);
    SYLAR_LOG_INFO(g_logger) << "logger config:" << sylar::LoggerMgr::GetInstance()->toYamlString();

    return 0;
}