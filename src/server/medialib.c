#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "medialib.h"
#include "proto.h"
#include "mytbf.h"
#include "server_conf.h"

#define PATHSIZE 1024
#define LINEBUFSIZE 1024

struct channel_context_st{
    chnid_t chnid;  // 频道id
    char *desc;     // 指向频道的描述信息
    glob_t mp3glob;
    int pos;
    int fd;         // 频道目录下打开的mp3文件的文件描述符
    off_t offset;   // 发送这个数据是一段一段发送出去的
    mytbf_t *tbf;   // 流量控制---一个频道一个令牌桶
};

// 创建结构体数组存储所有的频道节目信息
static struct channel_context_st channel[MAXCHNID + 1];   // 100 个频道  + 节目单的频道id
static chnid_t curr_id = MINCHNID;


static int open_next(chnid_t chnid)
{
    for (int i = 0; i < channel[chnid].mp3glob.gl_pathc; i++)
    {
        channel[chnid].pos++;
        //所有的歌曲都没有打开
        if (channel[chnid].pos == channel[chnid].mp3glob.gl_pathc)
        {
            channel[chnid].pos = 0; //再来一次
        }

        close(channel[chnid].fd);
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY);
        //如果打开还是失败
        if (channel[chnid].fd < 0)
        {
            syslog(LOG_WARNING, "open(%s):%s", channel[chnid].mp3glob.gl_pathv[chnid], strerror(errno));
        }
        else //success
        {
            channel[chnid].offset = 0;
            return 0;
        }
    }
    syslog(LOG_ERR, "None of mp3 in channel %d is available.", chnid);
    return -1;
}


/*
 * func:
 *      解析path路径下的频道信息是否合法，如果合法返回合法频道的记录信息
 *      若该频道合法，还会在该函数中，为该频道创建一个令牌桶，实现流量控制
 * parameter:
 *      const char *path:
 *          具体的频道目录，若媒体库目录为./Music 在媒体库下有三个频道目录ch1、ch2、ch3
 *          则具体的频道目录可以是 ./Music/ch_{i} 其中嗯i 可以是1、2、3
 * return:
 *      失败，返回NULL
 *      成功，返回struct channel_context_st类型的结构体指针
 * */
static struct channel_context_st *path2entry(const char *path)
{
    char pathstr[PATHSIZE] = {'\0'};
    char linebuf[LINEBUFSIZE];
    FILE *fp;
    struct channel_context_st *me;

    // 将需要解析的目录复制给字符串数组 pathstr
    strncpy(pathstr,path,PATHSIZE);
    // 将字符串拼接并且复制给字符串数组 pathstr
    // 此时pathstr指向的是 path 目录下的描述文件
    strncat(pathstr,DESC_FNAME,PATHSIZE);

    // 1. 处理描述文件
    // 打开描述文件
    fp = fopen(pathstr,"r");
    if(fp == NULL)
    {
        // 描述文件不存在，说明path路径的频道信息不合法
        syslog(LOG_INFO,"%s is not a channel dir (can not find desc.txt)",path);
        return NULL;
    }

    // 文件存在，但是描述文件内容为空，如果不为空，那么文件第一行内容就是频道的描述
    // 将第一行的描述信息存储到linebuf中
    if(fgets(linebuf,LINEBUFSIZE,fp) == NULL)
    {
        syslog(LOG_INFO,"%s is not a channel dir (can get the desc.txt,but Content is empty)",path);
        fclose(fp);  // 关闭文件
        return NULL;
    }
    fclose(fp);  // 关闭文件

    me = malloc(sizeof(*me));
    if(me == NULL)
    {
        syslog(LOG_ERR,"malloc:%s", strerror(errno));
        return NULL;
    }

    // 为该合法频道创建一个令牌桶作流量控制
    me->tbf = mytbf_init(MP3_BITRATE / 8, MP3_BITRATE / 8 * 10);

    if(me->tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init():%s", strerror(errno));
        free(me);
        return NULL;
    }
    // 为合法的频道的描述文件内容分配一个新的内存空间，并且使用me结构体指针指向，作回传
    me->desc = strdup(linebuf);
    // 使用如下指令会段错误，因为me->desc，只是一个字符串指针。并没有实际的内存空间
    // strcpy(me->desc,linebuf);

    // 2.处理mp3文件
    // 将需要解析的目录复制给字符串数组 pathstr
    strncpy(pathstr,path,PATHSIZE);
    // 将字符串拼接并且复制给字符串数组 pathstr
    // 此时pathstr指向的是 path 目录下的描述文件
    strncat(pathstr,MP3_PARTERN,PATHSIZE);
    // 路径匹配,找到频道目录下的所有mp3文件
    if(glob(pathstr,0,NULL,&me->mp3glob) != 0)
    {
        syslog(LOG_ERR, "%s is not a channel dir(can not find mp3 files)", path);
        mytbf_destroy(me->tbf); // 销毁令牌桶
        free(me);
        return NULL;
    }
    me->pos = 0;
    me->offset = 0;
    // 打开mp3文件
    me->fd = open(me->mp3glob.gl_pathv[me->pos],O_RDONLY);

