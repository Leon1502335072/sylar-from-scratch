#include "bytearray.h"
#include <fstream>
#include <sstream>
#include <string.h>
#include <iomanip>
#include <cmath>

#include "endian.h"
#include "log.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ByteArray::Node::Node(size_t s)
    :ptr(new char[s])
    ,next(nullptr)
    ,size(s) {
}

ByteArray::Node::Node()
    :ptr(nullptr)
    ,next(nullptr)
    ,size(0) {
}

ByteArray::Node::~Node() 
{
    if(ptr) 
    {
        delete[] ptr;
    }
}

ByteArray::ByteArray(size_t base_size)
    :m_baseSize(base_size)
    ,m_position(0)
    ,m_capacity(base_size)
    ,m_size(0)
    ,m_endian(SYLAR_BIG_ENDIAN)
    ,m_root(new Node(base_size))
    ,m_cur(m_root) {
    // 构造时就创建了一个内存块
}

ByteArray::~ByteArray() 
{
    Node* tmp = m_root;
    while(tmp) 
    {
        m_cur = tmp;
        tmp = tmp->next;
        delete m_cur;
    }
}

bool ByteArray::isLittleEndian() const 
{
    return m_endian == SYLAR_LITTLE_ENDIAN;
}

void ByteArray::setIsLittleEndian(bool val) 
{
    if(val) 
    {
        m_endian = SYLAR_LITTLE_ENDIAN;
    } 
    else 
    {
        m_endian = SYLAR_BIG_ENDIAN;
    }
}

void ByteArray::writeFint8  (int8_t value) 
{
    // 因为就写一个字节的数据所以不需要考虑字节序
    write(&value, sizeof(value));
}

