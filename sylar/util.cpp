/**
 * @file util.cpp
 * @brief util函数实现
 * @version 0.1
 * @date 2021-06-08
 */

#include <unistd.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <signal.h> // for kill()
#include <sys/syscall.h>
#include <sys/stat.h>
#include <execinfo.h> // for backtrace()
#include <cxxabi.h>   // for abi::__cxa_demangle()
#include <algorithm>  // for std::transform()
#include "util.h"
#include "log.h"
#include "fiber.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 返回的是全局唯一的线程id
pid_t GetThreadId() 
{
    return syscall(SYS_gettid);
}

uint64_t GetFiberId() 
{
    return Fiber::GetFiberId();
}

//获取当前时间 返回的时间是毫秒级的 epoll_wait()的时间也是毫秒级的
uint64_t GetElapsedMS() 
{
    // struct timespec的两个属性值：秒，纳秒
    struct timespec ts = {0}; 
    // CLOCK_MONOTONIC:从系统启动这一刻起开始计时,不受系统时间被用户改变的影响。
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    // 返回的是毫秒
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
// 返回线程名称
std::string GetThreadName() 
{
    char thread_name[16] = {0};
    pthread_getname_np(pthread_self(), thread_name, 16);
    return std::string(thread_name);
}

void SetThreadName(const std::string &name) 
{
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}

static std::string demangle(const char *str) 
{
    size_t size = 0;
    int status  = 0;
    std::string rt;
    rt.resize(256);
    if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) 
    {
        char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
        if (v) 
        {
            std::string result(v);
            free(v);
            return result;
        }
    }
    if (1 == sscanf(str, "%255s", &rt[0])) 
    {
        return rt;
    }
    return str;
}

void Backtrace(std::vector<std::string> &bt, int size, int skip) 
{
    void **array = (void **)malloc((sizeof(void *) * size));
    size_t s     = ::backtrace(array, size);

    char **strings = backtrace_symbols(array, s);
    if (strings == NULL) {
        SYLAR_LOG_ERROR(g_logger) << "backtrace_synbols error";
        return;
    }

    for (size_t i = skip; i < s; ++i) {
        bt.push_back(demangle(strings[i]));
    }

    free(strings);
    free(array);
}

std::string BacktraceToString(int size, int skip, const std::string &prefix) 
{
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i) 
    {
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}

uint64_t GetCurrentMS() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}

uint64_t GetCurrentUS() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

std::string ToUpper(const std::string &name) 
{
    std::string rt = name;
    std::transform(rt.begin(), rt.end(), rt.begin(), ::toupper);
    return rt;
}

std::string ToLower(const std::string &name) 
{
    std::string rt = name;
    std::transform(rt.begin(), rt.end(), rt.begin(), ::tolower);
    return rt;
}

std::string Time2Str(time_t ts, const std::string &format) 
{
    struct tm tm;
    // localtime_r也是用来获取系统时间，运行于linux平台下, 结果保存在tm。
    localtime_r(&ts, &tm);
    char buf[64];
    // 按照format格式 把tm的内容写入buf
    strftime(buf, sizeof(buf), format.c_str(), &tm);
    return buf;
}
// 字符串转时间
time_t Str2Time(const char *str, const char *format) 
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    // char *strptime(char *, char* format, struct tm*) 字符串转时间 保存在t里
    if (!strptime(str, format, &t)) 
    {
        return 0;
    }
    // 把tm类型的结构体转换为time_t
    return mktime(&t);
}

void FSUtil::ListAllFile(std::vector<std::string> &files, const std::string &path, const std::string &subfix) 
{
    // access用于检查文件的权限 参数0表示只检查文件是否存在
    if (access(path.c_str(), 0) != 0) 
    {
        return;
    }
    // opendir函数： opendir 用于打开一个目录，并返回一个指向目录的指针（称为目录流）
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) 
    {
        return;
    }
    // dp用来保存读到的目录里的每一项
    struct dirent *dp = nullptr;
    // 逐条读取目录中的文件 信息存在dp中
    while ((dp = readdir(dir)) != nullptr) 
    {
        // 该条是目录
        if (dp->d_type == DT_DIR) 
        {
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) 
            {
                continue;
            }
            // 递归找
            ListAllFile(files, path + "/" + dp->d_name, subfix);
        }  
        else if (dp->d_type == DT_REG) // 该条是一个普通文件
        {
            // dp->d_name 读到的文件过着目录的名字，不加路径
            std::string filename(dp->d_name);
            // 如果查找标志为空 即所有的普通文件都符合 -> 路径+文件名添加到vector 
            if (subfix.empty()) 
            {
                files.push_back(path + "/" + filename);
            } 
            else 
            {
                // 如果文件名+后缀的大小都小于 ".yml" 一定不是
                if (filename.size() < subfix.size()) 
                {
                    continue;
                }
                // 再判断是否是".yml"文件 filename.substr() 的返回值是一个被截取的字符串，而filename本身并没有改变
                if (filename.substr(filename.length() - subfix.size()) == subfix) 
                {
                    files.push_back(path + "/" + filename);
                }
            }
        }
    }
    closedir(dir);
}

