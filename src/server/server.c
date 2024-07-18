#include <stdio.h>
#include <stdlib.h>
#include <proto.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "proto.h"

#include "server_conf.h"
#include "medialib.h"
#include "thr_channel.h"
#include "thr_list.h"

int serverSd;  // 不能使用static 修饰，否则其链接性变为内部
struct sockaddr_in sndaddr;  // 存储远端ip信息，对于服务端需要将数据包发送至多播组，远端即多播组
static struct mlib_listentry_st *list;  //

/*
 * -M   指定多播组
 * -P   指定接收端口
 * -F   前台运行
 * -D   指定媒体库路径
 * -I   指定网络设备
 * -H   显示帮助
 */
// 服务端结构体字段初始化
struct server_conf_st server_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .mgroup = DEFAULT_MGROUP,
    .media_dir = DEFAULT_MEDIADIR,
    .runmode = RUN_DAEMON,
    .ifname = DEFAULT_IF,
};

/*打印帮助数据*/
static void printhelp(void)
{
    printf("-M   指定多播组\n");
    printf("-P   指定接收端口\n");
    printf("-F   前台运行\n");
    printf("-D   指定媒体库路径\n");
    printf("-I   指定网络设备\n");
    printf("-H   显示帮助\n");
}


/*守护进程退出函数*/
static void daemon_exit(int s)
{
    thr_list_destroy();    // 销毁节目单线程
    thr_channel_destroyall();  // 销毁各个频道线程
    mlib_freechnlist(list);    // 销毁节目单结构体数据
    if (s < 0) {
        syslog(LOG_ERR, "Daemon failure exit.");
        exit(1);
    }
    syslog(LOG_INFO, "Signal-%d caught, exit now.", s);

    closelog();  // 关闭系统日志
    exit(0);  // 退出守护进程
}


/*创建守护进程，按照守护进程的流程去创建即可*/
static int daemonize(void)
{
    pid_t pid;
    int fd;

    pid = fork();

    if(pid < 0)
    {
        // 将报错写到日志中
        syslog(LOG_ERR, "fork():%s", strerror(errno));
        return -1;
    }

    // 父进程退出
    else if(pid > 0){
        printf("PPID Exit.\n");
        printf("daemon  PID : %d\n",pid);
        exit(0);
    }


    // 子进程，创建新的会话，并且成为会话的leader
    // 并且重定向或者关闭标准的输入输出流
    else{
        // 打开空设备，这个是一个特殊的设备
        // 只要把数据扔到这个特殊的设备文件/dev/null中, 数据被被销毁了
        fd = open("/dev/null",O_RDWR);
        if(fd < 0)
        {
            // 将报错写到系统日志中
            syslog(LOG_WARNING, "open():%s", strerror(errno));
            return -2;
        }
        // 标准的输入输出流，错误流重定向到空设备中
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);

        if(fd > 2)
            close(2);

        // 开启一个新的会话，使该子进程成为会话领头进程，脱离所有终端控制
        setsid();
        // 在系统日志中输出，告知已经创建好守护进程
        syslog(LOG_INFO, "Daemon initialized OK");
        // 改变当前进程的工作路经，工作路径需要为一个绝对有的路径，以避免守护进程持续占用任何挂载点
        chdir("/");
        // 改变文件模式掩码（umask）,设置 umask 为 0 以确保守护进程可以读写其创建的任何文件，且不受继承的文件模式掩码的限制
        umask(0);
        return 0;
    }
}


/*套接字初始化函数，设置套接字属性*/
static void socket_init(void)
{
    // 创建UDP套接字,用于向多播组ip发送数据
    serverSd = socket(AF_INET,SOCK_DGRAM,0);
    if(serverSd < 0)
    {
        // 套接字创建失败，向系统日志发送信息
        syslog(LOG_ERR, "socket():%s", strerror(errno));
        exit(1);
    }

    // 设置套接字属性，建立多播组
    // 指定用于发送多播数据包的网络接口（指定机器的那一个网卡设备用于发送多播数据）
    struct ip_mreqn mreq;
    inet_pton(AF_INET, server_conf.mgroup, &mreq.imr_multiaddr);  // 多播组ip
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);         // 网络接口IP
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);        // 网卡设备索引
    // 在IP层，设置属性名 IP_MULTICAST_IF 建立多播组
    if (setsockopt(serverSd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
    {
        syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF):%s", strerror(errno));
        exit(1);
    }

    // 定义远端ip信息--服务端需要将数据包发送至多播组，因此多播组ip就是远端ip
    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET,server_conf.mgroup,&sndaddr.sin_addr);
    // printf("%s %d\n",__FUNCTION__, __LINE__);  调试
}


