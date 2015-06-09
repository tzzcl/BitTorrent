#include "pwp.h"
#include "file.h"
#include "util.h"
#include "info.h"
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
struct globalArgs_t globalArgs;
struct globalInfo_t globalInfo;
int listenfd;
static unsigned char set_bit[8] = {1,2,4,8,16,32,64,128};
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
static inline void safe_free(void* content){
	if (content!=NULL)
		free(content);
}
static inline void drop_conn(p2p_cb* nowp2p)
{
	pthread_mutex_lock(&p2p_mutex);
	list_del(&nowp2p->list);
	pthread_mutex_unlock(&p2p_mutex);
	safe_free(nowp2p->peer_field);
	close(nowp2p->connfd);
	safe_free(nowp2p);
}

int listen_init(){
	int listenfd,connfd;
	struct sockaddr_in servaddr;
	if ((listenfd=socket(AF_INET,SOCK_STREAM,0))<0)
	{
		perror("Problem in creating the listening socket");
		exit(1);
	}
	memset(&servaddr,0,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PEER_PORT);
	bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
	listen(listenfd,8);
	return listenfd;
}

static inline int is_bitfield_empty(char *bitfield,int len){
    int flag = 1;
    for(int i = 0; i < len; i++){
        if(bitfield[i] != 0){
            flag = 0;
            break;
        }
    }
    return flag;
}

int valid_ip(char* ip){
	ListHead* ptr;
	pthread_mutex_lock(&p2p_mutex);
	list_foreach(ptr,&p2p_cb_head){
		p2p_cb *temp = list_entry(ptr,p2p_cb,list);
		if (strcmp(temp->peer_ip,ip)==0)
		{
			pthread_mutex_unlock(&p2p_mutex);
			return 1;
		}
	}
	pthread_mutex_unlock(&p2p_mutex);
	return 0;
}
p2p_cb* new_init_p2p(){
	p2p_cb* temp=malloc(sizeof(p2p_cb));
	memset(temp,0,sizeof(p2p_cb));
	temp->self_choke=1;
	temp->self_interest=0;
	temp->peer_choke=1;
	temp->peer_interest=0;
	list_init(&temp->list);
	return temp;
}
int getconn(char* ip,int port){

}
void* p2p_run_thread(void* param){
	p2p_thread *current = (p2p_thread *)param;
	int connfd;
	if (current->is_connect!=1){
		connfd=current->connfd;
	}
	else{
		connfd=connect_to_host(current->ip,current->port);
		if (connfd==-1)
		{
			int tmp = errno;
            			printf("Error when connect to peer %s:%d, reason:%s\n", current->ip, current->port, strerror(tmp));
            			return NULL;
		}
		current->connfd=connfd;
	}
	int is_connect=current->is_connect;
	p2p_cb* newcb=new_init_p2p();
	newcb->connfd=connfd;
	strcpy(newcb->peer_ip,current->ip);
	safe_free(current);
	int bit=globalInfo.torrentmeta->num_pieces/8+(globalInfo.torrentmeta->num_pieces%8>0);
	newcb->peer_field=(char*)malloc(bit);
	memset(newcb->peer_field,0,bit);
	pthread_mutex_lock(&p2p_mutex);
	list_add_before(&p2p_cb_head,&newcb->list);
	pthread_mutex_unlock(&p2p_mutex);
	if (is_connect){
		puts("send a handshake");
		send_handshake(connfd);
	}
	char len;
	if (readn(connfd,&len,1)>0)
	{
		printf("handshake received %d\n",len);
		char str[len];
		char reserve[8];
		int info_hash[5];
		char peer_id[20];
		readn(connfd,str,len);
		readn(connfd,reserve,8);
		readn(connfd,info_hash,20);
		for (int i=0;i<5;i++)
			info_hash[i]=ntohl(info_hash[i]);
		readn(connfd,peer_id,20);
		if (memcmp(info_hash,globalInfo.torrentmeta->info_hash,20)!=0)
		{
			puts("wrong hash message");
			drop_conn(newcb);
			return NULL;
		}
		else{
			pthread_mutex_lock(&p2p_mutex);
			memcpy(newcb->peer_id,peer_id,20);
			pthread_mutex_unlock(&p2p_mutex);
			if (!is_connect){
				send_handshake(connfd);
			}
		}
	}
	if (!is_bitfield_empty(globalInfo.bitfield,bit)){
		char msg[5+bit];
        		*(int*)msg= htonl(1+bit);
       		msg[4] = 5;
        		memcpy(msg+5,globalInfo.bitfield,bit);
        		if (send(connfd,msg,5+bit,0) == -1){
            		printf("Error when send: %s", strerror(errno));
            		drop_conn(newcb);
            		return NULL;
        }
	}
}
void send_have(int connfd,int index){
	char msg[9];
	memset(msg,0,sizeof(msg));
	*(int*)msg = htonl(1);
	msg[4] = 4;
	*(int*)(msg+5) = htonl(index);
    	send(connfd,msg,9,0);	
}
void send_request(int connfd,int index,int begin,int length){
	char msg[17];
	memset(msg,0,sizeof(msg));
	*(int*)msg = htonl(13);
	msg[4] = 6;
	*(int*)(msg+5) = htonl(index);
	*(int*)(msg+9) = htonl(begin);
	*(int*)(msg+13) = htonl(length);
    	send(connfd,msg,17,0);	
}
void send_interest(int connfd){
	char msg[5];
	memset(msg,0,sizeof(msg));
	*(int*)msg = htonl(1);
	msg[4] = 2;
    	send(connfd,msg,5,0);
}
void send_choke(int connfd){
	char msg[5];
	memset(msg,0,sizeof(msg));
	*(int*)msg = htonl(1);
	msg[4] = 0;
    	send(connfd,msg,5,0);
}
void send_not_interest(int connfd){
	char msg[5];
	memset(msg,0,sizeof(msg));
	*(int*)msg = htonl(1);
	msg[4] = 3;
    	send(connfd,msg,5,0);
}
void send_msg(int connfd){
	char msg[5];
	memset(msg,0,sizeof(msg));
	*(int*)msg = htonl(1);
	msg[4] = 1;
    	send(connfd,msg,5,0);
}
void send_piece(int connfd,int index,int begin,int length){
	char* block=get_block(index);
	char msg[13];
	*(int*)msg = htonl(9+length);
	msg[4] = 7;
	*(int*)(msg+5) = htonl(index);
	*(int*)(msg+9) = htonl(begin);
    	send(connfd,msg,13,0);
    	send(connfd,block+begin,length,0);
}
void send_handshake(int connfd){
	char* pstr="BitTorrent protocol";
	const int pstrlen=19;
	const int len=49+pstrlen;
	char msg[len];
	memset(msg,0,sizeof(msg));
	char* content=msg;
	content[0]=19;
	content++;
	memcpy(content,pstr,sizeof(char)*pstrlen);
	//to do global hash
	send(connfd,msg,len,0);
}