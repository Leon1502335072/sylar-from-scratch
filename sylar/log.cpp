/**
 * @file log.cpp
 * @brief 日志模块实现
 * @version 0.1
 * @date 2021-06-08
 */

#include <utility> // for std::pair
#include "log.h"
#include "config.h"
#include "env.h"

namespace sylar {

const char *LogLevel::ToString(LogLevel::Level level) 
{
    switch (level) 
    {
    #define XX(name) case LogLevel::name: return #name;
        XX(FATAL);
        XX(ALERT);
        XX(CRIT);
        XX(ERROR);
        XX(WARN);
        XX(NOTICE);
        XX(INFO);
        XX(DEBUG);
    #undef XX       // #undef 去掉xx的定义
        default:
            return "NOTSET";
    }
    return "NOTSET";
}

LogLevel::Level LogLevel::FromString(const std::string &str) 
{
#define XX(level, v) if(str == #v) { return LogLevel::level; } // #v 字符串的意思
    XX(FATAL, fatal);
    XX(ALERT, alert);
    XX(CRIT, crit);
    XX(ERROR, error);
    XX(WARN, warn);
    XX(NOTICE, notice);
    XX(INFO, info);
    XX(DEBUG, debug);

    XX(FATAL, FATAL);
    XX(ALERT, ALERT);
    XX(CRIT, CRIT);
    XX(ERROR, ERROR);
    XX(WARN, WARN);
    XX(NOTICE, NOTICE);
    XX(INFO, INFO);
    XX(DEBUG, DEBUG);
#undef XX

    return LogLevel::NOTSET;
}

LogEvent::LogEvent(const std::string &logger_name, LogLevel::Level level, const char *file, int32_t line
        , int64_t elapse, uint32_t thread_id, uint64_t fiber_id, time_t time
        , const std::string &thread_name)
    : m_level(level)
    , m_file(file)
    , m_line(line)
    , m_elapse(elapse)
    , m_threadId(thread_id)
    , m_fiberId(fiber_id)
    , m_time(time)
    , m_threadName(thread_name)
    , m_loggerName(logger_name) {
}

void LogEvent::printf(const char *fmt, ...) 
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void LogEvent::vprintf(const char *fmt, va_list ap) 
{
    char *buf = nullptr;
    // vasprintf 用来动态给buf分配内存来存放格式化后的字符串
    int len = vasprintf(&buf, fmt, ap);
    if(len != -1) 
    {
        m_ss << std::string(buf, len);
        free(buf);
    }
}

// 这些都是FormatItem的子类 后面通过父类指针指向子类空间实现多态

// m 内容
class MessageFormatItem : public LogFormatter::FormatItem
{
public:
    MessageFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getContent();
    }
};

// p 日志级别
class LevelFormatItem : public LogFormatter::FormatItem 
{
public:
    LevelFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << LogLevel::ToString(event->getLevel());
    }
};

// r 累计毫秒数
class ElapseFormatItem : public LogFormatter::FormatItem 
{
public:
    ElapseFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getElapse();
    }
};

// c 日志器名称
class LoggerNameFormatItem : public LogFormatter::FormatItem 
{
public:
    LoggerNameFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getLoggerName();
    }
};

// t 线程号
class ThreadIdFormatItem : public LogFormatter::FormatItem 
{
public:
    ThreadIdFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getThreadId();
    }
};

// F 协程号
class FiberIdFormatItem : public LogFormatter::FormatItem 
{
public:
    FiberIdFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getFiberId();
    }
};

// N 线程名称
class ThreadNameFormatItem : public LogFormatter::FormatItem 
{
public:
    ThreadNameFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getThreadName();
    }
};

// d 时间
class DateTimeFormatItem : public LogFormatter::FormatItem 
{
public:
    DateTimeFormatItem(const std::string& format = "%Y-%m-%d %H:%M:%S"):m_format(format) 
    {   
        if(m_format.empty()) 
        {
            m_format = "%Y-%m-%d %H:%M:%S";
        }
    }

