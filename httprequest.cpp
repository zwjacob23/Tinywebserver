#include "httprequest.h"
using namespace std;

const unordered_set<string> HttpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };

void HttpRequest::Init() {
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

// 解析处理
bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";      // 行结束符标志(回车换行)
    if(buff.ReadableBytes() <= 0) { // 没有可读的字节
        return false;
    }
    // 读取数据
    while(buff.ReadableBytes() && state_ != FINISH) {
        // 从buff中的读指针开始到读指针结束，这块区域是未读取得数据并去处"\r\n"，返回有效数据得行末指针
        //从目前可读的到可读的尽头中，找到CRLF的位置，返回CRLF位置的指针
        //会一行一行的读，第一行读了肯定是请求行，然后请求行处理完了就处理请求头部
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        // 转化为string类型
        std::string line(buff.Peek(), lineEnd);//每一行都是要单独处理的吗？
        switch(state_)
        {
        /*
            有限状态机，从请求行开始，每处理完后会自动转入到下一个状态    
        */
        case REQUEST_LINE:
            if(!ParseRequestLine_(line)) {
                return false;
            }
            ParsePath_();   // 解析路径
            break;    
        case HEADERS:
            ParseHeader_(line);
            if(buff.ReadableBytes() <= 2) { 
                state_ = FINISH;
            }
            break;
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.BeginWrite()) { break; } // 读完了
        buff.RetrieveUntil(lineEnd + 2);        // 跳过回车换行
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

// 解析路径
void HttpRequest::ParsePath_() {
    if(path_ == "/") {
        path_ = "/index.html"; //如果用户输入的是/那么就是要访问/index.html
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == path_) {
                path_ += ".html";//或者有其他几项可能列在了HTML状态表里了
                break;
            }
        }
    }
}

//解析请求行
bool HttpRequest::ParseRequestLine_(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$"); 
    //请求行一般是这样的：GET / HTTP/1.1
    //反正就是通过正则表达式来将buffer里面收到的请求数据包进行匹配
    //匹配处需要的method字段，path字段和version字段。
    //解析完成后就将状态改为HEADERS准备继续解析后面的请求头部
    smatch subMatch;
    // 在匹配规则中，以括号()的方式来划分组别 一共三个括号 [0]表示整体
    if(regex_match(line, subMatch, patten)) {      // 匹配指定字符串整体是否符合
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;   // 状态转换为下一个状态
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

void HttpRequest::ParseHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");//用正则表达式来获得需要的字段
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {//如果能匹配到，如果匹配不到了就处理body的部分
        header_[subMatch[1]] = subMatch[2];
    }
    else {
        state_ = BODY;  // 状态转换为下一个状态
    }
}

void HttpRequest::ParseBody_(const string& line) {//这个时候line一行包含了所有信息了
    body_ = line;
    ParsePost_();
    state_ = FINISH;    // 状态转换为下一个状态
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

// 16进制转化为10进制
int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

// 处理post请求
// 就是用户要把自己想说的话写在了这个数据包里面，我们需要将其提取出来。

void HttpRequest::ParsePost_() {//只有POST才会有body部分的信息
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        //header_["Content-Type"]这个是一个头部字段，他描述请求或者响应中的数据格式
        //如果是"application/x-www-form-urlencoded"，就以位置请求的数据是URL编码格式
        ParseFromUrlencoded_();     // POST请求体示例
        //基于上面这个函数将body里面的字段进行进一步的分解，判断这个数据包到底想干嘛哦
        //用body来填充post_表信息。填完之后再判断是不是要登录或者注册

        if(DEFAULT_HTML_TAG.count(path_)) { // 如果是登录/注册的path，常用.count+哈希表的方式判断有无出现
            int tag = DEFAULT_HTML_TAG.find(path_)->second; 
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);  // 为1则是登录
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";//这个改path是什么意思？
                } 
                else {
                    path_ = "/error.html";
                }
            }
        }
    }   
}

// 从url中解析编码
void HttpRequest::ParseFromUrlencoded_() {
    if(body_.size() == 0) { return; }

    string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;

    for(; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
        // key
        case '=':
            key = body_.substr(j, i - j);
            j = i + 1;
            break;
        // 键值对中的空格换为+或者%20
        case '+':
            body_[i] = ' ';
            break;
        case '%':
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
            body_[i + 2] = num % 10 + '0';
            body_[i + 1] = num / 10 + '0';
            i += 2;
            break;
        // 键值对连接符
        case '&':
            value = body_.substr(j, i - j);
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);//最后一次的value提取：
    if(post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == "") { return false; }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql;//获得一个指向某个具体的数据库的指针在下面这个函数中，但需要时mysql*的格式
    //conn会返回一个同样类型的数据给sql
    SqlConnRAII(&sql,  SqlConnPool::Instance());//在连接池中为对应的指针赋值。
    assert(sql);
    
    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };//查询的命令
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;//查询的结果集
    
    if(!isLogin) { flag = true; }
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    //将指定用户名和密码存到order里面去
    LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) { //在数据库中根据order命令查询到了对应的用户名和密码就返回0
        mysql_free_result(res);
        return false; 
    }
    res = mysql_store_result(sql);//将查询的结果记录再res中
    j = mysql_num_fields(res);//应该返回了两列
    fields = mysql_fetch_fields(res);

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 登录行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { flag = true; }
            else {
                flag = false;
                LOG_INFO("pwd error!");
            }
        } 
        else { 
            flag = false; 
            LOG_INFO("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) { //将新的用户名和密码存到数据库里面去
            LOG_DEBUG( "Insert error!");
            flag = false; 
        }
        flag = true;
    }
    // SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}
//根据给定的key返回body字段中对应的value
std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}