void ByteArray::writeFuint8 (uint8_t value) 
{
    write(&value, sizeof(value));
}
void ByteArray::writeFint16 (int16_t value) 
{
    // 不等于我机器上的字节序，需要转一下再写
    if(m_endian != SYLAR_BYTE_ORDER) 
    {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFuint16(uint16_t value) 
{
    if(m_endian != SYLAR_BYTE_ORDER) 
    {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFint32 (int32_t value) 
{
    if(m_endian != SYLAR_BYTE_ORDER) 
    {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFuint32(uint32_t value) 
{
    if(m_endian != SYLAR_BYTE_ORDER) 
    {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFint64 (int64_t value) 
{
    if(m_endian != SYLAR_BYTE_ORDER) 
    {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFuint64(uint64_t value) 
{
    if(m_endian != SYLAR_BYTE_ORDER) 
    {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

// 将int32_t类型的数据用zigzag算法编码：1 -> 2, -1 -> 1, -1000 -> 1999
static uint32_t EncodeZigzag32(const int32_t& v) 
{
    if(v < 0) 
    {
        return ((uint32_t)(-v)) * 2 - 1;
    } 
    else 
    {
        return v * 2;
    }
}

static uint64_t EncodeZigzag64(const int64_t& v) 
{
    if(v < 0) 
    {
        return ((uint64_t)(-v)) * 2 - 1;
    } 
    else 
    {
        return v * 2;
    }
}

// 将zigzag类型的数据解码
static int32_t DecodeZigzag32(const uint32_t& v) 
{
    return (v >> 1) ^ -(v & 1);
}

static int64_t DecodeZigzag64(const uint64_t& v) 
{
    return (v >> 1) ^ -(v & 1);
}


void ByteArray::writeInt32  (int32_t value) 
{
    // 先编码成zigzag类型，然后再写
    writeUint32(EncodeZigzag32(value));
}


void ByteArray::writeUint32 (uint32_t value) 
{ 
    uint8_t tmp[5];
    uint8_t i = 0;
    while(value >= 0x80) 
    {
        // 每7个bit位取一次，所以最多需要5个字节的空间
        tmp[i++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    tmp[i++] = value;
    write(tmp, i);
}

void ByteArray::writeInt64  (int64_t value) 
{
    writeUint64(EncodeZigzag64(value));
}

void ByteArray::writeUint64 (uint64_t value) 
{
    uint8_t tmp[10];
    uint8_t i = 0;
    while(value >= 0x80) 
    {
        tmp[i++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    tmp[i++] = value;
    write(tmp, i);
}

void ByteArray::writeFloat  (float value) 
{
    // 先将float类型数据写到uint32类型的空间
    uint32_t v;
    memcpy(&v, &value, sizeof(value));
    writeFuint32(v);
}

void ByteArray::writeDouble (double value) 
{
    // 先将double类型数据写到uint64类型的空间
    uint64_t v;
    memcpy(&v, &value, sizeof(value));
    writeFuint64(v);
}

void ByteArray::writeStringF16(const std::string& value) 
{
    // 先将string对象的长度size用两字节的方式写到内存块，然后再写内容
    writeFuint16(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringF32(const std::string& value) 
{
    // 先将string对象的长度size用4字节的方式写到内存块，然后再写内容
    writeFuint32(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringF64(const std::string& value) 
{
    // 先将string对象的长度size用8字节的方式写到内存块，然后再写内容
    writeFuint64(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringVint(const std::string& value) 
{
    // value.size一般不是很大，所以压缩一下再写，然后写内容
    writeUint64(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringWithoutLength(const std::string& value) 
{
    // 不写长度，直接写内容
    write(value.c_str(), value.size());
}

int8_t   ByteArray::readFint8() 
{
    int8_t v;
    read(&v, sizeof(v));
    return v;
}

uint8_t  ByteArray::readFuint8() 
{
    uint8_t v;
    read(&v, sizeof(v));
    return v;
}

#define XX(type) \
    type v; \
    read(&v, sizeof(v)); \
    if(m_endian == SYLAR_BYTE_ORDER) { \
        return v; \
    } else { \
        return byteswap(v); \
    }

int16_t  ByteArray::readFint16() 
{
    XX(int16_t);
}
uint16_t ByteArray::readFuint16() 
{
    XX(uint16_t);
}

int32_t  ByteArray::readFint32() 
{
    XX(int32_t);
}

uint32_t ByteArray::readFuint32() 
{
    XX(uint32_t);
}

int64_t  ByteArray::readFint64() 
{
    XX(int64_t);
}

uint64_t ByteArray::readFuint64() 
{
    XX(uint64_t);
}

#undef XX

int32_t  ByteArray::readInt32() 
{
    return DecodeZigzag32(readUint32());
}

uint32_t ByteArray::readUint32() 
{
    uint32_t result = 0;
    for(int i = 0; i < 32; i += 7) 
    {
        uint8_t b = readFuint8();
        if(b < 0x80) 
        {
            result |= ((uint32_t)b) << i;
            break;
        } 
        else 
        {
            result |= (((uint32_t)(b & 0x7f)) << i);
        }
    }
    return result;
}

int64_t  ByteArray::readInt64() 
{
    return DecodeZigzag64(readUint64());
}

uint64_t ByteArray::readUint64() 
{
    uint64_t result = 0;
    for(int i = 0; i < 64; i += 7) 
    {
        uint8_t b = readFuint8();
        if(b < 0x80) 
        {
            result |= ((uint64_t)b) << i;
            break;
        } 
        else 
        {
            result |= (((uint64_t)(b & 0x7f)) << i);
        }
    }
    return result;
}

float    ByteArray::readFloat() 
{
    uint32_t v = readFuint32();
    float value;
    memcpy(&value, &v, sizeof(v));
    return value;
}

double   ByteArray::readDouble()
{
    uint64_t v = readFuint64();
    double value;
    memcpy(&value, &v, sizeof(v));
    return value;
}

std::string ByteArray::readStringF16() 
{
    uint16_t len = readFuint16();
    std::string buff;
    buff.resize(len);
    // 这点在上面读出长度后m_position也就指向了读出长度后的位置，然后在从m_position往后读len字节的数据
    read(&buff[0], len);
    return buff;
}

std::string ByteArray::readStringF32() 
{
    uint32_t len = readFuint32();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

std::string ByteArray::readStringF64() 
{
    uint64_t len = readFuint64();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

std::string ByteArray::readStringVint() 
{
    uint64_t len = readUint64();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

void ByteArray::clear() 
{
    m_position = m_size = 0;
    m_capacity = m_baseSize;
    Node* tmp = m_root->next;
    while(tmp) 
    {
        m_cur = tmp;
        tmp = tmp->next;
        delete m_cur;
    }
    m_cur = m_root;
    m_root->next = NULL;
}

void ByteArray::write(const void* buf, size_t size) 
{
    if(size == 0) 
    {
        return;
    }
    addCapacity(size);
    // 取当前块的写入位置或者说计算当前块已写入的数据量位置
    size_t npos = m_position % m_baseSize;
    // 计算当前块剩余空间
    size_t ncap = m_cur->size - npos;
    // 记录buf中已写入的数据量
    size_t bpos = 0;

    while(size > 0) 
    {
        // 当前块的剩余空间足够容纳
        if(ncap >= size) 
        {
            // 目标空间，源空间，写入的量（长度）
            memcpy(m_cur->ptr + npos, (const char*)buf + bpos, size);
            if(m_cur->size == (npos + size)) 
            {
                m_cur = m_cur->next;
            }
            m_position += size;
            bpos += size;
            size = 0;
        } 
        else // 当前块的空间放不下，则将当前块的剩余空间用完
        {
            memcpy(m_cur->ptr + npos, (const char*)buf + bpos, ncap);
            m_position += ncap;
            bpos += ncap;
            size -= ncap;
            m_cur = m_cur->next;
            ncap = m_cur->size;
            npos = 0;
        }
    }

    if(m_position > m_size) 
    {
        m_size = m_position;
    }
}

void ByteArray::read(void* buf, size_t size) 
{
    if(size > getReadSize()) 
    {
        throw std::out_of_range("not enough len");
    }

    size_t npos = m_position % m_baseSize;
    // 距离当前块边界还有多少数据量
    size_t ncap = m_cur->size - npos;
    // 记录已读入buf的字节数
    size_t bpos = 0;
    while(size > 0) 
    {
        // 如果当前块中的数据都读不完或者刚好读完
        if(ncap >= size) 
        {
            // 目标空间，源空间，写入的量
            memcpy((char*)buf + bpos, m_cur->ptr + npos, size);
            if(m_cur->size == (npos + size)) 
            {
                m_cur = m_cur->next;
            }
            m_position += size;
            bpos += size;
            size = 0;
        } 
        else 
        {
            memcpy((char*)buf + bpos, m_cur->ptr + npos, ncap);
            m_position += ncap;
            bpos += ncap;
            size -= ncap;
            m_cur = m_cur->next;
            ncap = m_cur->size;
            npos = 0;
        }
    }
}

void ByteArray::read(void* buf, size_t size, size_t position) const 
{
    if(size > (m_size - position)) 
    {
        throw std::out_of_range("not enough len");
    }

    size_t npos = position % m_baseSize;
    size_t ncap = m_cur->size - npos;
    size_t bpos = 0;
    Node* cur = m_cur;
    while(size > 0) 
    {
        if(ncap >= size) 
        {
            memcpy((char*)buf + bpos, cur->ptr + npos, size);
            if(cur->size == (npos + size)) 
            {
                cur = cur->next;
            }
            position += size;
            bpos += size;
            size = 0;
        } 
        else 
        {
            memcpy((char*)buf + bpos, cur->ptr + npos, ncap);
            position += ncap;
            bpos += ncap;
            size -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
    }
}

void ByteArray::setPosition(size_t v) 
{
    if(v > m_capacity) 
    {
        throw std::out_of_range("set_position out of range");
    }
    m_position = v;
    if(m_position > m_size) 
    {
        m_size = m_position;
    }
    
    // 当m_position被设置成新的值以后，m_cur从链表头开始找m_position在哪个块里
    m_cur = m_root;
    while(v > m_cur->size) 
    {
        v -= m_cur->size;
        m_cur = m_cur->next;
    }
    // 如果m_position正好在一个块的最后边界，那么m_cur指向m_position的下一个块
    if(v == m_cur->size) 
    {
        m_cur = m_cur->next;
    }
}

bool ByteArray::writeToFile(const std::string& name) const 
{
    // ofstream类有一个open方法成员，就是用这个方法来打开文件的
    std::ofstream ofs;
    // ios::trunc 如果文件存在，把文件长度设为0，ios::binary：以二进制方式打开文件，缺省的方式是文本方式
    ofs.open(name, std::ios::trunc | std::ios::binary);
    if(!ofs) 
    {
        // 打开失败写日志
        SYLAR_LOG_ERROR(g_logger) << "writeToFile name=" << name
            << " error , errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    // read_size = m_size - m_position
    int64_t read_size = getReadSize();
    int64_t pos = m_position;
    Node* cur = m_cur;

    while(read_size > 0) 
    {
        int diff = pos % m_baseSize;
        // 这行有点问题！！！
        int64_t len = (read_size > (int64_t)m_baseSize ? m_baseSize : read_size) - diff;
        ofs.write(cur->ptr + diff, len);
        cur = cur->next;
        pos += len;
        read_size -= len;
    }
    // 这一段是自己的逻辑
    // int diff = pos % m_baseSize;
    // while (read_size > 0) 
    // {
    //     if(read_size<=(m_baseSize-diff))
    //     {
    //         ofs.write(cur->ptr + diff, read_size);
    //         break;
    //     } 
    //     else 
    //     {
    //         int64_t len = m_baseSize - diff;
    //         ofs.write(cur->ptr + diff, len);
    //         read_size -= len;
    //         cur = cur->next;
    //         diff = 0;
    //     }
    // }

    return true;
}

bool ByteArray::readFromFile(const std::string& name) 
{
    std::ifstream ifs;
    ifs.open(name, std::ios::binary);
    if(!ifs) 
    {
        SYLAR_LOG_ERROR(g_logger) << "readFromFile name=" << name
            << " error, errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    // share_ptr(T* buf, function<void(T* ptr)> cb);
    std::shared_ptr<char> buff(new char[m_baseSize], [](char *ptr) { delete[] ptr; });
    while(!ifs.eof()) 
    {
        // 先读到buf
        ifs.read(buff.get(), m_baseSize);
        // 在从buf写到内存块
        write(buff.get(), ifs.gcount());
    }
    return true;
}

void ByteArray::addCapacity(size_t size) 
{
    if(size == 0) 
    {
        return;
    }
    size_t old_cap = getCapacity();
    if(old_cap >= size) 
    {
        return;
    }

    // 计算减去老的空间后还需要多少空间
    size = size - old_cap;
    // 向上取整
    size_t count = ceil(1.0 * size / m_baseSize);
    Node* tmp = m_root;
    while(tmp->next) 
    {
        tmp = tmp->next;
    }

    Node* first = NULL;
    for(size_t i = 0; i < count; ++i) 
    {
        tmp->next = new Node(m_baseSize);
        if(first == NULL) 
        {
            first = tmp->next;
        }
        tmp = tmp->next;
        // 变更总容量
        m_capacity += m_baseSize;
    }

    if(old_cap == 0) 
    {
        // m_cur = m_cur->next;
        m_cur = first;
    }
}

std::string ByteArray::toString() const 
{
    std::string str;
    str.resize(getReadSize());
    if(str.empty()) 
    {
        return str;
    }
    read(&str[0], str.size(), m_position);
    return str;
}

std::string ByteArray::toHexString() const 
{
    std::string str = toString();
    std::stringstream ss;

    for(size_t i = 0; i < str.size(); ++i) 
    {
        // 每读入32个字符就换行
        if(i > 0 && i % 32 == 0) 
        {
            ss << std::endl;
        }
        // std::setw ：需要填充多少个字符,默认填充的字符为' '空格
        // std::setfill：设置std::setw的默认填充字符
        ss << std::setw(2) << std::setfill('0') << std::hex
           << (int)(uint8_t)str[i] << " ";
    }

    return ss.str();
}


uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len) const 
{
    len = len > getReadSize() ? getReadSize() : len;
    if(len == 0) 
    {
        return 0;
    }

    uint64_t size = len;

    size_t npos = m_position % m_baseSize;
    size_t ncap = m_cur->size - npos;
    struct iovec iov;
    Node* cur = m_cur;

    while(len > 0) 
    {
        if(ncap >= len) 
        {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } 
        else 
        {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers
                                ,uint64_t len, uint64_t position) const 
{
    len = len > getReadSize() ? getReadSize() : len;
    if(len == 0) 
    {
        return 0;
    }

    uint64_t size = len;

    size_t npos = position % m_baseSize;
    size_t count = position / m_baseSize;
    Node* cur = m_root;
    while(count > 0) 
    {
        cur = cur->next;
        --count;
    }

    size_t ncap = cur->size - npos;
    struct iovec iov;
    while(len > 0) 
    {
        if(ncap >= len) 
        {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } 
        else 
        {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getWriteBuffers(std::vector<iovec>& buffers, uint64_t len) 
{
    if(len == 0) 
    {
        return 0;
    }
    addCapacity(len);
    uint64_t size = len;

    size_t npos = m_position % m_baseSize;
    size_t ncap = m_cur->size - npos;
    // 两个成员：开始地址，长度
    struct iovec iov;
    Node* cur = m_cur;
    while(len > 0) 
    {
        if(ncap >= len) 
        {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } 
        else 
        {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;

            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

}
