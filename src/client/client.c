#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <fcntl.h>
#include <wait.h>
#include <errno.h>
#include <getopt.h>

#include "proto.h"
#include "client.h"


/*
 * 命令行指令结构体字段初始化
 * 为客户端指定默认的 多播组ip 接收端口 播放器（解码器）
 * */
struct client_conf_st client_conf = {
        .rcvport = DEFAULT_RCVPORT,
        .mgroup = DEFAULT_MGROUP,
        .player_cmd = DEFAULT_PLAYERCMD
};


/*
 * 短格式:-M  长格式:--mgroup   指定多播组
 * 短格式:-P  长格式:--port     指定接接收端口
 * 短格式:-p  长格式:--player   指定播放器
 * 短格式:-H  长格式:--help     显示帮助
 */
/*客户端帮助目录*/
static void printhelp(void)
{
    printf("-P  --port    指定接收端口\n");
    printf("-M  --mgroup  指定多播组\n");
    printf("-p  --player  指定播放器\n");
    printf("-H  --help    显示帮助\n");
}


/*
 * func:
 *      从缓冲区buf中，向文件描述符fd，坚持写够len个字节的数据
 * return:
 *      从buf中写入到fd的字节数
 * */
static int writen(int fd, const char *buf, size_t len)
{
    int pos = 0;
    int ret;
    while(len > 0)
    {
        ret = write(fd, buf+pos, len);
        if(ret < 0)
        {
            // 假错，则继续重新写
            if(errno == EINTR)
                continue;
            // 真错，返回-1
            perror("write()");
            return -1;
        }
        len -= ret;
        pos += ret;
    }
    return pos;
}