    void format(std::ostream& os, LogEvent::ptr event) override 
    {
        struct tm tm;

        // 日志事件直接掉自己的getTime() 获得一个总秒数
        time_t time = event->getTime();

        // localtime_r函数将给定的时间值转换为本地时区的日历时间表示 结果存在tm结构体中
        localtime_r(&time, &tm);

        char buf[64];
        // 用于日期和时间格式化的函数，它允许你将一个 tm 结构体表示的时间转换成一个基于指定格式的字符串。
        strftime(buf, sizeof(buf), m_format.c_str(), &tm);
        os << buf;
    }
private:
    std::string m_format;
};

// f 文件名
class FileNameFormatItem : public LogFormatter::FormatItem 
{
public:
    FileNameFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getFile();
    }
};

// l 行号
class LineFormatItem : public LogFormatter::FormatItem 
{
public:
    LineFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << event->getLine();
    }
};
// n 换行符 （换行符子类，实现换行）
class NewLineFormatItem : public LogFormatter::FormatItem 
{
public:
    NewLineFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << std::endl;
    }
};

// 常规字符处理类
class StringFormatItem : public LogFormatter::FormatItem 
{
public:
    StringFormatItem(const std::string& str) : m_string(str) {}
        
    void format(std::ostream& os, LogEvent::ptr event) override 
    {
        os << m_string;
    }
private:
    std::string m_string;
};
// T 制表符
class TabFormatItem : public LogFormatter::FormatItem 
{
public:
    TabFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << "\t";
    }
};
// % 百分号
class PercentSignFormatItem : public LogFormatter::FormatItem 
{
public:
    PercentSignFormatItem(const std::string &str) {}
    void format(std::ostream &os, LogEvent::ptr event) override 
    {
        os << "%";
    }
};

LogFormatter::LogFormatter(const std::string &pattern): m_pattern(pattern)    
{
    init();
}

/**
 * 简单的状态机判断，提取pattern中的常规字符和模式字符
 * 
 * 解析的过程就是从头到尾遍历，根据状态标志决定当前字符是常规字符还是模式字符
 * 
 * 一共有两种状态，即正在解析常规字符和正在解析模板转义字符
 * 
 * 比较麻烦的是%%d，后面可以接一对大括号指定时间格式，比如%%d{%%Y-%%m-%%d %%H:%%M:%%S}，这个状态需要特殊处理
 * 
 * 一旦状态出错就停止解析，并设置错误标志，未识别的pattern转义字符也算出错
 * 
 * @see LogFormatter::LogFormatter
 */
