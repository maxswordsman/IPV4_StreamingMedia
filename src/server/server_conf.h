#ifndef IPV4_STREAMING_MEDIA_SERVER_CONF_H
#define IPV4_STREAMING_MEDIA_SERVER_CONF_H

#define DEFAULT_MEDIADIR  "/home/zxz/Proj/C_C++/linux_c/IPV4_StreamingMedia/Music"   // 默认的媒体库位置
#define DEFAULT_IF  "ens33"               // 默认的网卡设备

enum{
    RUN_DAEMON = 1,  // 以daemon模式运行，守护进程的形式运行在后台
    RUN_FOREGROUND   // 作为前台方式运行
};

struct server_conf_st{
    char *rcvport;    // 端口
    char *mgroup;     // 多播组ip
    char *media_dir;  // 媒体库路径
    char *runmode;    // 运行模式
    char *ifname;     // 网络设备
};

extern struct server_conf_st server_conf;
extern int serverSd;  // 服务端的UDP套接字文件描述符
extern struct sockaddr_in sndaddr;


#endif //IPV4_STREAMING_MEDIA_SERVER_CONF_H