static int __lstat(const char *file, struct stat *st = nullptr) 
{
    struct stat lst;
    // 获取文件信息
    int ret = lstat(file, &lst);
    if (st) 
    {
        *st = lst;
    }
    return ret;
}

static int __mkdir(const char *dirname) 
{
    // 判断文件是否允许读 如果可读就说明存在 直接返回 不需要创建
    if (access(dirname, F_OK) == 0) 
    {
        return 0;
    }
    // 否则创建目录 自己和同组有读、写、执行权限，其他人有读和执行的权限
    return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

bool FSUtil::Mkdir(const std::string &dirname) 
{
    // 存在 直接返回
    if (__lstat(dirname.c_str()) == 0) 
    {
        return true;
    }
    char *path = strdup(dirname.c_str()); //复制路径
    char *ptr  = strchr(path + 1, '/');   //查找 / 字符第一次出现的位置
    do 
    {
        for (; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/')) 
        {
            *ptr = '\0';
            if (__mkdir(path) != 0) 
            {
                break;
            }
        }
        if (ptr != nullptr) 
        {
            break;
        } 
        else if (__mkdir(path) != 0) 
        {
            break;
        }
        free(path);
        return true;
    } while (0);
    free(path);
    return false;
}

bool FSUtil::IsRunningPidfile(const std::string &pidfile) 
{
    if (__lstat(pidfile.c_str()) != 0) 
    {
        return false;
    }
    std::ifstream ifs(pidfile);
    std::string line;
    if (!ifs || !std::getline(ifs, line)) 
    {
        return false;
    }
    if (line.empty()) 
    {
        return false;
    }
    pid_t pid = atoi(line.c_str());
    if (pid <= 1) 
    {
        return false;
    }
    if (kill(pid, 0) != 0) 
    {
        return false;
    }
    return true;
}

bool FSUtil::Unlink(const std::string &filename, bool exist) 
{
    if (!exist && __lstat(filename.c_str())) 
    {
        return true;
    }
    return ::unlink(filename.c_str()) == 0;
}

bool FSUtil::Rm(const std::string &path) 
{
    struct stat st;
    if (lstat(path.c_str(), &st)) 
    {
        return true;
    }
    if (!(st.st_mode & S_IFDIR)) 
    {
        return Unlink(path);
    }

    DIR *dir = opendir(path.c_str());
    if (!dir) 
    {
        return false;
    }

    bool ret          = true;
    struct dirent *dp = nullptr;
    while ((dp = readdir(dir))) 
    {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) 
        {
            continue;
        }
        std::string dirname = path + "/" + dp->d_name;
        ret                 = Rm(dirname);
    }
    closedir(dir);
    if (::rmdir(path.c_str())) 
    {
        ret = false;
    }
    return ret;
}

bool FSUtil::Mv(const std::string &from, const std::string &to) 
{
    if (!Rm(to)) 
    {
        return false;
    }
    return rename(from.c_str(), to.c_str()) == 0;
}

bool FSUtil::Realpath(const std::string &path, std::string &rpath) 
{
    if (__lstat(path.c_str())) 
    {
        return false;
    }
    char *ptr = ::realpath(path.c_str(), nullptr);
    if (nullptr == ptr)
    {
        return false;
    }
    std::string(ptr).swap(rpath);
    free(ptr);
    return true;
}

bool FSUtil::Symlink(const std::string &from, const std::string &to) 
{
    if (!Rm(to)) 
    {
        return false;
    }
    return ::symlink(from.c_str(), to.c_str()) == 0;
}

std::string FSUtil::Dirname(const std::string &filename) 
{
    if (filename.empty()) 
    {
        return ".";
    }
    auto pos = filename.rfind('/');
    if (pos == 0) 
    {
        return "/";
    } 
    else if (pos == std::string::npos) 
    {
        return ".";
    } 
    else 
    {
        return filename.substr(0, pos);
    }
}

std::string FSUtil::Basename(const std::string &filename) 
{
    if (filename.empty()) 
    {
        return filename;
    }
    auto pos = filename.rfind('/');
    if 
    (pos == std::string::npos) 
    {
        return filename;
    } 
    else 
    {
        return filename.substr(pos + 1);
    }
}

bool FSUtil::OpenForRead(std::ifstream &ifs, const std::string &filename, std::ios_base::openmode mode) 
{
    ifs.open(filename.c_str(), mode);
    return ifs.is_open();
}

bool FSUtil::OpenForWrite(std::ofstream &ofs, const std::string &filename, std::ios_base::openmode mode) 
{
    ofs.open(filename.c_str(), mode);
    if (!ofs.is_open()) 
    {
        std::string dir = Dirname(filename);
        Mkdir(dir);
        ofs.open(filename.c_str(), mode);
    }
    return ofs.is_open();
}

int8_t TypeUtil::ToChar(const std::string &str) 
{
    if (str.empty()) 
    {
        return 0;
    }
    return *str.begin();
}

int64_t TypeUtil::Atoi(const std::string &str) 
{
    if (str.empty()) 
    {
        return 0;
    }
    return strtoull(str.c_str(), nullptr, 10);
}

double TypeUtil::Atof(const std::string &str) 
{
    if (str.empty()) 
    {
        return 0;
    }
    return atof(str.c_str());
}

int8_t TypeUtil::ToChar(const char *str) 
{
    if (str == nullptr) 
    {
        return 0;
    }
    return str[0];
}

int64_t TypeUtil::Atoi(const char *str) 
{
    if (str == nullptr) 
    {
        return 0;
    }
    return strtoull(str, nullptr, 10);
}

double TypeUtil::Atof(const char *str) 
{
    if (str == nullptr) 
    {
        return 0;
    }
    return atof(str);
}

std::string StringUtil::Format(const char* fmt, ...) 
{
    va_list ap;
    va_start(ap, fmt);
    auto v = Formatv(fmt, ap);
    va_end(ap);
    return v;
}

std::string StringUtil::Formatv(const char* fmt, va_list ap) 
{
    char* buf = nullptr;
    auto len = vasprintf(&buf, fmt, ap);
    if(len == -1) 
    {
        return "";
    }
    std::string ret(buf, len);
    free(buf);
    return ret;
}

static const char uri_chars[256] = 
{
    /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 0, 0,
    /* 64 */
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
    /* 128 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    /* 192 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

static const char xdigit_chars[256] = 
{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define CHAR_IS_UNRESERVED(c)           \
    (uri_chars[(unsigned char)(c)])

//-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~
std::string StringUtil::UrlEncode(const std::string& str, bool space_as_plus) 
{
    static const char *hexdigits = "0123456789ABCDEF";
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str() ; c < end; ++c) 
    {
        if(!CHAR_IS_UNRESERVED(*c)) 
        {
            if(!ss) 
            {
                ss = new std::string;
                ss->reserve(str.size() * 1.2);
                ss->append(str.c_str(), c - str.c_str());
            }
            if(*c == ' ' && space_as_plus) 
            {
                ss->append(1, '+');
            } 
            else 
            {
                ss->append(1, '%');
                ss->append(1, hexdigits[(uint8_t)*c >> 4]);
                ss->append(1, hexdigits[*c & 0xf]);
            }
        } 
        else if(ss) 
        {
            ss->append(1, *c);
        }
    }
    if(!ss) 
    {
        return str;
    } 
    else 
    {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::UrlDecode(const std::string& str, bool space_as_plus) 
{
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str(); c < end; ++c) 
    {
        if(*c == '+' && space_as_plus) 
        {
            if(!ss) 
            {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, ' ');
        } 
        else if(*c == '%' && (c + 2) < end
                    && isxdigit(*(c + 1)) && isxdigit(*(c + 2)))
        {
            if(!ss) 
            {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, (char)(xdigit_chars[(int)*(c + 1)] << 4 | xdigit_chars[(int)*(c + 2)]));
            c += 2;
        } 
        else if(ss) 
        {
            ss->append(1, *c);
        }
    }
    if(!ss) 
    {
        return str;
    } 
    else 
    {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::Trim(const std::string& str, const std::string& delimit) 
{
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) 
    {
        return "";
    }
    auto end = str.find_last_not_of(delimit);
    return str.substr(begin, end - begin + 1);
}

std::string StringUtil::TrimLeft(const std::string& str, const std::string& delimit) 
{
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) 
    {
        return "";
    }
    return str.substr(begin);
}

std::string StringUtil::TrimRight(const std::string& str, const std::string& delimit) 
{
    auto end = str.find_last_not_of(delimit);
    if(end == std::string::npos) 
    {
        return "";
    }
    return str.substr(0, end);
}

std::string StringUtil::WStringToString(const std::wstring& ws) 
{
    std::string str_locale = setlocale(LC_ALL, "");
    const wchar_t* wch_src = ws.c_str();
    size_t n_dest_size = wcstombs(NULL, wch_src, 0) + 1;
    char *ch_dest = new char[n_dest_size];
    memset(ch_dest,0,n_dest_size);
    wcstombs(ch_dest,wch_src,n_dest_size);
    std::string str_result = ch_dest;
    delete []ch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return str_result;
}

std::wstring StringUtil::StringToWString(const std::string& s) 
{
    std::string str_locale = setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t n_dest_size = mbstowcs(NULL, chSrc, 0) + 1;
    wchar_t* wch_dest = new wchar_t[n_dest_size];
    wmemset(wch_dest, 0, n_dest_size);
    mbstowcs(wch_dest,chSrc,n_dest_size);
    std::wstring wstr_result = wch_dest;
    delete []wch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return wstr_result;
}


} // namespace sylar