    if(me->fd < 0)
    {
        // mp3文件打不开
        syslog(LOG_WARNING, "%s open failed.", me->mp3glob.gl_pathv[me->pos]);
        mytbf_destroy(me->tbf);  // 销毁令牌桶
        free(me);
        return NULL;
    }
    me->chnid = curr_id;
    curr_id++;
    return me;
}


/*
 * func:
 *      获取频道库的所有合法频道的信息 （频道id + 描述信息），获得媒体库中的节目单信息
 * parameter:
 *      struct mlib_listentry_st **result:指向所有合法频道的记录信息
 *      int *resume:记录了媒体库中有多少条合法的频道记录信息
 * */
int mlib_getchnlist(struct mlib_listentry_st **result,int *resume)
{
    char path[PATHSIZE];
    glob_t globres;
    int num = 0;
    struct mlib_listentry_st *ptr;   // 节目单中的一条频道信息结构体
    struct channel_context_st *res;  // 每一条频道信息指针

    // 频道节目数组初始化
    for(int i = 0;i<MAXCHNID+1;i++)
    {
        channel[i].chnid = -1;
    }

    // 媒体库目录路径存储到path字符数组中
    // "%s/*"  后面的*号代表通配符
    snprintf(path,PATHSIZE,"%s/*",server_conf.media_dir);

    // 使用glob函数查找媒体库目录,并且将目录信息存储到globres结构体中
    // 成功时返回0
    if(glob(path,0,NULL,&globres))
    {
        syslog(LOG_DEBUG,"err 1");
        return -1;
    }

    // ptr 指针指向的内存空间大小，应该是媒体库所有频道记录的和
    // globres.gl_pathc 记录了媒体库中总共有多少个存在的频道（有多少个chi目录，除了发送节目单的频道）
    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);
    if(ptr == NULL)
    {
        // 向系统日志发送错误信息
        syslog(LOG_ERR,"malloc error in  medialib.c in mlib_getchnlist.\n");
        exit(1);
    }

    // 通过for中函数进行解析，判断媒体库中那些频道源文件是合法的
    // gl_pathc字段是匹配到的文件数量  gl_pathv指向找到的文件名数组的指针
    for(int i = 0;i<globres.gl_pathc;i++)
    {
        // 将路径传入函数进行解析,判断这条频道记录是否合法，如果非法传回NULL，合法将频道的信息传回来
        // globres.gl_pathv[i] 其实就是 /var/music/ch(i+1)
        res = path2entry(globres.gl_pathv[i]);

        if(res != NULL)
        {
            // 这条频道信息合法，向系统日志发送告知这条频道的id以及描述信息
            syslog(LOG_DEBUG,"Channel: %d desc: %s",res->chnid,res->desc);
            // memcpy 将 path2entry 返回回来的这条合法的频道信息，存储到结构体数组对应的位置
            memcpy(channel + res->chnid,res,sizeof(*res));
            ptr[num].chnid = res->chnid;
            ptr[num].desc = strdup(res->desc);   // 字符串复制
            // strcpy(ptr[num].desc,res->desc);
            // 合法记录+1
            num++;
        }
    }

    // *result 指向的 真实合法的频道记录的总和
    // 例如，globres.gl_pathc 有4个匹配的目录结构，但是经过for循环解析，发现有两个目录结构是假的，不合法
    // 那么合法（可用）的只有2个频道信息。 *result 就是指向 ptr中两条合法的频道信息记录
    *result = realloc(ptr,sizeof(struct mlib_listentry_st) * num);
    if(*result == NULL)
    {
        // 向系统日志发送错误
        syslog(LOG_ERR,"relloc failed in  medialib.c in mlib_getchnlist.\n");
        exit(1);
    }

    *resume = num;  // resume  将合法的频道记录数量回传

    return 0;
}


/*
 * func:
 *      读取每个频道下的mp3文件内容，通过令牌桶控制读取速度
 * parameter:
 *      chnid_t chnid 表示频道id，指定需要读取的频道
 *      void *buf 表示读取信息的缓冲区，读取的数据存储到缓冲区
 *      size_t size 类型参数 表示需要读取的字节数量
 * return:
 *      返回从mp3文件中读取的内容字节长度
 * */
size_t mlib_readchn(chnid_t chnid, void *buf, size_t size)
{
    int tbfsize;
    int len;

    // 获得令牌
    tbfsize = mytbf_fetchtoken(channel[chnid].tbf,size);

    while(1)
    {
        // 读取mp3文件  buf：指向一个缓冲区的指针，用来存储从文件中读取的数据
        // tbfsize 指定要读取的字节数
        // offset：文件中的偏移量，从文件开始处的字节数，指定从哪里开始读取数据
        len = pread(channel[chnid].fd,buf,tbfsize,channel[chnid].offset);
        if(len < 0)
        {
            //当前这首歌可能有问题，读取下一首歌
            syslog(LOG_WARNING, "media file %s pread():%s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            open_next(chnid);
        }
        else /*len > 0*/ //真正读取到了数据
        {
            channel[chnid].offset += len;  // 下次从这个偏移量继续读
            break;
        }
    }

    if((tbfsize - len) > 0)
        // 归还没有使用完的令牌
        mytbf_returntoken(channel[chnid].tbf, tbfsize - len);

    return len;
}


/*释放节目单信息*/
int mlib_freechnlist(struct mlib_listentry_st *ptr)
{
    free(ptr);
    return 0;
}