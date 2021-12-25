#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"


//http报文处理流程
//浏览器发出http连接请求，主线程创建一个http接收对象接收请求冰将所有数据读入对应的buffer,将改对象插入任务队列(请求队列)，工作线程从任务队列中取出一个任务进行处理
class http_conn
{
public:
    static const int FILENAME_LEN = 200;//设置读取文件的名称m_file大小
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //本项目只用到get和post
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,//检查请求行
        CHECK_STATE_HEADER,         //检查头部
        CHECK_STATE_CONTENT         //检查内容
    };
    enum HTTP_CODE
    {
        NO_REQUEST,//请求不完善，需要读取更多数据
        GET_REQUEST,//得到了一个完整的请求
        BAD_REQUEST,//客户请求出现语法错误
        NO_RESOURCE,//客户访问资源不存在
        FORBIDDEN_REQUEST,//客户对资源没有访问权限
        FILE_REQUEST,//表示请求文件？
        INTERNAL_ERROR,//服务器内部错误
        CLOSED_CONNECTION//客户已经关闭连接了
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    //各子线程通过process()对任务进行处理，调用process_read()和process_write()分别完成报文响应和报文解析的任务
    void process();
    //读取浏览器发来的全部数据，直到无数据可读或对方关闭连接，读到m_read_buf中，更新m_read_idx
    bool read_once();
    //响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //CGI使用线程池初始化数据库读取表
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();
    //从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    //向m_write_buf写入数据
    bool process_write(HTTP_CODE ret);
    //主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    //主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    //主状态机解析报文中的请求内容数据
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();
    //m_start_line是已经解析的字符数
    //get_line()用于指针向后偏移指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
    //从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    void unmap();

    //根据响应报文的格式，生成对应的八个部分，以下函数均由do_request()调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;//所有socket上的事件都被注册到同一个epoll内核事件表中，所有将epoll文件描述符设置为静态的
    static int m_user_count;//统计用户数量
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;//该http连接的socket和对方的socket地址
    sockaddr_in m_address;
    
    char m_read_buf[READ_BUFFER_SIZE];//储存读取的请求报文的数据
    int m_read_idx;//表示读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;//当前正在分析的字符在读缓冲区的位置
    int m_start_line;//m_read_buf中已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE];//储存发出的响应报文数据
    int m_write_idx;//写缓冲区中待发送的字节数
    CHECK_STATE m_check_state;//主状态机当前所处的状态
    METHOD m_method;

    //以下为解析请求报文中对应的六个变量
    char m_real_file[FILENAME_LEN];//客户请求的目标文件的完整路径，其内容等于doc_root + m_url,doc_root是网站根目录
    char *m_url;//客户请求的目标文件的文件名
    char *m_version;//http协议版本号
    char *m_host;//主机名
    int m_content_length;//http请求消息体的长度
    bool m_linger;//http是否要保持连接


    char *m_file_address;//客户请求的目标文件被mmap到内存中的起始位置。用内存读写取代I/O读写，提高了文件的读取效率
    struct stat m_file_stat;//目标文件的状态
    struct iovec m_iv[2];//使用write_v来执行写操作
    int m_iv_count;//被写内存块的数量
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;//剩余发送字节数
    int bytes_have_send;//已发送字节数
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;//LT,ET标志
    int m_close_log;//日志开关

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