void LogFormatter::init() 
{
    // 按顺序存储解析到的pattern项
    // 每个pattern包括一个整数类型和一个字符串，类型为0表示该pattern是常规字符串，为1表示该pattern需要转义
    // 日期格式单独用下面的dataformat存储    
    std::vector<std::pair<int, std::string>> patterns;
    // 临时存储常规字符串
    std::string tmp;
    // 日期格式字符串，默认把位于%d后面的大括号对里的全部字符都当作格式字符，不校验格式是否合法
    std::string dateformat;
    // 是否解析出错
    bool error = false;
    
    // 是否正在解析常规字符，初始时为true
    bool parsing_string = true;

    // "%d{%Y-%m-%d %H:%M:%S} [%rms]%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
    size_t i = 0;
    while(i < m_pattern.size()) 
    {
        std::string c = std::string(1, m_pattern[i]);
        if(c == "%") 
        {
            if(parsing_string) 
            {
                // 如果tmp非空
                if(!tmp.empty()) 
                {
                    patterns.push_back(std::make_pair(0, tmp));
                }
                tmp.clear();
                parsing_string = false; // 在解析常规字符时遇到%，表示开始解析模板字符
                // parsing_pattern = true;
                i++;
                continue;
            } 
            else 
            {
                patterns.push_back(std::make_pair(1, c));
                parsing_string = true; // 在解析模板字符时遇到%，表示这里是一个%转义
                // parsing_pattern = false;
                i++;
                continue;
            }
        } 
        else 
        { // not %
            if(parsing_string) 
            { // 持续解析常规字符直到遇到%，解析出的字符串作为一个常规字符串加入patterns
                tmp += c;
                i++;
                continue;
            } 
            else 
            { // 模板字符，直接添加到patterns中，添加完成后，状态变为解析常规字符，%d特殊处理
                patterns.push_back(std::make_pair(1, c));
                parsing_string = true; 

                // 后面是对%d的特殊处理，如果%d后面直接跟了一对大括号，那么把大括号里面的内容提取出来作为dateformat
                if(c != "d") 
                {
                    i++;
                    continue;
                }
                i++;
                if(i < m_pattern.size() && m_pattern[i] != '{') 
                {
                    continue;
                }
                i++;
                while( i < m_pattern.size() && m_pattern[i] != '}') 
                {
                    dateformat.push_back(m_pattern[i]);
                    i++;
                }
                if(m_pattern[i] != '}') 
                {
                    // %d后面的大括号没有闭合，直接报错
                    std::cout << "[ERROR] LogFormatter::init() " << "pattern: [" << m_pattern << "] '{' not closed" << std::endl;
                    error = true;
                    break;
                }
                i++;
                continue;
            }
        }
    } // end while(i < m_pattern.size())

    if(error) 
    {
        m_error = true;
        return;
    }

    // 模板解析结束之后剩余的常规字符也要算进去
    if(!tmp.empty()) 
    {
        patterns.push_back(std::make_pair(0, tmp));
        tmp.clear();
    }

    // for debug 
    // std::cout << "patterns:" << std::endl;
    // for(auto &v : patterns) {
    //     std::cout << "type = " << v.first << ", value = " << v.second << std::endl;
    // }
    // std::cout << "dataformat = " << dateformat << std::endl;
    
    // map<string, function<FormatItem::ptr(const std::string& str)>> function<该种函数类型的返回值 (参数)>
    // s_format_items是一个函数集合，一个string对应一个函数对象，即传进来什么string就做什么样的处理
    static std::map<std::string, std::function<FormatItem::ptr(const std::string& str)> > s_format_items = {
#define XX(str, C)  {#str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt));} }
                    // key lamda表达式是value
        XX(m, MessageFormatItem),           // m:消息
        XX(p, LevelFormatItem),             // p:日志级别
        XX(c, LoggerNameFormatItem),        // c:日志器名称
//        XX(d, DateTimeFormatItem),          // d:日期时间
        XX(r, ElapseFormatItem),            // r:累计毫秒数
        XX(f, FileNameFormatItem),          // f:文件名
        XX(l, LineFormatItem),              // l:行号
        XX(t, ThreadIdFormatItem),          // t:线程号
        XX(F, FiberIdFormatItem),           // F:协程号
        XX(N, ThreadNameFormatItem),        // N:线程名称
        XX(%, PercentSignFormatItem),       // %:百分号
        XX(T, TabFormatItem),               // T:制表符
        XX(n, NewLineFormatItem),           // n:换行符
#undef XX
    };

    for(auto &v : patterns) {        
        if(v.first == 0) // 常规字符
        {
            m_items.push_back(FormatItem::ptr(new StringFormatItem(v.second)));
        } 
        else if( v.second =="d") //非常规字符且second是d 时间格式
        {
            m_items.push_back(FormatItem::ptr(new DateTimeFormatItem(dateformat)));
        } 
        else // 转义字符
        {
            auto it = s_format_items.find(v.second); //在map里找到对应的转义字符的迭代器（索引）
            // 没有该转义字符
            if(it == s_format_items.end()) 
            {
                std::cout << "[ERROR] LogFormatter::init() " << "pattern: [" << m_pattern << "] " << 
                "unknown format item: " << v.second << std::endl;
                error = true;
                break;
            } 
            else 
            {
                // 用map里相应的处理函数对其进行处理 再添加到 m_items
                m_items.push_back(it->second(v.second));
            }
        }
    }

    if(error) 
    {
        m_error = true;
        return;
    }
}

std::string LogFormatter::format(LogEvent::ptr event) 
{
    std::stringstream ss;
    for(auto &i:m_items) 
    {
        // 多态--不同子类的format实现不同的功能
        i->format(ss, event);
    }
    return ss.str();
}

// 流式输出到os中 也就是传过来的cout 或者是文件流
std::ostream &LogFormatter::format(std::ostream &os, LogEvent::ptr event) 
{
    for(auto &i:m_items) 
    {
        i->format(os, event);
    }
    return os;
}

// LogAppender的default_formatter会通过子类传进来
LogAppender::LogAppender(LogFormatter::ptr default_formatter)
    : m_defaultFormatter(default_formatter) {
}

