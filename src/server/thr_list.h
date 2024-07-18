#ifndef IPV4_STREAMING_MEDIA_THR_LIST_H
#define IPV4_STREAMING_MEDIA_THR_LIST_H

#include "medialib.h"

/*创建节目单线程*/
int thr_list_create(struct mlib_listentry_st *,int);

/*销毁节目单线程*/
int thr_list_destroy(void);

#endif //IPV4_STREAMING_MEDIA_THR_LIST_H
