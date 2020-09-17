#include "./http/http_conn.h"
#include "./lock/lock.h"
#include "./threadpool/pool.h"
#include "./time/m_time.h"
#include <assert.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <vector>

using namespace std;

extern void addfd(int epollfd, int socketfd);
extern void setnonblocking(int socketfd);

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int MAX_WAIT_TIME = 15;
static int pipefd[2];
static int epollfd = 0; //全局静态变量
static t_client_list m_timer_list;

template <typename T>
void destroy_user_list(T user_list) //用模板改写下，销毁两个列表
{
    int count = 0;
    for (auto i : user_list)
    {
        if (i)
        {
            count++;
            delete i;
        }
    }
    cout << "delete " << count << " items" << endl;
}

void addfd_lt(int epollfd, int socketfd)
{
    /*
        struct epoll_event 
        {
            __uint32_t events;  // Epoll events 
            epoll_data_t data;  // User data variable 
        };
        events可以是以下几个宏的集合：
        EPOLLIN ：      表示对应的文件描述符可以读（包括对端SOCKET正常关闭）；
        EPOLLOUT：      表示对应的文件描述符可以写；
        EPOLLPRI：      表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
        EPOLLERR：      表示对应的文件描述符发生错误；
        EPOLLHUP：      表示对应的文件描述符被挂断；
        EPOLLET：       将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。
        EPOLLONESHOT：  只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，
                        需要再次把这个socket加入到EPOLL队列里
        对于epoll_data_t:
        typedef union epoll_data 
        {
            void ptr;       // 指向用户自定义的数据结构
            int fd;
            __uint32_t u32;
            __uint64_t u64;
        } epoll_data_t;
    */
    epoll_event event;
    event.data.fd = socketfd;
    event.events = EPOLLIN | EPOLLRDHUP;
    /*
        int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
        对指定文件描述符fd执行op操作
        epfd:           epoll_create()的返回值
        op:             op操作，用三个宏来表示: 添加EPOLL_CTL_ADD，删除EPOLL_CTL_DEL，修改EPOLL_CTL_MOD,
                                              分别添加、删除和修改对fd的监听事件
        fd：            需要监听的fd（文件描述符）
        epoll_event：   是告诉内核需要监听什么事
    */
    epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &event);    // 添加socketfd进内核事件表中，需要监听的事件为event
}

void cb_func(client_data *c_data)
/*
    定时器回调函数，真正地关闭非活动连接，仅仅在定时器事件中被触发，不会主动调用
*/
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, c_data->sockfd, 0);
    close(c_data->sockfd);
    http_conn::m_user_count--;
}

void addsig(int sig, void(handler)(int), bool restart = true)
{
    /*
        struct sigaction
        {
            void (*sa_handler)(int);
            void (*sa_sigaction)(int, siginfo_t *, void *);
            sigset_t sa_mask;
            int sa_flags;
            void (*sa_restorer)(void);
        }
        sa_handler      一个函数指针，主要是表示接收到信号时所要采取的行动
        sa_mask         在处理该信号时暂时将sa_mask指定的信号集搁置
        sa_flags        用来设置信号处理的其他相关操作，下列的数值可用。 
        SA_RESETHAND    当调用信号处理函数时，将信号的处理函数重置为缺省值SIG_DFL
        SA_RESTART      如果信号中断了进程的某个系统调用，则系统自动启动该系统调用
        SA_NODEFER      一般情况下，当信号处理函数运行时，内核将阻塞该给定信号。
                        但是如果设置了SA_NODEFER标记，那么在该信号处理函数运行时，内核将不会阻塞该信号
    */
    struct sigaction sa;
    /*
        void * memset(void *ptr, int value, size_t num);
        Sets the first num bytes of the block of memory pointed by ptr to the specified value (interpreted as an unsigned char).
    */
    memset(&sa, '\0', sizeof(sa)); // sa这个结构体的内存全部用\0填充
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    /*
        int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
        signum参数指出要捕获的信号类型，act参数指定新的信号处理方式，oldact参数输出先前信号的处理方式（如果不为NULL的话）
    */
    sigaction(sig, &sa, NULL);
}

void sig_handler(int sig)
{
    //cout << "sig_handler get: " << sig << endl;
    int errnotemp = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0); // 写入当前是哪个信号发出的信号,管道的属性：向后面的写，前面的就读出来
    errno = errnotemp;                   // 保证可重入性
}

