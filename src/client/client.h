#ifndef IPV4_STREAMING_MEDIA_CLIENT_H
#define IPV4_STREAMING_MEDIA_CLIENT_H

/*
 * /usr/bin/mpg123：这是一个常见的命令行音频播放器，用于播放MP3文件。它通常安装在Unix和Linux系统的 /usr/bin 目录下
 * -：在命令行工具中，单独的短横线 (-) 通常表示该工具应从标准输入（stdin）读取数据，而不是从文件读取。在这里，它意味着 mpg123 将从它的标准输入读取音频数据
 * >：这是一个重定向操作符，用于将命令的输出从默认的输出目标（通常是屏幕）重定向到另一个位置
 * /dev/null：这是一个特殊的文件，通常被称为“黑洞”。向 /dev/null 写入的数据都会被丢弃，读取 /dev/null 时通常会立即得到文件结束符（EOF）。这里使用它是为了忽略 mpg123 的所有输出，即把所有输出都丢弃
 * */
#define DEFAULT_PLAYERCMD "/usr/bin/mpg123 - > /dev/null"  // 输出重定向到/dev/null
#define DEFAULT_IF_NAME             "ens33"

struct client_conf_st
{
   char *rcvport;
   char *mgroup;
   char *player_cmd; // 播放器指令
};

extern struct client_conf_st client_conf;

#endif //IPV4_STREAMING_MEDIA_CLIENT_H
