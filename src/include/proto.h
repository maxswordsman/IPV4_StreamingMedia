/*
 * 通信协议
 * */
#ifndef IPV4_STREAMING_MEDIA_PROTO_H
#define IPV4_STREAMING_MEDIA_PROTO_H

#include "site_type.h"

// 数据发送到组播地址，然后被分发到加入改组的成员
#define DEFAULT_MGROUP "224.2.2.2"  // 定义多播组的地址
#define DEFAULT_RCVPORT "1989"      // 接收端口
#define CHNNR 100                  // 频道数量
#define LISTCHNID 0
#define MINCHNID 1
#define MAXCHNID (MINCHNID + CHNNR - 1)

// 推荐包的长度-IP报的长度-UDP报的长度
#define MSG_CHANNEL_MAX (65536-20-8)  //  msg_channel_st具体频道数据包的最大长度
// 数据包的最大长度 - 频道id报头的大小
#define MSG_DATA (MSG_CHANNEL_MAX - sizeof(chnid_t)) // 具体频道数据包中数据(msg_channel_st中的data)的最大长度

// 推荐包的长度-IP报的长度-UDP报的长度
#define MSG_LIST_MAX (65536 - 20 -8) // msg_list_st数据包的最大长度
// 数据包的最大长度 - 频道id报头的大小
#define MAX_ENTRY (MSG_LIST_MAX - sizeof(chnid_t)) // msg_list_st数据包中数据(msg_list_st中的entry)的最大长度

// 定义有关具体频道的数据包
// GUN C 中 __attribute__((packed)) 取消数据对齐
struct msg_channel_st{
    chnid_t chnid;   // 频道id,介于[MINCHNID,MAXCHNID]
    uint8_t data[1]; // 数据包的长度，变长的数组
}__attribute__((packed));


/*
  节目单形式：
  1 music : xxxx   （xxx代表具体的名称）
  2 sport : xxxx
  3 xxxxx : xxxx
  ....
 */

// 节目单中每一条记录的数据包
// 需要包含节目号+描述 (1 music : xxxx)
struct msg_listentry_st{
    chnid_t chnid;
    uint16_t len;    // 这条记录数据的长度
    uint8_t desc[1]; // 变长数组，柔性数组成员
}__attribute__((packed));

// 定义msg_list数据包
// 用于发送的节目单数据包
struct msg_list_st{
    chnid_t chnid;  // 频道id,因为是msg_list只有选择LIST_CHNID才会获取得到节目单
    struct msg_listentry_st entry[1];  // 变长数组，柔性数组成员
}__attribute__((packed));




#endif //IPV4_STREAMING_MEDIA_PROTO_H