void LogAppender::setFormatter(LogFormatter::ptr val) 
{
    MutexType::Lock lock(m_mutex);
    m_formatter = val;
}

LogFormatter::ptr LogAppender::getFormatter() 
{
    MutexType::Lock lock(m_mutex);
    return m_formatter ? m_formatter : m_defaultFormatter;
}

// 输出到cout的子类，在构造时给父类构造传一个默认的日志格式的格式器智能指针
StdoutLogAppender::StdoutLogAppender()
    : LogAppender(LogFormatter::ptr(new LogFormatter)) {
}

void StdoutLogAppender::log(LogEvent::ptr event) 
{
    if(m_formatter) 
    {
        // format的两个参数：1、输出到标准输出 2、日志事件
        m_formatter->format(std::cout, event);
    } 
    else 
    {
        m_defaultFormatter->format(std::cout, event);
    }
}

std::string StdoutLogAppender::toYamlString() 
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    // node["pattern"] = m_formatter->getPattern() 原来这行代码是有bug的，也就是考虑的不全面 换成下面这一行就行了
    node["pattern"] = m_formatter ? m_formatter->getPattern() : m_defaultFormatter->getPattern();
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// 输出到文件的子类
FileLogAppender::FileLogAppender(const std::string &file)
    : LogAppender(LogFormatter::ptr(new LogFormatter)) 
{
    m_filename = file;
    reopen();
    if(m_reopenError) 
    {
        std::cout << "reopen file " << m_filename << " error" << std::endl;
    }
}

/**
 * 如果一个日志事件距离上次写日志超过3秒，那就重新打开一次日志文件
 */
void FileLogAppender::log(LogEvent::ptr event) 
{
    uint64_t now = event->getTime();
    if(now >= (m_lastTime + 3)) 
    {
        reopen();
        if(m_reopenError) 
        {
            std::cout << "reopen file " << m_filename << " error" << std::endl;
        }
        m_lastTime = now;
    }
    if(m_reopenError) 
    {
        return;
    }

    MutexType::Lock lock(m_mutex);
    if(m_formatter) // 用户自己设置了日志格式
    {
        if(!m_formatter->format(m_filestream, event)) 
        {
            std::cout << "[ERROR] FileLogAppender::log() format error" << std::endl;
        }
    } 
    else // 默认的日志格式
    {
        if(!m_defaultFormatter->format(m_filestream, event)) 
        {
            std::cout << "[ERROR] FileLogAppender::log() format error" << std::endl;
        }
    }
    
}

bool FileLogAppender::reopen() 
{
    MutexType::Lock lock(m_mutex);
    // 如果已经打开则先关闭
    if(m_filestream) 
    {
        m_filestream.close();
    }
    // 以流式追加的方式打开文件m_filename，然后往m_filestream输入的内容就会直接输出到文件m_filename
    m_filestream.open(m_filename, std::ios::app);
    // 文件错误打开标识：如果上面打开失败m_reopenError为true 成功为false
    m_reopenError = !m_filestream;
    // 返回重新打开是否成功
    return !m_reopenError;
}

std::string FileLogAppender::toYamlString() 
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["file"] = m_filename;
    node["pattern"] = m_formatter ? m_formatter->getPattern() : m_defaultFormatter->getPattern();
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// 日志器的构造
Logger::Logger(const std::string &name)
    : m_name(name)
    , m_level(LogLevel::INFO)
    , m_createTime(GetElapsedMS()) {
    }

void Logger::addAppender(LogAppender::ptr appender) 
{
    MutexType::Lock lock(m_mutex);
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender) 
{
    MutexType::Lock lock(m_mutex);
    for(auto it = m_appenders.begin(); it != m_appenders.end(); it++) 
    {
        if(*it == appender) 
        {
            m_appenders.erase(it);
            break;
        }
    }
}

void Logger::clearAppenders() 
{
    MutexType::Lock lock(m_mutex);
    m_appenders.clear();
}

// 调用Logger的所有appenders将日志写一遍，Logger至少要有一个appender，否则没有输出   
// FATAL=0, ALERT=100, CRIT=200, ERROR=300, WARN=400, NOTICE=500, INFO=600, DEBUG= 700, NOTSET = 800,      
void Logger::log(LogEvent::ptr event) 
{
    if(event->getLevel() <= m_level) 
    {
        for(auto &i : m_appenders) 
        {
            i->log(event);
        }
    }
}

