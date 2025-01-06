#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "log.h"


//连接池，在项目开始的时候就要建立许多个和数据库的连接，以提高我们访问数据库的效率

//连接的是mysql数据库，但是访问的是数据库里的哪个database呢？
class SqlConnPool {
public:
    static SqlConnPool *Instance();

    MYSQL *GetConn();//连接数据库的指针
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();
    //初始化的时候要给访问数据库的账号和密码
    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);
    void ClosePool();

private:
    SqlConnPool() = default;//允许默认构造函数
    ~SqlConnPool() { ClosePool(); }

    int MAX_CONN_;

    std::queue<MYSQL *> connQue_;//这是存放指向数据库的指针的
    std::mutex mtx_;//池子是公共资源，所以也需要这个锁。
    sem_t semId_;
};

/* 资源在对象构造初始化 资源在对象析构时释放*/
class SqlConnRAII {//这是我们最终调用的对象，为了更合理的构造和销毁对应的计算机资源。
public:
    //将建立的池子和我们的数据库建立连接
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);
        *sql = connpool->GetConn();//返回我们池子里面指向数据库的指针
        sql_ = *sql;//将我们的指针指向这个里面的指针。？到底是谁传给谁啊
        connpool_ = connpool;
    }
    
    ~SqlConnRAII() {
        if(sql_) { connpool_->FreeConn(sql_); }
    }
    
private:
    MYSQL *sql_;
    SqlConnPool* connpool_;
};

#endif // SQLCONNPOOL_H