int main(int argc,char *argv[])
{

    // 初始化级别：
    // 默认值  <  配置文件  <  环境变量  <  命令行参数
    int sd;
    pid_t pid;
    int len;
    int chosenid; // 选择频道id
    int ret;

    struct sockaddr_in serveraddr,raddr;
    socklen_t serveraddr_len,raddr_len;

    /*1. 使用getopt_long函数，解析命令行选项*/
    int c;
    int index = 0;
    struct option argarr[] = {
            {"port",1,NULL,'P'},
            {"mgroup",1,NULL,'M'},
            {"player",1,NULL,'p'},
            {"help",0,NULL,'H'},
            {NULL,0,NULL,0}
    };
    while(1)
    {
        // extern char *optarg 是与getopt_long有关的全局变量，会自动指向命令行当前带参数选项，后面的参数
        c = getopt_long(argc,argv,"P:M:p:H",argarr,&index);
        if(c < 0)
            break;
        switch (c)
        {
            case 'P':
                client_conf.rcvport = optarg;
                break;
            case 'M':
                client_conf.mgroup = optarg;
                break;
            case 'p':
                client_conf.player_cmd = optarg;
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
    /*********************************************************************************/


    /*2. 创建报式套接字UDP，并且设置套接字属性*/
    sd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (sd < 0)
    {
        perror("socket()");
        exit(1);
    }
    // 套接字属性-加入多播组--成为多播组的成员
    struct ip_mreqn mreq;
    // 客户端向加入的多播组的IP地址
    inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr);
    // 设置本地网络接口的IP地址，用于接收多播消息
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    // 指定网络接口，指定多播数据由该网络接口（网卡）接收
    mreq.imr_ifindex = if_nametoindex(DEFAULT_IF_NAME);
    if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("setockopt()");
        exit(1);
    }
    // 套接字属性--允许多播数据被本地回环接口接收
    int val = 1;
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) < 0)
    {
        perror("setockopt()");
        exit(1);
    }
    /*********************************************************************************/


    /*3. 绑定本机IP与端口*/
    struct sockaddr_in laddr;
    laddr.sin_family = AF_INET;
    // 端口号需要与服务端一致
    laddr.sin_port = htons(atoi(client_conf.rcvport));
    inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr);
    if (bind(sd, (void *)&laddr, sizeof(laddr)) < 0)
    {
        perror("bind()");
        exit(1);
    }
    /*********************************************************************************/


    /*4. 创建管道用于父子进程通信*/
    int pd[2]; // 读写的文件描述符数组，0用于读取，1用于写入
    if (pipe(pd) < 0)
    {
        perror("pipe()");
        exit(1);
    }
    /*********************************************************************************/


    /*5. 创建父子进程，区分父子进程的工作*/
    pid = fork();
    if (pid < 0)
    {
        perror("fork()");
        exit(1);
    }
    // 子进程-调解码器
    if (pid == 0)
    {
        // 子进程调管道的读端，读取数据，调用解码器进行播放
        close(sd);    // 子进程会将父进程资源复制，关闭sd
        close(pd[1]); // 关闭写端

        // 0号描述符表示标准输入
        // 现在从标准输入读取的数据都是从管道读端读取的数据
        dup2(pd[0], 0); // 将stdin(标准输入)关闭，重定向到管道读端
        if (pd[0] > 0)  // 如果 pd[0]的文件描述符不为0，则关闭原始的pd[0] 文件描述符
            // 因为pd[0]已经重定向到0上，保留原始的pd[0]可能会存在资源泄露
            close(pd[0]);

        // 通过execl函数，启动一个新的shell进程，并通过这个shell执行 client_conf.player_cmd 中定义的命令
        // 通过shell 指令，执行命令 client_conf.player_cmd
        // 例如 sh -c 'ls -l'  就是在当前目录下通过启动一个新的shell进程执行命令 ls -l
        if (execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL) < 0)
        {
            perror("execl()");
            exit(1);
        }
    }
    // 父进程-从网络上收包，发送给子进程(写管道)
    else
    {
        // 收节目单
        close(pd[0]);   //关闭读端
        struct msg_list_st *msg_list; // 节目单结构体指针
        msg_list = malloc(MSG_LIST_MAX);
        if (msg_list == NULL)
        {
            perror("malloc()");
            exit(1);
        }
        serveraddr_len = sizeof(struct sockaddr_in); // 此句关键，如何不设置，会出现服务端地址与接收端地址不匹配错误
        // 接收数据
        while (1)
        {
            // 接收的数据，放到msg_list缓冲区中，缓冲区大小为MSG_LIST_MAX
            // serveraddr 存放发送数据机器的ip信息
            len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (void *)&serveraddr, &serveraddr_len);
            // 数据量不够，出错
            if (len < sizeof(struct msg_list_st))
            {
                fprintf(stderr, "message is too small.\n");
                continue;
            }
            // 比对数据包中的频道id，节目单的频道id为LISTCHNID
            if (msg_list->chnid != LISTCHNID)
            {
                fprintf(stderr, "received program list chnid %d is not match.\n", msg_list->chnid);
                continue;
            }
            break;
        }

        // 打印节目单，选择频道
        // 创建节目单中每一条记录的数据结构体指针
        struct msg_listentry_st *pos;
        // (char *)pos < (((char *)msg_list) + len) 中的len为上面recvfrom接收的节目单整体数据长度
        for(pos = msg_list->entry;(char *)pos < (((char *)msg_list) + len);pos = (void *)(((char *)pos) + ntohs(pos->len)))
        {
            printf("channel %d : %s\n", pos->chnid, pos->desc);  // 打印这条节目的数据
        }
        free(msg_list);

        // 选择频道
        // 用户做的选择无需上传到server端，因为server端是将所有频道的数据都进行发送，用户在这些数据中挑选中自己需要的数据
        puts("Please choose a channel:");

        do{
            ret = scanf("%d", &chosenid);
        }while(ret < 1);

        fprintf(stdout, "chosen channel = %d\n", chosenid);

        // 收频道包，发送给子进程
        struct msg_channel_st *msg_channel;  // 创建用于接收指定频道数据的结构体
        msg_channel = malloc(MSG_CHANNEL_MAX);
        if (msg_channel == NULL)
        {
            perror("malloc()");
            exit(1);
        }
        len = 0;
        raddr_len = sizeof(struct sockaddr_in);
        while (1)
        {
            // 做完频道id选择之后，持续接收频道的数据包，并且判断是不是自己指定频道id的包
            len = recvfrom(sd,msg_channel,MSG_CHANNEL_MAX,0,(void *)&raddr,&raddr_len);
            // 比对serveraddr与raddr中的ip信息，判断节目单与各个频道数据的发送方是否是同一个机器,防止中间方伪造数据
            if (raddr.sin_addr.s_addr != serveraddr.sin_addr.s_addr || raddr.sin_port != serveraddr.sin_port)
            {
                fprintf(stderr, "Ignore:address not match.\n");
                continue;
            }
            if (len < sizeof(struct msg_channel_st))
            {
                fprintf(stderr, "message is too small.\n");
                continue;
            }
            if (msg_channel->chnid == chosenid)
            {
                // 如果当前接收的频道的数据包就是，用户选择的频道id的数据包，则将数据写入管道的写端口，这样子进程就可以调用解码器对数据进行解析
                // 可考虑设置缓存区接收字节，避免音乐播放断断续续
                fprintf(stdout, "accepted channel %d msg.\n", msg_channel->chnid);
                // 从msg_channel->data向管道的写端口写入数据，坚持写够字节数为len - sizeof(chnid_t)
                if(writen(pd[1], msg_channel->data, len - sizeof(chnid_t)) < 0)
                {
                    fprintf(stderr, "writen(): writen data error!\n");
                    exit(1);
                }
            }
        }
        free(msg_channel);
        close(sd);
        wait(NULL);
    }
    /*********************************************************************************/

    exit(0);
}

