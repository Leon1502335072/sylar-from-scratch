/**
 * @file config.cc
 * @brief 配置模块实现
 * @version 0.1
 * @date 2021-06-14
 */
#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string &name) 
{
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str

static void ListAllMember(const std::string &prefix, const YAML::Node &node, 
                          std::list<std::pair<std::string, const YAML::Node>> &output) 
{
    // 命名不合法 find_first_not_of大小写不敏感
    if (prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) 
    {
        SYLAR_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    // 如果node是一个map
    if (node.IsMap()) 
    {
        for (auto it = node.begin(); it != node.end(); ++it)      
        {
            // 递归提取键和值
            ListAllMember(prefix.empty() ? it->first.Scalar()
                                         : prefix + "." + it->first.Scalar(),
                          it->second, output);
        }
    }
}

void Config::LoadFromYaml(const YAML::Node &root) 
{
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);

    for (auto &i : all_nodes) 
    {
        std::string key = i.first;
        if (key.empty()) 
        {
            continue;
        }
        // 将key转为小写
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        // 查询是否包含key
        ConfigVarBase::ptr var = LookupBase(key);

        // 如果存在key才从文件中加载更新，不存在直接跳过
        if (var) 
        {
            // i.second是否是标量，也就是单个元素的值 而不是数组或矩阵
            if (i.second.IsScalar()) 
            {
                // 将YAML::内结点值转为Scalar类型
                // 然后从字符串中加载（已通过实现偏特化实现了类型的转换），设置m_val，进行更新
                var->fromString(i.second.Scalar());
            } 
            else 
            {
                // 其他类型 Sequence,偏特化中fromString有对应的处理方法
                std::stringstream ss;
                ss << i.second;
                // 创建一个变量 需要同时传递与该变量相匹配的仿函数
                var->fromString(ss.str());
            }
        }
    }
}

/// 记录每个文件的修改时间
static std::map<std::string, uint64_t> s_file2modifytime;
/// 是否强制加载配置文件，非强制加载的情况下，如果记录的文件修改时间未变化，则跳过该文件的加载
static sylar::Mutex s_mutex;

void Config::LoadFromConfDir(const std::string &path, bool force) 
{
    std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    // 返回所有的.yml文件（带路径的）
    FSUtil::ListAllFile(files, absoulte_path, ".yml");

    for (auto &i : files) 
    {
        {
            struct stat st;
            // 获取文件信息保存在st中
            lstat(i.c_str(), &st);
            sylar::Mutex::Lock lock(s_mutex);
            // 非强制加载且最后一次修改时间没变 跳过
            if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) 
            {
                continue;
            }
            // 服务启动后第一次会把yml文件的修改时间都加载进s_file2modifytime
            s_file2modifytime[i] = st.st_mtime;
        }
        try 
        {
            // 加载yml文件
            YAML::Node root = YAML::LoadFile(i);
            LoadFromYaml(root);
            SYLAR_LOG_INFO(g_logger) << "LoadConfFile file="
                                     << i << " ok";
        } 
        catch (...) 
        {
            SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file="
                                      << i << " failed";
        }
    }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) 
{
    RWMutexType::ReadLock lock(GetMutex());
    // 返回所有变量（是一个map，key是string，value是父类的智能指针）
    ConfigVarMap &m = GetDatas();
    for (auto it = m.begin(); it != m.end(); ++it)
    {
        cb(it->second);
    }
}

} // namespace sylar