int main(int argc,char *argv[])
{
    int c;

    struct sigaction sa;

    sa.sa_handler = daemon_exit;  // 进程捕获到这些信号的处理函数为守护进程退出函数
    sigemptyset(&sa.sa_mask);     // sa.sa_mask在信号处理函数执行时，额外需要阻塞的信号集合
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGQUIT);
    // 上面三行将三个信号添加进入了，需要阻塞的信号集合中
    // 在执行 daemon_exit 函数时，上述的三个信号将被自动阻塞
    // 意味着如果在 daemon_exit 执行期间，程序再次收到这些信号，这些信号不会打断当前的信号处理函数，而是待处理函数执行完毕后再决定如何处理这些阻塞的信号
    // 这防止了信号处理过程中的递归或重入问题，从而使程序的行为更为可预测和稳定
    // 意味着当处理这些信号的 daemon_exit 函数正在执行时，这三个信号将不会再次被递送，从而阻止了信号处理函数的中途被相同信号中断

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    // 为 SIGTERM、SIGINT 和 SIGQUIT 信号分别设置了新的处理动作，即用 sa 结构体中定义的处理方式。
    // 即进程捕获到了SIGTERM、SIGINT 和 SIGQUIT 就会执行 daemon_exit 函数


    // 打开系统日志
    // 参数1：程序的名称，帮助在日志文件中区分来自不同应用程序的日志消息
    // 参数2：参数是一个整数，用于指定日志操作的选项
    // 参数3：指定日志消息的默认设施。设施参数用于指定日志消息的种类
    openlog("IPV4_StreamingMedia", LOG_PID|LOG_PERROR, LOG_DAEMON);


    /*1. 使用getopt()，进行命令行分析*/
    while(1)
    {
        // extern char *optarg 是与getopt()有关的全局变量，会自动指向命令行当前带参数选项，后面的参数
        c = getopt(argc,argv,"M:P:FD:I:H");
        if(c < 0)
            break;
        switch (c) {
            case 'M':
                server_conf.mgroup = optarg;
                break;
            case 'P':
                server_conf.rcvport = optarg;
                break;
            case 'F':
                server_conf.runmode = RUN_FOREGROUND;
                break;
            case 'D':
                server_conf.media_dir = optarg;
                break;
            case 'I':
                server_conf.ifname = optarg;
                break;
            case 'H':
                printhelp();
                exit(0);
                break;
            default:
                abort();
                break;
        }
    }
    /****************************************************************************************/


    /*2. 守护进程的实现*/
    // 命令行选项指定server作为一个守护进程运行
    if(server_conf.runmode == RUN_DAEMON)
    {
        if(daemonize() != 0)  // 将server端进程转化为守护进程
            exit(1);
    }
    else if(server_conf.runmode == RUN_FOREGROUND)
    {
        // do nothing
    }
    else
    {
        // 向系统日志发送，级别为LOG_ERR
        syslog(LOG_ERR, "EINVAL server_conf.runmode.");
        exit(1);
    }
    /****************************************************************************************/


    /*3. SOCKET初始化，设置套接字属性，创建多播组*/
    socket_init();

    /*4. 获取频道信息*/
    struct mlib_listentry_st *list; // 媒体库中，频道信息结构体
    int list_size;  // 节目单上的频道数量(媒体库中有效的频道数量)
    int err;
    // if error
    err = mlib_getchnlist(&list,&list_size);  // 获取节目单信息
    if(err)
    {
        // 媒体库中节目单信息获取失败
        syslog(LOG_ERR,"mlib_getchnlist() failed in main.c.");
        exit(1);
    }

    // 向系统日志发送调试信息，节目单中频道数量
    syslog(LOG_DEBUG,"channel sizes = %d",list_size);

    /*5. 创建频道线程*/
    // 创建100个频道线程，一个频道对应一个线程，此处创建线程数量要考虑机器所能创建线程数量的上限
    int i;
    for(i = 0;i<list_size;i++)
    {
        err = thr_channel_create(list+i);
        if(err)
        {
            syslog(LOG_ERR,"thr_channel_create() failed.");
            exit(1);
        }
    }
    // 向系统日志发送调试信息
    syslog(LOG_DEBUG,"%d channel threads create.",i);


    /*6. 创建节目单线程*/
    err = thr_list_create(list,list_size);
    if(err)
    {
        syslog(LOG_ERR,"thr_list_create() failed.");
        exit(1);
    }
    // 向系统日志发送调试信息
    syslog(LOG_DEBUG,"the channel_list threads create.");


    while(1)
        // 使用kill [daemon_pid] 发送信号，可以打断pause,并且去执行daemon_exit函数这样就退出了守护进程
        pause();  // 将进程挂起

    exit(0);   // 执行不到
}