std::string Logger::toYamlString() 
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["name"] = m_name;
    node["level"] = LogLevel::ToString(m_level);
    for(auto &i : m_appenders) 
    {
        node["appenders"].push_back(YAML::Load(i->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr event)
                            : m_logger(logger), m_event(event) {
}

/**
 * @note LogEventWrap在析构时写日志
 */
LogEventWrap::~LogEventWrap() 
{
    // log里将日志事件输出到所有的输出地集合 list<appender>
    m_logger->log(m_event);
}

LoggerManager::LoggerManager() 
{
    // 创建root日志器 并赋值给m_root
    m_root.reset(new Logger("root"));
    // 添加输出地，这里是默认输出到标准输出 （父类指针指向子类空间）
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));
    // 日志器添加到map里
    m_loggers[m_root->getName()] = m_root;
    init();
}

//如果指定名称的日志器未找到，那会就新创建一个，但是新创建的Logger是不带Appender的，需要手动添加Appender
Logger::ptr LoggerManager::getLogger(const std::string &name) 
{
    MutexType::Lock lock(m_mutex);
    auto it = m_loggers.find(name);
    if(it != m_loggers.end()) 
    {
        return it->second;
    }

    Logger::ptr logger(new Logger(name));
    m_loggers[name] = logger;
    return logger;
}

/**
 * @todo 实现从配置文件加载日志配置
 */
void LoggerManager::init() {
}

