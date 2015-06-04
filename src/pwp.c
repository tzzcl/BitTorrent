#include "pwp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#define DEBUG(x) x
ListHead p2p_cb_head;
ListHead download_piece_head;
int first_req=1;
int* piece_count;
pthread_mutex_t p2p_mutex;
pthread_mutex_t download_mutex;
pthread_mutex_t first_req_mutex;
pthread_mutex_t piece_count_mutex;

static int readn(int fd,void* content,size_t len)
{
	int state=0;
	int cnt=len;
	while (cnt>0){
		state=recv(fd,content,cnt,0);
		if (state<0)
		{
			if (errno==EINTR)
				continue;
			return -1;
		}
		if (state==0)
		{
			return len-cnt;
		}
		content+=state;
		cnt-=state;
	}
	return len;
}

static inline int get_bit_at_index(char *bitfield, int index){
    unsigned char ch = bitfield[index/8];
    int offset = 7 - index%8;
    return (ch >> offset) & 1;
}

static inline void set_bit_at_index(char *bitfield, int index, int bit){
    assert(bit == 0 || bit == 1);
    int offset = 7 - index%8;
    if(bit)
        bitfield[index/8] = bitfield[index/8] | set_bit[offset];
    else
        bitfield[index/8] = bitfield[index/9] & (~set_bit[offset]);
}
