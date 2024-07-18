/*多线程并发的令牌桶，流量控制*/
#ifndef SERVER_MYTBF_H
#define SERVER_MYTBF_H

#define MYTBF_MAX 1024 // 令牌桶数组容量大小

typedef void mytbf_t;  // 令牌桶类型

/*初始化令牌桶*/
// cps 每次可以获得令牌数量  burst 桶内可以积攒的领牌数量上限
mytbf_t *mytbf_init(int cps,int burst);

/*从令牌桶中取令牌*/
// 从令牌桶中取得令牌，int 第二个参数是想取多少
// 返回值，为真正取得了多少
int mytbf_fetchtoken(mytbf_t *,int);

/*归还没有使用完的令牌*/
// 将没有用完的令牌数量还到令牌桶中, int 第二个参数是想还回去的数量
// 返回值，为真正还了多少
int mytbf_returntoken(mytbf_t *,int);

/*令牌桶销毁*/
int mytbf_destroy(mytbf_t *);

#endif //SERVER_MYTBF_H