std::string LoggerManager::toYamlString() 
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    for(auto& i : m_loggers) 
    {
        node.push_back(YAML::Load(i.second->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

///////////////////////////////////////////////////////////////////////////////
// 从配置文件中加载日志配置
/**
 * @brief 日志输出器配置结构体定义
 */
struct LogAppenderDefine 
{
    int type = 0; // 输出地： 1 File, 2 Stdout
    std::string pattern; 
    std::string file;

    //重载 ==号 用于判断两个结构体是否相等
    bool operator==(const LogAppenderDefine &oth) const 
    {
        return type == oth.type && pattern == oth.pattern && file == oth.file;
    }
};

/**
 * @brief 日志器配置结构体定义
 */
struct LogDefine 
{
    std::string name;
    LogLevel::Level level = LogLevel::NOTSET;
    std::vector<LogAppenderDefine> appenders;

    // 重载 =
    bool operator==(const LogDefine &oth) const 
    {
        return name == oth.name && level == oth.level && appenders == appenders;
    }

    // 重载 <
    bool operator<(const LogDefine &oth) const 
    {
        return name < oth.name;
    }

    // 名字是否为空，为空返回false
    bool isValid() const 
    {
        return !name.empty();
    }
};

template<>
class LexicalCast<std::string, LogDefine> {
public:
    LogDefine operator()(const std::string &v) 
    {
        YAML::Node n = YAML::Load(v);
        LogDefine ld;
        // 是否定义了name这个标签 IsDefined就是用来判断在yaml文件中是否定义了某个标签
        if(!n["name"].IsDefined()) 
        {
            std::cout << "log config error: name is null, " << n << std::endl;
            throw std::logic_error("log config name is null");
        }
        ld.name = n["name"].as<std::string>();
        ld.level = LogLevel::FromString(n["level"].IsDefined() ? n["level"].as<std::string>() : "");

        if(n["appenders"].IsDefined()) 
        {
            for(size_t i = 0; i < n["appenders"].size(); i++) 
            {
                auto a = n["appenders"][i];
                if(!a["type"].IsDefined()) 
                {
                    std::cout << "log appender config error: appender type is null, " << a << std::endl;
                    continue;
                }
                std::string type = a["type"].as<std::string>();
                LogAppenderDefine lad;
                if(type == "FileLogAppender") 
                {
                    lad.type = 1; 
                    // 既然输出到文件 必须有file标签
                    if(!a["file"].IsDefined()) 
                    {
                        std::cout << "log appender config error: file appender file is null, " << a << std::endl;
                        continue;
                    }
                    lad.file = a["file"].as<std::string>();
                    if(a["pattern"].IsDefined()) 
                    {
                        lad.pattern = a["pattern"].as<std::string>();
                    }
                } 
                else if(type == "StdoutLogAppender") 
                {
                    lad.type = 2;
                    if(a["pattern"].IsDefined()) 
                    {
                        lad.pattern = a["pattern"].as<std::string>();
                    }
                } 
                else 
                {
                    std::cout << "log appender config error: appender type is invalid, " << a << std::endl;
                    continue;
                }
                ld.appenders.push_back(lad);
            }
        } // end for
        return ld;
    }
};

template<>
class LexicalCast<LogDefine, std::string> {
public:
    std::string operator()(const LogDefine &i) 
    {
        YAML::Node n;
        n["name"] = i.name;
        n["level"] = LogLevel::ToString(i.level);
        for(auto &a : i.appenders) 
        {
            YAML::Node na;
            if(a.type == 1) 
            {
                na["type"] = "FileLogAppender";
                na["file"] = a.file;
            } 
            else if(a.type == 2) 
            {
                na["type"] = "StdoutLogAppender";
            }
            // 非空
            if(!a.pattern.empty()) 
            {
                na["pattern"] = a.pattern;
            }
            n["appenders"].push_back(na);
        }
        std::stringstream ss;
        ss << n;
        return ss.str();
    }
};

// 初始化一个日志变量 默认值是一个set<LogDefine>的集合 创建即加入到了变量map的集合
sylar::ConfigVar<std::set<LogDefine>>::ptr g_log_defines = 
    sylar::Config::Lookup("logs", std::set<LogDefine>(), "logs config");

struct LogIniter {
    LogIniter() 
    {
        // 日志变量添加变化回调函数
        g_log_defines->addListener([](const std::set<LogDefine> &old_value, const std::set<LogDefine> &new_value) {
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "on log config changed";
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "old_value size: " << old_value.size();
            // 对新的set进行遍历
            for(auto &i : new_value) 
            {
                auto it = old_value.find(i);
                sylar::Logger::ptr logger;
                // 没找到 表示是新增的日志定义的结构体
                if(it == old_value.end()) 
                {
                    // 新增logger 这里只是在LoggerManager里创建应有的日志器，整体的logs变量的新值在config.h的setValue中赋值
                    logger = SYLAR_LOG_NAME(i.name);
                } 
                else 
                {
                    // 新值不等于旧值 （结构体内已重载了==）
                    if(!(i == *it)) 
                    {
                        // 修改的logger   通过名字查找日志器的宏
                        logger == SYLAR_LOG_NAME(i.name);
                    } 
                    else 
                    {
                        continue;
                    }
                }
                logger->setLevel(i.level);
                logger->clearAppenders();
                for(auto &a : i.appenders) 
                {
                    sylar::LogAppender::ptr ap; // 注意这个是父类指针

                    if(a.type == 1) // 输出到文件
                    {
                        // 指向子类空间（多态）
                        ap.reset(new FileLogAppender(a.file));
                    } 
                    else if(a.type == 2) //输出到cout
                    {
                        // 如果以daemon方式运行，则不需要创建终端appender
                        if(!sylar::EnvMgr::GetInstance()->has("d")) 
                        {
                            ap.reset(new StdoutLogAppender);
                        } 
                        else 
                        {
                            continue;
                        }
                    }
                    if(!a.pattern.empty()) // 格式非空
                    {
                        ap->setFormatter(LogFormatter::ptr(new LogFormatter(a.pattern)));
                    } 
                    else 
                    {
                        ap->setFormatter(LogFormatter::ptr(new LogFormatter)); //启用默认的格式
                    }
                    logger->addAppender(ap);
                }
            }

            // 以配置文件为主，如果程序里定义了配置文件中未定义的logger，那么把程序里定义的logger设置成无效
            for(auto &i : old_value) 
            {
                auto it = new_value.find(i);
                if(it == new_value.end()) 
                {
                    auto logger = SYLAR_LOG_NAME(i.name);
                    logger->setLevel(LogLevel::NOTSET);
                    logger->clearAppenders();
                }
            }
        });
    }
};

//在main函数之前注册配置更改的回调函数
//用于在更新配置时将log相关的配置加载到Config
static LogIniter __log_init;

///////////////////////////////////////////////////////////////////////////////

} // end namespace sylar