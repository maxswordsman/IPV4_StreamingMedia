#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "proto.h"
#include "thr_list.h"
#include "server_conf.h"
#include "medialib.h"

// 节目单线程id
static pthread_t tid_list;
// 节目单中包含的节目数量
static int nr_list_ent;
// 节目单信息数组（存储所有有效的频道信息：频道id + 描述信息）
static struct mlib_listentry_st *list_ent;


/*
 * func:
 *      节目单线程函数，用于处理从媒体库中获得的节目单数据，将节目单数据发送给多播组ip
 *      每秒执行一次
 * */
static void *thr_list(void *p)
{
    int totalsize;
    struct msg_list_st *entlistp;  // 用于发送的节目单数据包结构体指针
    struct msg_listentry_st *entryp;  // 用于发送的节目单中的一条频道信息的数据包结构体指针
    int size;
    int ret;
    struct timespec t;

    /*
     * // 用于发送的单个频道信息  频道id + 描述信息
     * struct msg_listentry_st{
     *      chnid_t chnid;
     *      uint16_t len;          // 这条记录数据的长度  频道id + uint16_t + 描述信息 的 总共大小
     *      uint8_t desc[1];       // 变长数组，柔性数组成员,用于存储频道的描述信息
     * }__attribute__((packed));   // 不使用对齐
     *
     * // 用于发送的节目单信息   节目单id + 用于发送的单个频道信息(频道id + 描述信息)
     * struct msg_list_st{
     *      chnid_t chnid;  // 节目单的频道id  LIST_CHNID
     *      struct msg_listentry_st entry[1];  // 柔性结构体数组，用于存储多条 用于发送的单个频道信息
     * }__attribute__((packed));
     * */


    totalsize = sizeof(chnid_t);   // 一个频道id的大小---节目单的频道id

    for(int i=0;i<nr_list_ent;i++)
    {
        // 计算为所有有效的频道信息的存储空间大小 （每个频道的id + 各自的描述信息）
        totalsize += sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);
    }

    // 此时totalsize 存储节目单的空间大小
    entlistp = malloc(totalsize);
    if(entlistp == NULL)
    {
        // 内存开辟失败
        syslog(LOG_ERR,"malloc():%s", strerror(errno));
        exit(1);
    }

    entlistp->chnid = LISTCHNID;   // 节目单的频道id
    entryp = entlistp->entry;      // 指向所有的有效频道的 （频道id + 描述信息）
    // 此时对entryp进行操作，实际就是对entlistp->entry进行操作，指向同一块内存空间
    // entryp 指向的是 entlistp->entry指向空间的起始地址

    for(int i = 0;i<nr_list_ent;i++)
    {
        // 计算每一个有效频道信息的存储空间大小
        // 频道id + uint16_t + 描述信息 的总大小 (总的字节数)
        size = sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);

        // uint_8类型变量为什么不需要转化为网络字节序
        // 因为对于单字节数据类型不要进行字节序转换，因为单字节数据（8位）在内存中只占用一个字节，无论是大端字节序还是小端字节序，其在内存中的表达方式都是一致的。
        // uint_8 类型数据也是 8位 在内存中只占用一个字节
        entryp->chnid = list_ent[i].chnid;
        // size 为int 变量，进行网络传输，字节序应该从主机序转化为网络序
        entryp->len = htons(size);

        strcpy(entryp->desc,list_ent[i].desc);
        // entryp 指向的地址向后移动，用于填写entlistp->entry内存空间中的下一个频道的信息
        // (char *)entryp：将 entryp 转换为 char * 类型。char * 是指向字符的指针，而一个字符（在C语言中）通常占一个字节。
        // 这种转换使得对指针的增加操作变得以字节为单位，因为对 char * 指针的算术操作会按字节来进行。
        // (void *) 可以指向任何类型的数据，常用于泛型数据处理
        entryp = (void *)(((char *)entryp) + size);
    }

    // 在0号频道发送节目单
    while (1)
    {
        // 将节目单信息发送（报式套接字 UDP 使用sendto）
        // serverSd为服务端创建的UDP套接字文件描述符
        // 将节目单数据包发送至多播组ip
        ret = sendto(serverSd,entlistp,totalsize,0,(void *)&sndaddr,sizeof sndaddr);
        if(ret < 0)
        {
            // 发送失败
            syslog(LOG_WARNING,"sendto():%s", strerror(errno));
        }
        else
        {
            syslog(LOG_DEBUG,"send to program list succeed.");
        }

        // 1s发送一次节目单数据包
        // 使用nanosleep函数使得线程休眠1s
        t.tv_sec = 1;
        t.tv_nsec = 0;
        while(nanosleep(&t,&t) != 0)
        {
            // 真错
            if(errno != EINTR)
            {
                fprintf(stderr,"nanosleep():%s\n", strerror(errno));
                exit(1);
            }
        }
    }
}


/*
 * func:
 *      创建节目单线程
 * parameter：
 *      struct mlib_listentry_st *listp : 存储着媒体库中所有的频道信息（这是从媒体库中获得的信息）
 *      频道id 以及 频道描述信息
 * */
int thr_list_create(struct mlib_listentry_st *listp,int nr_ent)
{
    int err;
    list_ent = listp;
    nr_list_ent = nr_ent;


    err = pthread_create(&tid_list,NULL,thr_list,NULL);
    if(err)
    {
        // err 非0 表示线程创建失败
        syslog(LOG_ERR,"pthread_create():%s",strerror(errno));
        return -1;  // 创建失败return  -1
    }
    return 0;
}


/*销毁节目单线程*/
int thr_list_destroy(void)
{
    // 发送节目单的线程取消
    pthread_cancel(tid_list);
    // 线程资源回收
    pthread_join(tid_list,NULL);
    return 0;
}