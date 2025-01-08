#include "webserver.h"

using namespace std;
//在初始化的时候就要注意一下的初始化：
//工作目录srcDir，方便之后去处理请求的资源
//连接池也要提前准备好SqlConnPool::Instance()->Init（）；
//开启日志
WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256);//获得目前的工作目录？
    assert(srcDir_);
    strcat(srcDir_, "/resources/");
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    // 初始化操作
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);  // 连接池单例的初始化
    // 初始化事件和初始化socket(监听)
    InitEventMode_(trigMode);//事件类型一般要设置为ET模式
    if(!InitSocket_()) { isClose_ = true;}

    // 是否打开日志标志
    if(openLog) {//开启日志
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer() {
    close(listenFd_);//关闭监听的fd
    isClose_ = true;
    free(srcDir_);//释放掉
    SqlConnPool::Instance()->ClosePool();//关闭连接池
}

//线程池是在哪里开的呢？

//不同的世界设置不等同的事件触发模式（读写应该都设置为边缘触发ET模式）
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;    // 检测socket关闭
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;     // EPOLLONESHOT由一个线程处理
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    while(!isClose_) {//持续打开webserver
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();     // 获取下一次的超时等待事件(至少这个时间才会有用户过期，每次关闭超时连接则需要有新的请求进来)
        }//每次循环开始的时候先处理最快到期的定时器事件，然后wait的事件小于这个即将到期的时间。
        int eventCnt = epoller_->Wait(timeMS);//等待对应长度的时间
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if(fd == listenFd_) {
                DealListen_();
            }
            //如果是对应的读、写事件那么就交给对应的线程去做（这个线程在deal函数里面）
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client) {//关闭一个连接，那么就是要从红黑树上删除。
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

void WebServer::AddClient_(int fd, sockaddr_in addr) {//增加一个客人，被addfd函数调用，用于添加定时器
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if(timeoutMS_ > 0) {//这里主要是给新增的客人贴上一个定时检验的定时器，避免长时间不联系（kindof长连接）
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonblock(fd);//设置非阻塞
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

// 处理监听套接字，主要逻辑是accept新的套接字，并加入timer和epoller中
void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}
        else if(HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr);//添加定时器定时检查这个连接状态
    } while(listenEvent_ & EPOLLET);
}

// 处理读事件，主要逻辑是将OnRead加入线程池的任务队列中
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);//收到了这个客户的消息，要重新给这个客户计时
    //使用bind将function类型绑定了一些参数成为了一个仿函数然后传递到线程池中去
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client)); // 这是一个右值，bind将参数和函数绑定
    //threadpool是一个比较独立的过程，他自己开了很多线程，你要让他做事就直接将任务放在这个类的task参数里面就可以了。
}

// 处理写事件，主要逻辑是将OnWrite加入线程池的任务队列中
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

void WebServer::OnRead_(HttpConn* client) {//实际上对于读缓冲区里的操作。应该是先读到自己的buffer中，再解析
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);         // 读取客户端套接字的数据，读到httpconn的读缓存区
    if(ret <= 0 && readErrno != EAGAIN) {   // 读异常就关闭客户端
        CloseConn_(client);
        return;
    }
    // 业务逻辑的处理（先读后处理）
    OnProcess(client);
}

/* 处理读（请求）数据的函数 */
void WebServer::OnProcess(HttpConn* client) {
    // 首先调用process()进行逻辑处理，解析，生成响应都在这个函数里面执行
    if(client->process()) { // 根据返回的信息重新将fd置为EPOLLOUT（写）或EPOLLIN（读）
    //读完事件就跟内核说可以写了
    //读完如果有东西被放到缓冲区了，那么说明什么呢？说明有东西要还给客户，所以要把监测的事件改为写
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);    // 响应成功，修改监听事件为写,等待OnWrite_()发送
    } else {
    //写完事件就跟内核说可以读了
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client) {//将我们自己缓冲区的东西读给fd。这是再下一次循环的时候检测到写缓存区可以写才调用的
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);//写的时候主要是一个响应头部和一个响应数据，是两个指针方便操作
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            // OnProcess(client);
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN); // 回归换成监测读事件
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {  // 缓冲区满了 
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    // 优雅关闭
    {
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 绑定
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));//将监听文件描述符绑定一个固定的端口
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 监听
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);  // 将监听套接字加入epoller
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);   
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 设置非阻塞
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
