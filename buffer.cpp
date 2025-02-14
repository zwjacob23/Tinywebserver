#include "buffer.h"

// 读写下标初始化，vector<char>初始化
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0)  {}  

// 可写的数量：buffer大小 - 写下标
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

// 可读的数量：写下标 - 读下标
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

// 可预留空间：已经读过的就没用了，等于读下标
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

const char* Buffer::Peek() const {
    
    return &buffer_[readPos_];
}

// 确保可写的长度
void Buffer::EnsureWriteable(size_t len) {
    if(len > WritableBytes()) {
        MakeSpace_(len);//从prependable里面分割一些给对应的buffer，来写外面传来的数据
    }
    assert(len <= WritableBytes());//如果makespace了都还不够数据那么就用断言来报错
}

// 移动写下标，在Append中使用
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
}

// 读取len长度，移动读下标
void Buffer::Retrieve(size_t len) {
    readPos_ += len;
}

// 读取到end位置
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek()); // end指针 - 读指针 长度
}///目前还没有读到end只读到了peak，要读到end

// 取出所有数据，buffer归零，读写下标归零,在别的函数中会用到
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size()); // 覆盖原本数据，将0开始的size()个数的字节全部清零
    readPos_ = writePos_ = 0;//右值传递，都赋值为0；
}

//读清零为什么写也要清零。读写下标怎么是在同样的起始位置。难道是相同的数据又要读又要写？
//应该是这样相当于是buffer中有个写指针快点，一直在写，然后后面有个读指针一直在追，追到了的就把位置空给写指针继续写

// 取出剩余可读的str
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());//从目前读到的最远处读到最远能读到的位置，也就是写指针的位置
    RetrieveAll();//读完之后清空缓存区
    return str;
}

// 写指针的位置
const char* Buffer::BeginWriteConst() const {
    return &buffer_[writePos_];
}

char* Buffer::BeginWrite() {
    return &buffer_[writePos_];
}
//写指针的位置为什么一个要加const 一个不加const？


// 添加str到缓冲区
void Buffer::Append(const char* str, size_t len) {
    assert(str);//len是要加入的str字符数组的长度
    EnsureWriteable(len);    // 确保可写的长度，不够就makespace，再不行就报错
    std::copy(str, str + len, BeginWrite());    // 将str放到写下标开始的地方
    HasWritten(len);    // 移动写下标
}

void Buffer::Append(const std::string& str) {//除了传入字符数组也可以传字符串
    Append(str.c_str(), str.size());
}

void Append(const void* data, size_t len) {
    Append(static_cast<const char*>(data), len);
}

// 将buffer中的读下标的地方放到该buffer中的写下标位置
void Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 将fd的内容读到缓冲区，即writable的位置
ssize_t Buffer::ReadFd(int fd, int* Errno) {
    char buff[65535];   // 栈区
    struct iovec iov[2];
    size_t writeable = WritableBytes(); // 先记录能写多少
    // 分散读， 保证数据全部读完
    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writeable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    ssize_t len = readv(fd, iov, 2);
    if(len < 0) {
        *Errno = errno;
    } else if(static_cast<size_t>(len) <= writeable) {   // 若len小于writable，说明写区可以容纳len
        writePos_ += len;   // 直接移动写下标
    } else {    
        writePos_ = buffer_.size(); // 写区写满了,下标移到最后
        Append(buff, static_cast<size_t>(len - writeable)); // 剩余的长度，写道栈里面去
    }
    return len;
}

//fd又要往writable写，又要从readable读

// 将buffer中可读的区域写出到fd中

//buffer既要作为写缓冲区也要作为读缓冲区，其中readpos作为一个偏移量记录了现在读到哪里了或者写到哪里了。


ssize_t Buffer::WriteFd(int fd, int* Errno) {
    ssize_t len = write(fd, Peek(), ReadableBytes());
    if(len < 0) {
        *Errno = errno;
        return len;
    } 
    Retrieve(len);
    return len;
}

char* Buffer::BeginPtr_() {
    return &buffer_[0];
}

const char* Buffer::BeginPtr_() const{
    return &buffer_[0];
}

// 扩展空间
void Buffer::MakeSpace_(size_t len) {
    if(WritableBytes() + PrependableBytes() < len) {
        buffer_.resize(writePos_ + len + 1);//如果剩下的仍然不够装，那么就扩容？
    } else {
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readable;//往回退readindex的距离
        assert(readable == ReadableBytes());
    }
}
