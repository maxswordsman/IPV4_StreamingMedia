#ifndef IPV4_STREAMING_MEDIA_MEDIALIB_H
#define IPV4_STREAMING_MEDIA_MEDIALIB_H

#include "site_type.h"

#define MP3_PARTERN "/*.mp3"
#define DESC_FNAME  "/desc.txt"

#define MP3_BITRATE 	(128 * 1024)   // MP3文件播放的比特率 这代表 128kbps（千比特每秒）

// 媒体库中每一个频道信息的结构体
// 当然也可以使用一个相同类型的指针，指向一个结构体数组，代表媒体库中所有的频道信息
struct mlib_listentry_st{
    chnid_t chnid;   // 频道id
    char *desc;      // 频道描述信息
};

/*获取频道库的频道信息*/
int mlib_getchnlist(struct mlib_listentry_st **,int *);

/*释放节目单信息*/
int mlib_freechnlist(struct mlib_listentry_st *);

/*读取每个频道信息*/
// chnid_t 表示频道id，指定需要读取的频道
// void * 表示读取信息的缓冲区，读取的数据存储到缓冲区
// size_t 类型参数 表示需要读取的字节数量
size_t mlib_readchn(chnid_t, void *, size_t);

#endif //IPV4_STREAMING_MEDIA_MEDIALIB_H
