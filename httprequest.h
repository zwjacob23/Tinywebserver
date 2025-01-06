#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>    // 正则表达式
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "buffer.h"
#include "log.h"
#include "sqlconnpool.h"

class HttpRequest {
public:
    enum PARSE_STATE {//枚举类用一些字符串表达一些整数，为了代码可读性更强。并且将其封装成了一个类来使用
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };
    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);   

    std::string path() const;
    std::string& path();//返回一个字符串的引用。//这个path是什么的path？这几个返回是什么东西，是解出来的字段吗？
    std::string method() const;//const的意思是常量成员函数，不能修改任何成员变量的值
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const; 

private:
    bool ParseRequestLine_(const std::string& line);    // 处理请求行
    void ParseHeader_(const std::string& line);         // 处理请求头
    void ParseBody_(const std::string& line);           // 处理请求体

    void ParsePath_();                                  // 处理请求路径
    void ParsePost_();                                  // 处理Post事件
    void ParseFromUrlencoded_();                        // 从url种解析编码

    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);  // 用户验证

    PARSE_STATE state_;//枚举类
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
    static int ConverHex(char ch);  // 16进制转换为10进制
};

#endif
