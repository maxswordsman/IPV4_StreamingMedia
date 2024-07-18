#include <stdlib.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "proto.h"
#include "server_conf.h"
#include "thr_channel.h"


/*每个频道由一个线程负责*/
struct thr_channel_ent_st{
    chnid_t chnid;
    pthread_t tid;
};


static int tid_nextpos = 0;
/*频道线程的结构体数组 CHNNR 为频道（最多）数量*/
struct thr_channel_ent_st thr_channel[CHNNR];


/*
 * func:
 *      频道线程的处理函数,读取媒体库中，具体频道的mp3文件内容，并且发送多播组ip
 * parameter：
 *      void *ptr
 *      为单个频道的信息 包含 频道id + 描述信息
 *      void * 为万能类型，在函数体内部还是得进行显式转换
 * */
static void *thr_channel_snder(void *ptr)
{
    struct msg_channel_st *sbufp;  // 在网络通信中发送的频道信息数据包
    int len;
    struct mlib_listentry_st *ent = ptr;


    // MSG_CHANNEL_MAX 为 msg_channel_st具体频道数据包的最大长度
    sbufp = malloc(MSG_CHANNEL_MAX);
    if(sbufp == NULL)
    {
        syslog(LOG_ERR,"malloc():%s",strerror(errno));
        exit(1);
    }
    sbufp->chnid = ent->chnid; // 频道号是uint_8 8位相当于单字节 无需字节序转换

    // 频道内容读取
    while(1)
    {
        // 读取的内容存放到缓冲区 sbutf->data 中
        len = mlib_readchn(ent->chnid,sbufp->data,MSG_DATA);

        // 发送频道中的内容至多播放组ip
        if(sendto(serverSd,sbufp,len+sizeof(chnid_t),0,(void *)&sndaddr, sizeof(sndaddr)) < 0)
        {
            syslog(LOG_ERR, "thr_channel(%d):sendto():%s", ent->chnid, strerror(errno));
            break;
        } else{
            syslog(LOG_DEBUG, "thr_channel(%d): sendto() succeed.", ent->chnid);
        }
        // 让出调度器，让别的线程使用CPU时间片
        sched_yield();
    }
    // 线程退出
    pthread_exit(NULL);
}

/*
 * func:
 *      创建频道线程
 * parameter：
 *      struct mlib_listentry_st *ptr: 为单个频道的信息 包含 频道id + 描述信息
 * return:
 *      成功，返回0
 *      失败，返回错误码 errno
 * */
int thr_channel_create(struct mlib_listentry_st *ptr)
{
    int err;

    if(tid_nextpos >= CHNNR)
    {
        // 频道线程的结构体数组空间不够
        return -ENOSPC;
    }
    // 创建频道线程
    err = pthread_create(&thr_channel[tid_nextpos].tid,NULL,thr_channel_snder,ptr);

    if(err)
    {
        syslog(LOG_WARNING,"pthread_create():%s",strerror(err));
        return -err;  // pthread_create函数出错的时候，返回其出错码
    }
    // 为当前频道线程结构体数组中当前索引位置的频道线程结构体 设置字段的值
    thr_channel[tid_nextpos].chnid = ptr->chnid;
    // 当前索引位置已经被该线程占用，索引位置+1
    tid_nextpos++;

    return 0;
}


/*
 * func:
 *      销毁频道线程
 * parameter:
 *      struct mlib_listentry_st *ptr: 为单个频道的信息 包含 频道id + 描述信息
 * return:
 *      失败，返回错误码
 *      成功，返回0
 * */
int thr_channel_destroy(struct mlib_listentry_st *ptr)
{
    // 遍历线程结构体数组
    for(int i = 0;i>CHNNR;i++)
    {
        // 找到负责该频道的线程
        if(thr_channel[i].chnid = ptr->chnid)
        {
            // 线程取消
            if(pthread_cancel(thr_channel[i].tid) < 0)
            {
                // 取消失败
                syslog(LOG_ERR,"pthread_cannel():thr thread of channel %d",ptr->chnid);
                return -ESRCH;   // 错误码 表示没有找到这样的线程
            }
            // 线程资源回收
            pthread_join(thr_channel[i].tid,NULL);
            thr_channel[i].chnid = -1;   // 频道线程结构体数组位置取消占用
        }
    }
    return 0;
}


/*
 * 销毁所有的频道线程
 * */
int thr_channel_destroyall(void)
{
    for(int i=0;i<CHNNR;i++)
    {
        // 不为-1 这个索引位置有频道线程
        if(thr_channel[i].chnid > 0)
        {
            // 线程取消
            if(pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR,"pthread_cannel():thr thread of channel %d",thr_channel[i].chnid);
                return -ESRCH;
            }
            // 线程资源回收
            pthread_join(thr_channel[i].tid,NULL);
            thr_channel[i].chnid =  -1;
        }
    }
    return 0;
}

