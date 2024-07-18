#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <syslog.h>

#include "mytbf.h"

/*令牌桶类型数组，对于文件可以有MYTBF_MAX中传输速率选择*/
static struct mytbf_st* job[MYTBF_MAX];  // 这是多个进程共享的资源，需要作线程同步，避免数据混乱
// 线程互斥量的初始化
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER;
// 子进程ID
static pthread_t tid_alarm;
// 定义全局 once 控制变量，这个变量跟踪初始化函数是否已被调用
static pthread_once_t init_once = PTHREAD_ONCE_INIT;

/*一个令牌桶的信息结构体*/
struct mytbf_st
{
    int cps;     // 令牌桶每次可以积攒的令牌数
    int burst;   // 一个令牌桶中积攒的令牌数上限
    int token;   // 一个令牌桶的实时令牌数
    int pos;     // 该令牌桶位于令牌桶系统中的令牌桶数组中的位置索引
    pthread_mutex_t mut;  // 每一个令牌桶通过互斥量保证token的同步
    pthread_cond_t cond;  // 条件变量--用于通知令牌桶令牌数量的变化
};


/*为令牌桶系统中每个令牌桶积攒令牌的子线程函数*/
static void *thr_alarm(void *p)
{
    struct timespec t;

    while(1)
    {
        // 访问令牌桶系统的令牌桶结构体数组的共享资源，枷锁
        pthread_mutex_lock(&mut_job);
        for (int i = 0; i < MYTBF_MAX; i++)
        {

            // 当前令牌桶数组位置存在数据
            if(job[i] != NULL)
            {

                // 操作该令牌桶的令牌数，枷锁
                pthread_mutex_lock(&job[i]->mut);

                // 为i位置的令牌桶装载令牌
                job[i]->token += job[i]->cps;
                // 但是令牌桶所积累的令牌数量需要维持在令牌桶的上限
                if(job[i]->token > job[i]->burst)
                    job[i]->token = job[i]->burst;
                // for 循环内给令牌桶数组中的所有令牌桶均加了token
                // 需要唤醒所有的阻塞（挂起）的线程，去唤醒，此时令牌桶中的令牌>0，可以消费了
                pthread_cond_broadcast(&job[i]->cond);

                pthread_mutex_unlock(&job[i]->mut);
            }
        }

        pthread_mutex_unlock(&mut_job);

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


/*模块卸载*/
static void module_unload(void)
{
    // 取消为令牌桶系统中每个令牌桶积攒令牌的子线程
    pthread_cancel(tid_alarm);
    // 资源回收
    pthread_join(tid_alarm, NULL);

    for (int i = 0; i < MYTBF_MAX; i++)
    {
        if(job[i] != NULL)
        {
            // 销毁令牌桶系统中存在的令牌桶
            mytbf_destroy(job[i]);
        }
    }

    // 销毁互斥锁
    pthread_mutex_destroy(&mut_job);
}


/*模块加载*/
static void module_load(void)
{
    int err;
    // 创建令牌桶系统中为各个存在的令牌桶积攒令牌的子线程--该子线程在整个令牌桶系统中只创建一个
    err = pthread_create(&tid_alarm,NULL,thr_alarm,NULL);
    if(err)
    {
        fprintf(stderr,"pthread_create():%s\n", strerror(err));
        exit(1);
    }

    // 钩子函数，程序结束前最后执行，对模块进行卸载
    atexit(module_unload);
}


static int min(int a,int b)
{
    return a<b ? a:b;
}


/*在令牌桶数组中找到空位置*/
// 这个函数本身没有加线程锁，但是调用的时候应该枷锁
// 因为令牌桶数组为共享资源，需要保持线程同步，所以设置为unlocked
static int get_free_pos_unlocked(void)
{
    for(int i = 0; i < MYTBF_MAX; i++)
    {
        if(job[i] == NULL)
            return i;  // 返回空位置的下标
    }
    return -1;
}


/*
 * 令牌桶初始化
 * cps：每次可以取得的令牌数  burst 令牌桶中可以积攒的令牌数量上限
 * return: 一个令牌桶类型的指针
 * */
mytbf_t *mytbf_init(int cps,int burst)
{
    struct mytbf_st *me;

    // 模块加载函数，pthread_once即使多个线程执行，但是其只会在第一个调用的线程中执行一次
    pthread_once(&init_once,module_load);

    // 为当前的令牌桶开辟空间
    me = malloc(sizeof(*me));
    if(me == NULL)
        return NULL;

    me->token = 0;     // 当前令牌桶，初始令牌为0
    me->cps = cps;     // 每次可以获得的令牌数量为cps
    me->burst = burst; // 令牌桶中可以积攒的令牌数量上限为burst
    pthread_mutex_init(&me->mut,NULL);  // 关于令牌桶中令牌的互斥量初始化
    pthread_cond_init(&me->cond,NULL);  // 初始化条件变量

    // 访问共享资源，枷锁保持线程同步
    pthread_mutex_lock(&mut_job);
    int pos = get_free_pos_unlocked();
    if(pos < 0)
    {
        pthread_mutex_unlock(&mut_job);  // 失败，解锁退出
        free(me);
        return NULL;
    }

    me->pos = pos;
    job[pos] = me;
    // 解锁
    pthread_mutex_unlock(&mut_job);


    return me;
}


/*
 * 从令牌桶中取得令牌，int 第二个参数是想取多少
 * */
// 返回值，为真正取得了多少令牌
int mytbf_fetchtoken(mytbf_t *ptr,int size)
{
    struct mytbf_st *me = ptr;
    int n;

    if(size < 0)
        // EINVAL（Invalid Argument）是另一个常见的错误代码，表示传递给系统调用的一个或多个参数是无效的
        return -EINVAL;  // 这样就可以使用strerror(errno)进行报错

    // 访问该令牌桶中的令牌，但是可能有其他线程此时正准备归还令牌，因此需要保证线程同步
    // 枷锁
    pthread_mutex_lock(&me->mut);

    while(me->token <= 0)
    {
        // 如果没有条件变量通知，线程会阻塞在此处，挂起不会占用CPU很多资源
        // 直到 thr_alarm 与 mytbf_returntoken 函数中使用pthread_cond_broadcast 通知条件变量
        // 唤醒阻塞在这里的所有线程，并且抢锁，枷锁，查看token值
        pthread_cond_wait(&me->cond,&me->mut);

    }

    // 取小值
    n = min(me->token,size);
    me->token -= n;  // 令牌数量减少
    // 解锁
    pthread_mutex_unlock(&me->mut);

    return n;
}


/*
 * 将没有用完的令牌数量还到令牌桶中, int 第二个参数是想还回去的数量
 * */
// 返回值，为真正还了多少令牌
int mytbf_returntoken(mytbf_t *ptr,int size)
{
    struct mytbf_st *me = ptr;

    if(size < 0)
        // EINVAL（Invalid Argument）是另一个常见的错误代码，表示传递给系统调用的一个或多个参数是无效的
        return -EINVAL;  // 这样就可以使用strerror(errno)进行报错

    // 访问该令牌桶中的令牌，但是可能有其他线程此时正准备归还令牌，因此需要保证线程同步
    // 枷锁
    pthread_mutex_lock(&me->mut);
    me->token += size;  // 归还了，当前令牌桶的令牌数量增加
    // 但是还是要约束在上限内
    if(me->token > me->burst)
        me->token = me->burst;

    // 这个时候也需要唤醒阻塞的线程，因为此时的token>0,也可以进行消费了
    // 通过条件变量发送通知
    pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);

    return size;
}


/*
 * 销毁令牌桶
 * */
int mytbf_destroy(mytbf_t *ptr)
{
    // 将传入的令牌桶销毁
    struct mytbf_st *me = ptr;

    // 将当前令牌桶占用的令牌桶数组位置赋值为NULL，共享资源，枷锁
    pthread_mutex_lock(&mut_job);
    job[me->pos] = NULL;
    pthread_mutex_unlock(&mut_job);

    // 销毁该令牌桶中有关令牌的互斥量
    pthread_mutex_destroy(&me->mut);
    // 条件变量也进行销毁
    pthread_cond_destroy(&me->cond);
    // 释放创建传入的令牌桶开辟的动态空间
    free(ptr);

    return 0;
}
