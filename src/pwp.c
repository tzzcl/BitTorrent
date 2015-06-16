#include "pwp.h"
#include "file.h"
#include "util.h"
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
#include "ui.h"
#define DEBUG(x) 
ListHead p2p_cb_head;
ListHead download_piece_head;
int first_req=1;
int* piece_counter;
pthread_mutex_t p2p_mutex;
pthread_mutex_t download_mutex;
pthread_mutex_t first_req_mutex;
pthread_mutex_t piece_count_mutex;
int listenfd;
static const unsigned char set_bit[8] = {1,2,4,8,16,32,64,128};
void send_have(int connfd,int index){
	DEBUG(printf("%s:%d %d\n",__FUNCTION__,connfd,index);)
	char* msg=malloc(sizeof(char)*9);
	*(int*)msg = htonl(5);
	msg[4] = 4;
	*(int*)(msg+5) = htonl(index);
    	send(connfd,msg,9,0);	
    	free(msg);
}
void send_request(int connfd,int index,int begin,int length){
	DEBUG(printf("%s:%d %d %d %d\n",__FUNCTION__,connfd,index,begin,length);)
	char* msg=malloc(sizeof(char)*17);
	*(int*)msg = htonl(13);
	msg[4] = 6;
	*(int*)(msg+5) = htonl(index);
	*(int*)(msg+9) = htonl(begin);
	*(int*)(msg+13) = htonl(length);
    	send(connfd,msg,17,0);	
    	free(msg);
}
void send_interest(int connfd){
	DEBUG(puts(__FUNCTION__);)
	char* msg=malloc(sizeof(char)*5);
	*(int*)msg = htonl(1);
	msg[4] = 2;
    	send(connfd,msg,5,0);
    	free(msg);
}
void send_choke(int connfd){
	DEBUG(puts(__FUNCTION__);)
	char* msg=malloc(sizeof(char)*5);
	*(int*)msg = htonl(1);
	msg[4] = 0;
    	send(connfd,msg,5,0);
    	free(msg);
}
void send_not_interest(int connfd){
	DEBUG(puts(__FUNCTION__);)
	char* msg=malloc(sizeof(char)*5);
	*(int*)msg = htonl(1);
	msg[4] = 3;
    	send(connfd,msg,5,0);
    	free(msg);
}
void send_msg(int connfd){
	DEBUG(puts(__FUNCTION__);)
	char* msg=malloc(sizeof(char)*5);
	*(int*)msg = htonl(1);
	msg[4] = 1;
    	send(connfd,msg,5,0);
    	free(msg);
}
void send_piece(int connfd,int index,int begin,int length){
	DEBUG(puts(__FUNCTION__);)
	char* block=malloc(sizeof(char)*length);
	get_block(index,begin,length,block);
	char* msg=malloc(sizeof(char)*13);
	*(int*)msg = htonl(9+length);
	msg[4] = 7;
	*(int*)(msg+5) = htonl(index);
	*(int*)(msg+9) = htonl(begin);
    	send(connfd,msg,13,0);
    	send(connfd,block+begin,length,0);
    	free(block);
    	free(msg);
}
void send_handshake(int connfd){
	DEBUG(puts(__FUNCTION__);)
	char* pstr="BitTorrent protocol";
	const int pstrlen=19;
	const int len=49+pstrlen;
	char msg[len];
	memset(msg,0,sizeof(msg));
	msg[0]=19;
	memcpy(msg+1,pstr,sizeof(char)*pstrlen);
	int tmphash[5];
    	int i;
    	for (i = 0; i < 5; i++){
        		tmphash[i] = htonl(g_torrentmeta->info_hash[i]);
    	}
    	memcpy(msg+9+pstrlen,tmphash,20);
    	memcpy(msg+29+pstrlen,g_my_id,20);
	//to do global hash
	send(connfd,msg,len,0);
}