void timer_handler()
{
    m_timer_list.tick(); 
    // 这里是异步的吗？
    alarm(MAX_WAIT_TIME);
}

/*
    主函数中：
    1.创建线程池
    2.根据最大描述符来创建http请求数组，用来根据不同请求的不同描述符来记录各个http请求
    3.socket, bind, listen, epool_wait, 得到io事件event
    4.对于新的连接请求，接受，保存入http请求数组进行初始化
    5.对于读写请求，请求入队
*/

int main(int argc, char *agrv[])
/*
    argc            是命令行总的参数个数
    char *argv[]    是一个字符数组, 其大小是int argc, 主要用于命令行参数
                    第0个参数是程序的全名，以后的参数命令行后面跟的用户输入的参数，
*/
{
    // 从命令行中输入端口号；如果没有输入，默认端口号为12345
    int port;
    if (argc <= 1)
    {
        cout << "open default port : 12345" << endl;
        port = 12345;
    }
    else
    {
        port = atoi(agrv[1]);   // 将输入的字符串转为整型数字
    }

    // 创建服务端的监听socket，绑定地址和端口号，并允许重用本地地址和端口(结束连接后可以立即重启)
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);      
    struct sockaddr_in address;
    address.sin_addr.s_addr = htonl(INADDR_ANY);         // 将主机的无符号长整形数转换成网络字节顺序
    address.sin_family = AF_INET;                        // IPv4地址
    address.sin_port = htons(port);                      // 将一个无符号短整型数值转换为网络字节序
    
    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); 
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));

    listen(listenfd, 5);
    cout << "listen, listenfd : " << listenfd << endl;

    pool<http_conn> m_threadpool(10, 10000);             // 创建线程池对象: 线程数量10，请求数量10000
    vector<http_conn *> http_user_list(MAX_FD, nullptr); // 连接数量取决于描述符的数量,这个太奇怪了，析构什么的
    int user_count = 0;                                  // 用户数为0

    epoll_event event_list[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);                       // 创建一个文件描述符，指定内核中的事件表, 成功返回一个文件描述符，是其他所有epoll调用的句柄
    addfd_lt(epollfd, listenfd);                         // 向内核事件表中添加监听socket
    http_conn::m_epollfd = epollfd;                      // 更新http_conn类中的epoll描述符

    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);         // 创建一对匿名的套接字，用于定时器和其他信号消息的通知
    setnonblocking(pipefd[1]);                           // 写入是非阻塞的？？？？为什么
    addfd(epollfd, pipefd[0]);                           // 监听前一个管道描述符

    addsig(SIGALRM, sig_handler, false);                 // SIGALARM由alarm或setitimer设置的实时闹钟超时引起
    addsig(SIGTERM, sig_handler, false);                 // SIGTERM终止进程信号

    addsig(SIGINT, sig_handler, false);                      // SIGINT键盘输入以中断进程
    vector<client_data *> client_data_list(MAX_FD, nullptr); // 创建用户信息列表，其中包含定时器信息

    alarm(MAX_WAIT_TIME); //开始定时
    bool timeout = false;
    bool stop_server = false;
    while (!stop_server)
    {
        // epoll_wait函数如果检测到事件就绪，就将所有就绪的事件从内核事件表epollfd中复制到event_list指定的数组中
        int number = epoll_wait(epollfd, event_list, MAX_EVENT_NUMBER, -1);
        // 读取并处理就绪事件列表event_list
        for (int i = 0; i < number; ++i)
        {
            int socketfd = event_list[i].data.fd;   // 读取就绪的socket fd
            if (socketfd == listenfd)               // 如果监听完毕后
            {
                cout << "new client" << endl;
                // 新的连接
                struct sockaddr_in client_addr;
                socklen_t client_addr_length = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr *)(&client_addr), &client_addr_length); // 接受请求
                if (connfd < 0)
                {
                    printf("%s\n", strerror(errno));
                    continue; // 跳过这一轮
                }

                if (http_conn::m_user_count >= MAX_FD)
                    continue;

                if (http_user_list[connfd] == nullptr)        // 如果是新的客户端连接请求
                {
                    cout << "new http  " << connfd << endl;   // 输出新连接的文件描述符
                    http_user_list[connfd] = new http_conn(); // 创建新的连接对象
                }
                http_user_list[connfd]->init(connfd, client_addr); // 初始化连接

                // 初始化client_data
                if (client_data_list[connfd] == nullptr)
                {
                    cout << "new timer  " << connfd << endl;
                    client_data_list[connfd] = new client_data;
                }
                client_data_list[connfd]->address = client_addr;
                client_data_list[connfd]->sockfd = connfd;

                // 创建定时器
                t_client *timer = new t_client;
                timer->user_data = client_data_list[connfd];
                timer->cb_func = cb_func;
                time_t time_now = time(nullptr);
                timer->livetime = time_now + MAX_WAIT_TIME;
                client_data_list[connfd]->timer = timer;
                m_timer_list.add_timer(timer);
            }
            else if (event_list[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            /*
                EPOLLHUP：      表示对应的文件描述符被挂断；
                EPOLLERR：      表示对应的文件描述符发生错误；
                EPOLLRDHUP：    表示对端断开连接;
                出现上述几种情况时的处理措施：关闭连接，删除定时器
            */
            {
                //printf("%s\n", strerror(errno));
                http_user_list[socketfd]->close_conn("error!");
                //关闭对应的计时器
                t_client *timer = client_data_list[socketfd]->timer;
                if (timer)
                    m_timer_list.del_timer(timer);
            }
            else if ((socketfd == pipefd[0]) && (event_list[i].events & EPOLLIN)) 
            // 管道前面的描述符可写的话
            // 文件描述符可读时：接收数据
            {
                int sig;
                char signals[1024];
                int retmsg = recv(socketfd, signals, sizeof(signals), 0);
                if (retmsg <= 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < retmsg; ++i)
                    {
                        if (signals[i] == SIGALRM)
                        {
                            timeout = true;
                            break;
                        }
                        else if (signals[i] == SIGTERM || signals[i] == SIGINT)
                        {
                            stop_server = true;
                        }
                    }
                }
            }
            else if (event_list[i].events & EPOLLIN)
            {
                t_client *timer = client_data_list[socketfd]->timer;
                //客户的数据要读进来
                if (http_user_list[socketfd]->read())
                {
                    //如果读到了一些数据（已经读到了读缓存区里）,就把这个请求放到工作线程的请求队列中
                    m_threadpool.append(http_user_list[socketfd]);

                    //针对这个活跃的连接，更新他的定时器里规定的存活事件
                    if (timer)
                    {
                        time_t timenow = time(NULL);
                        timer->livetime = timenow + MAX_WAIT_TIME;
                        //cout << "adjust_timer" << endl;
                        m_timer_list.adjust_timer(timer); //调整当前timer的位置（因为很活跃）
                    }
                    else
                    {
                        cout << "did not creat timer for socket:" << socketfd << endl;
                    }
                }
                else
                {
                    //没有读到数据的话
                    http_user_list[socketfd]->close_conn("read nothing");
                    if (timer)
                        m_timer_list.del_timer(timer);
                }
            }
            else if (event_list[i].events & EPOLLOUT)
            {
                t_client *timer = client_data_list[socketfd]->timer;
                //cout << "want to write" << endl;
                if (http_user_list[socketfd]->write())
                {
                    //长连接的话，不关闭
                    if (timer)
                    {
                        cout << "long connect" << endl;
                        time_t timenow = time(NULL);
                        timer->livetime = timenow + MAX_WAIT_TIME;
                        m_timer_list.adjust_timer(timer); //调整当前timer的位置（因为很活跃）
                    }
                    else
                    {
                        cout << "did not creat timer for socket:" << socketfd << endl;
                    }
                }
                else
                {
                    //短连接，关闭连接
                    //cout << "short connect and close timer" << endl;
                    http_user_list[socketfd]->close_conn("write over");
                    if (timer)
                        m_timer_list.del_timer(timer);
                }
            }

            if (timeout)
            {
                timer_handler();
                timeout = false;
            }
        }
    }
    // 关闭服务器之后的操作
    close(epollfd);
    close(listenfd);
    destroy_user_list<vector<http_conn *>>(http_user_list);
    destroy_user_list<vector<client_data *>>(client_data_list);
    cout << "close ok" << endl;

    return 0;
}