static int readn(int fd,void* content,size_t len)
{
	int state=0;
	int cnt=len;
	char* temp=(char*)content;
	while (cnt>0){
		state=recv(fd,temp,cnt,0);
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
		temp+=state;
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
    int offset = 7 - index%8;
    if(bit)
        bitfield[index/8] = bitfield[index/8] | set_bit[offset];
    else
        bitfield[index/8] = bitfield[index/8] & (~set_bit[offset]);
}
static inline void safe_free(void* content){
	if (content!=NULL)
		free(content);
}
static inline void drop_conn(p2p_cb* nowcb)
{
	pthread_mutex_lock(&p2p_mutex);
	list_del(&nowcb->list);
	pthread_mutex_unlock(&p2p_mutex);
	safe_free(nowcb->peer_field);
	close(nowcb->connfd);
	safe_free(nowcb);
}
static inline int is_bitfield_complete(char *bitfield){
    int len=g_torrentmeta->num_pieces/8;

    for(int i = 0; i < len; i++){
        if (bitfield[i]!=0xff) return 0;
    }
    if (g_torrentmeta->num_pieces%8!=0)
    {
    	int size=g_torrentmeta->num_pieces%8;
    	int begin=g_torrentmeta->num_pieces/8*8;
    	for (int i=begin;i<begin+size;i++)
    		if (get_bit_at_index(bitfield,i)==0) return 0;
    }
    return 1;
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
static int real_piece_len(int index){
	int piece_num = g_torrentmeta->num_pieces;
    	int piece_len = g_torrentmeta->piece_len;
    	int total_len = g_torrentmeta->length;
    	int real_piece_len = (index == piece_num-1)?(total_len - index * piece_len):(piece_len);
    	return real_piece_len;
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
int select_piece(){//least first
	int min_index=-1,min_val=1e8;
	for (int i=0;i<g_torrentmeta->num_pieces;i++)
	{
		if (get_bit_at_index(g_bitfield,i)==0)
		{
			if (piece_counter[i]!=0&&piece_counter[i]<min_val){
				min_val=piece_counter[i];
				min_index=i;
			}
		}
	}
	return min_index;
}
int is_interested_bitfield(char *bitfield1, char *bitfield2, int len){
    char* interest_bitfield=malloc(sizeof(char)*len);
    for(int i = 0; i < len; i++){
        interest_bitfield[i] = (~bitfield1[i]) & bitfield2[i]; 
    }
    if(is_bitfield_empty(interest_bitfield,len))
        return 0;
    else
        return 1;
    free(interest_bitfield);
}
download_piece* find_download_piece(int index){
	ListHead* ptr;
	pthread_mutex_lock(&download_mutex);
	list_foreach(ptr,&download_piece_head){
        	download_piece *tmp = list_entry(ptr,download_piece,list);
        	//printf("downloading piece %d\n",tmp->index);
        	if(tmp->index == index){
              	pthread_mutex_unlock(&download_mutex);
              	return tmp;
        	}
   	}
	pthread_mutex_unlock(&download_mutex);
	return NULL;
}
download_piece *init_download_piece(int index){
	DEBUG(printf("%d %p\n",index,find_download_piece(index));)
	if (find_download_piece(index)!=NULL) return NULL;
	download_piece* now=malloc(sizeof(download_piece));
	now->index=index;
	now->sub_piece_size = SUB_PIECE_SIZE;
	int real_len=real_piece_len(index);
	int tmp1 = real_len/now->sub_piece_size;
    	int tmp2 = (real_len%now->sub_piece_size != 0);
    	now->sub_piece_num=tmp1+tmp2;
    	now->download_num=0;
    	now->sub_piece_state=(int*)malloc(4*now->sub_piece_num);
    	memset(now->sub_piece_state,0,sizeof(int)*now->sub_piece_num);
    	list_init(&now->list);
    	pthread_mutex_lock(&download_mutex);
    	list_add_before(&download_piece_head,&now->list);
	pthread_mutex_unlock(&download_mutex);
	return now;
}
int select_next_subpiece(int index,int* begin,int* length){
	ListHead* ptr;
	pthread_mutex_lock(&download_mutex);
	list_foreach(ptr,&download_piece_head){
		download_piece* d_piece=list_entry(ptr,download_piece,list);
		int rest=real_piece_len(index)%d_piece->sub_piece_size;
		if (d_piece->index==index){
			for (int i=0;i<d_piece->sub_piece_num;i++)
			{
				if (d_piece->sub_piece_state[i]==0)
				{
					*begin=i*d_piece->sub_piece_size;
					if (i==d_piece->sub_piece_num-1&&rest!=0)
					{
						*length=rest;
						pthread_mutex_unlock(&download_mutex);
						return 1;
					}
					else
					{
						*length=d_piece->sub_piece_size;
						pthread_mutex_unlock(&download_mutex);
						return 1;
					}
				}
			}
			for (int i=0;i<d_piece->sub_piece_num;i++)
			{
				if (d_piece->sub_piece_state[i]==1)
				{
					*begin=i*d_piece->sub_piece_size;
					int rest=g_torrentmeta->piece_len%d_piece->sub_piece_size;
					if (i==d_piece->sub_piece_num-1&&rest!=0)
					{
						*length = rest;
						pthread_mutex_unlock(&download_mutex);
						return 1;
					}
					else{
						*length=d_piece->sub_piece_size;
						pthread_mutex_unlock(&download_mutex);
						return 1;
					}
				}
			}
		}
	}
	pthread_mutex_unlock(&download_mutex);
	return 0;
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
			char line[100];
            			sprintf(line,"Error when connect to peer %s:%d, reason:%s\n", current->ip, current->port, strerror(tmp));
				update_info(line);
            			return NULL;
		}
		current->connfd=connfd;
	}
	int is_connect=current->is_connect;
	p2p_cb* newcb=new_init_p2p();
	newcb->connfd=connfd;
	strcpy(newcb->peer_ip,current->ip);
	safe_free(current);
	int bit=g_torrentmeta->num_pieces/8+(g_torrentmeta->num_pieces%8>0);
	newcb->peer_field=(char*)malloc(bit);
	memset(newcb->peer_field,0,bit);
	pthread_mutex_lock(&p2p_mutex);
	list_add_before(&p2p_cb_head,&newcb->list);
	pthread_mutex_unlock(&p2p_mutex);
	if (is_connect){
		update_info("send a handshake");
		//puts("send a handshake");
		send_handshake(connfd);
	}
	char len;
	if (readn(connfd,&len,1)>0)
	{
		DEBUG(printf("handshake received %d\n",len);)
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
		if (memcmp(info_hash,g_torrentmeta->info_hash,20)!=0)
		{
			update_info("wrong hash message");
			//puts("wrong hash message");
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
	if (!is_bitfield_empty(g_bitfield,bit)){
		char msg[5+bit];
        		*(int*)msg= htonl(1+bit);
       		msg[4] = 5;
        		memcpy(msg+5,g_bitfield,bit);
        		if (send(connfd,msg,5+bit,0) == -1){
					char line[100];
            		sprintf(line,"Error when send: %s", strerror(errno));
					update_info(line);
            		drop_conn(newcb);
            		return NULL;
        		}
	}
	char pre[5];
	while (readn(connfd,pre,4)>0)
	{
		int len=ntohl(*(int*)pre);
		DEBUG(printf("len %d\n",len);)
		if (len==0){
			continue;
		}
		else{
			readn(connfd,pre+4,1);
		}
		switch (pre[4]){
			case 0:{//choke
				DEBUG(puts("choke");)
				pthread_mutex_lock(&p2p_mutex);
				newcb->self_choke=1;
				pthread_mutex_unlock(&p2p_mutex);
				break;
			}
			case 1:{//unchoke
				DEBUG(puts("unchoke");)
				pthread_mutex_lock(&p2p_mutex);
				newcb->self_choke=0;
				pthread_mutex_unlock(&p2p_mutex);
				break;
			}
			case 2:{//interest
				DEBUG(puts("interest");)
				pthread_mutex_lock(&p2p_mutex);
				newcb->self_interest=1;
				newcb->self_choke=0;
				pthread_mutex_unlock(&p2p_mutex);
				break;
			}
			case 3:{
				DEBUG(puts("not_interest");)
				pthread_mutex_lock(&p2p_mutex);
				newcb->self_interest=0;
				pthread_mutex_unlock(&p2p_mutex);
				break;
			}
			case 4:{//have
				int index;
				readn(connfd,&index,4);
				index=ntohl(index);
				DEBUG(printf("have:%d\n",index);)
				set_bit_at_index(newcb->peer_field,index,1);
				pthread_mutex_lock(&piece_count_mutex);
				piece_counter[index]++;
				pthread_mutex_unlock(&piece_count_mutex);
				if (get_bit_at_index(g_bitfield,index)==0){//send interest
					send_interest(connfd);
					pthread_mutex_lock(&p2p_mutex);
					newcb->peer_interest=1;
					pthread_mutex_unlock(&p2p_mutex);
					pthread_mutex_lock(&first_req_mutex);
					if (first_req==1){
						pthread_mutex_unlock(&first_req_mutex);
						pthread_mutex_lock(&p2p_mutex);
						if (newcb->self_choke==0&&newcb->self_interest==1)
						{
							int begin,length;
							select_next_subpiece(index,&begin,&length);
							send_request(connfd,index,begin,length);
							first_req=0;
							download_piece* d_piece=init_download_piece(index);
							if (d_piece==NULL) 
							{
								pthread_mutex_unlock(&p2p_mutex);
								continue;
							}
							pthread_mutex_lock(&download_mutex);
							d_piece->download_num++;
							pthread_mutex_unlock(&download_mutex);
							int subpiece_index=begin/d_piece->sub_piece_size;
							d_piece->sub_piece_state[subpiece_index]=1;
						}
						pthread_mutex_unlock(&p2p_mutex);

					}
					else{
						pthread_mutex_unlock(&first_req_mutex);
						download_piece* d_piece=find_download_piece(index);
						pthread_mutex_lock(&p2p_mutex);
						pthread_mutex_lock(&download_mutex);

						if (d_piece!=NULL&&d_piece->download_num<MAX_REQUEST
							&&d_piece->download_num!=0
							&&newcb->self_choke==0
							&&newcb->peer_interest==1)
							{
								int begin,length;
								select_next_subpiece(index,&begin,&length);
								send_request(connfd,index,begin,length);
								d_piece->download_num++;
								int subpiece_index=begin/d_piece->sub_piece_size;
								d_piece->sub_piece_state[subpiece_index]=1;
							}
						pthread_mutex_unlock(&p2p_mutex);
						pthread_mutex_unlock(&download_mutex);
					}
				}
				break;
			}
			case 5:{
				DEBUG(puts("bitfield");)
				char field[len-1];
				if (len-1!=bit)
				{
					update_info("wrong bitfield");
					//puts("wrong bitfield");
					drop_conn(newcb);
					return NULL;
				}
				readn(connfd,field,len-1);
				
				unsigned char ch=field[len-2];
				int offset = 8 - g_torrentmeta->num_pieces%8;
				while (offset>=1&&offset<8)
				{
					if ((ch>>(offset-1))&1)
					{
						 puts("wrong idle bits");
                                 				drop_conn(newcb);
                                 				return NULL;
					}
					offset--;
				}
				pthread_mutex_lock(&p2p_mutex);
				memcpy(newcb->peer_field,field,len-1);
				pthread_mutex_unlock(&p2p_mutex);
				pthread_mutex_lock(&piece_count_mutex);
				for(int i = 0; i < g_torrentmeta->num_pieces; i++){
                             			if(get_bit_at_index(field,i) == 1)
                                 				piece_counter[i] ++;
                         			}
				pthread_mutex_unlock(&piece_count_mutex);
				pthread_mutex_lock(&p2p_mutex);
				char* first_bitfield=g_bitfield;
				char* second_bitfield=newcb->peer_field;
				if (is_interested_bitfield(first_bitfield,second_bitfield,bit))
				{
					send_interest(connfd);
					newcb->peer_interest=1;
					newcb->self_choke=0;
					pthread_mutex_lock(&first_req_mutex);
					if (first_req==1)
					{
						pthread_mutex_unlock(&first_req_mutex);
						for (int i=0;i<g_torrentmeta->num_pieces;i++)
						{
							if (get_bit_at_index(first_bitfield,i)==0
							&&get_bit_at_index(second_bitfield,i)==1
							&&newcb->self_choke==0
							&&newcb->peer_interest==1)
							{
								download_piece* d_piece=init_download_piece(i);
								if (d_piece==NULL) 
								{
								pthread_mutex_unlock(&p2p_mutex);
								continue;
								}
								pthread_mutex_lock(&download_mutex);
								d_piece->download_num++;
								pthread_mutex_unlock(&download_mutex);
								int begin,length;
								select_next_subpiece(i,&begin,&length);
								send_request(connfd,i,begin,length);
								int subpiece_index=begin/d_piece->sub_piece_size;
								d_piece->sub_piece_state[subpiece_index]=1;
								pthread_mutex_unlock(&p2p_mutex);
								break;
							}

						}
					}
					else
					{
						pthread_mutex_unlock(&first_req_mutex);
						pthread_mutex_unlock(&p2p_mutex);
						for (int i=0;i<g_torrentmeta->num_pieces;i++)
						{
							if (get_bit_at_index(first_bitfield,len)==0
							&&get_bit_at_index(second_bitfield,len)==1)
							{
								download_piece* d_piece=find_download_piece(i);
								if (d_piece==NULL)
									continue;
								pthread_mutex_lock(&download_mutex);
								if (d_piece->download_num<MAX_REQUEST
								&&d_piece->download_num!=0
								&&newcb->self_choke==0
								&&newcb->peer_interest==1)
								{
									int begin,length;
									if (select_next_subpiece(i,&begin,&length))
									{
										send_request(connfd,i,begin,length);
										 int subpiece_index = begin/d_piece->sub_piece_size;
                                                 							d_piece->sub_piece_state[subpiece_index] = 1;
									}
								}
								pthread_mutex_unlock(&download_mutex);
							}
						}
					}
				}
				pthread_mutex_unlock(&p2p_mutex);
				break;
			}
			case 6:{
				DEBUG(puts("request");)
				int temp[3];
				readn(connfd,temp,12);
				int index=ntohl(temp[0]);
				int begin=ntohl(temp[1]);
				int length=ntohl(temp[2]);
				if (length>(1<<17))
				{
					update_info("the length in request is larger than 2^17");
					//puts("the length in request is larger than 2^17");
                             			drop_conn(newcb);
                             			return NULL ;
				}
				pthread_mutex_lock(&p2p_mutex);
				if (newcb->self_interest==1&&newcb->peer_choke==0)
				{
					send_piece(connfd,index,begin,length);
				}
				else
				{
					update_info("invalid request");
					//puts("invalid request");
				}
				pthread_mutex_unlock(&p2p_mutex);
				break;
			}
			case 7:{//piece
				DEBUG(puts("piece");)
				char payload[len-1];
				readn(connfd,payload,len-1);
				int index = ntohl(*(int*)payload);
				piece[index]='@';
                        			int begin = ntohl(*(int*)(payload+4));
                        			int length = len - 9;
                        			download_piece* d_piece=find_download_piece(index);
                        			if (d_piece==NULL) continue;
                        			pthread_mutex_lock(&download_mutex);
                        			set_block(index,begin,length,payload+8);
                        			g_downloaded+=length;
                        			int subpiece_index=begin/d_piece->sub_piece_size;
                        			d_piece->sub_piece_state[subpiece_index]=2;
                        			if (!select_next_subpiece(index,&begin,&length))
                        			{
							char line[100];
                        				sprintf(line,"piece %d has been downloaded successfully\n",index);
							update_info(line);
                        				set_bit_at_index(g_bitfield,index,1);
                        				list_del(&d_piece->list);
                        				safe_free(d_piece->sub_piece_state);
                        				safe_free(d_piece);
                        				pthread_mutex_unlock(&download_mutex);
                        				ListHead* ptr;
                        				pthread_mutex_lock(&p2p_mutex);
                        				list_foreach(ptr,&p2p_cb_head){
                        					p2p_cb* tempp2p=list_entry(ptr,p2p_cb,list);
                        					send_have(tempp2p->connfd,index);
                        				}
                        				pthread_mutex_unlock(&p2p_mutex);
                        				if (is_bitfield_complete(g_bitfield))
                        				{
                        					//puts("File Download Complete!");
								update_info("File Download Complete!");
                        					ListHead* ptr;
                        					pthread_mutex_lock(&p2p_mutex);
                        					list_foreach(ptr,&p2p_cb_head)
                        					{
                        						p2p_cb* temp=list_entry(ptr,p2p_cb,list);
                        						if (temp->peer_interest==1)
                        						{
                        							temp->peer_interest=0;
                        							send_not_interest(temp->connfd);
                        						}
                        					}
                        					pthread_mutex_unlock(&p2p_mutex);
                        					continue;
                        				}
                        				int next_index=select_piece();
                        				download_piece* next_d_piece=NULL;
                        				if (next_index!=-1)
                        					next_d_piece=init_download_piece(next_index);
                        				else
                        				{
                        					pthread_mutex_lock(&first_req_mutex);
                        					first_req=1;
                        					pthread_mutex_unlock(&first_req_mutex);
                        				}
                        				pthread_mutex_lock(&p2p_mutex);
                        				list_foreach(ptr,&p2p_cb_head)
                        				{
                        					p2p_cb* temp=list_entry(ptr,p2p_cb,list);
                        					char* first_bitfield=g_bitfield;
                        					char* second_bitfield=temp->peer_field;
                        					if (!is_interested_bitfield(first_bitfield,second_bitfield,bit)
                        						&&temp->peer_interest==1)
                        					{
                        						send_not_interest(temp->connfd);
                        						temp->peer_interest=0;
                        					}
                        					if (next_index==-1)
                        					{
                        						pthread_mutex_unlock(&p2p_mutex);
                        						continue;
                        					}
                        					static int no_sub_piece=0;
                        					if (no_sub_piece){
                        						pthread_mutex_unlock(&p2p_mutex);
                        						continue;
                        					}
                        					pthread_mutex_lock(&download_mutex);
                        					if (next_d_piece!=NULL&&next_d_piece->download_num<MAX_REQUEST
                        						&&get_bit_at_index(second_bitfield,next_index)==1
                        						&&temp->self_choke==0&&temp->peer_interest==1)
                        					{
                        						int begin1,length1;
                        						if (!select_next_subpiece(next_index,&begin1,&length1))
                        							no_sub_piece=1;
                        						DEBUG(printf("%d\n",no_sub_piece);)
                        						send_request(temp->connfd,next_index,begin1,length1);

                        						next_d_piece->download_num++;
                        						int subpiece_index=begin1/next_d_piece->sub_piece_size;
                        						next_d_piece->sub_piece_state[subpiece_index]=1;
                        					}
                        					pthread_mutex_unlock(&download_mutex);
                        				}
                        				pthread_mutex_unlock(&p2p_mutex);
                        			}
                        			else
                        			{
                        				pthread_mutex_lock(&p2p_mutex);
                        				if (newcb->peer_interest==1&&newcb->self_choke==0)
                        				{
                        					send_request(connfd,index,begin,length);
                        					int subpiece_index=begin/d_piece->sub_piece_size;
                        					d_piece->sub_piece_state[subpiece_index]=1;
                        				}
                        				pthread_mutex_unlock(&p2p_mutex);
                        				pthread_mutex_unlock(&download_mutex);
                        			}
				break;
			}
			case 8:{
				break;
			}
		}
	}
	pthread_mutex_lock(&piece_count_mutex);
    	for(int i = 0; i < g_torrentmeta->num_pieces; i++){
        	if(get_bit_at_index(newcb->peer_field,i) == 1)
            		piece_counter[i] --;
    	}
	pthread_mutex_unlock(&piece_count_mutex);
	pthread_mutex_lock(&p2p_mutex);
	list_del(&newcb->list);
	pthread_mutex_unlock(&p2p_mutex);
	safe_free(newcb->peer_field);
	safe_free(newcb);
	update_info("quit p2p success");
	//puts("quit p2p success");
	return NULL;

}
