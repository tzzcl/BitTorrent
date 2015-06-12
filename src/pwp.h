#ifndef _PWP_H_
#define _PWP_H_
#include "list.h"
#include <pthread.h>
#define PEER_PORT 10970
#define SUB_PIECE_SIZE 16384
#define MAX_REQUEST 5
#define MAX_PEER 1000

typedef struct control_block{
	ListHead list;
	int connfd;
	int self_choke;
	int self_interest;
	int peer_choke;
	int peer_interest;
	char peer_id[20];
	char peer_ip[20];
	char* peer_field;
} p2p_cb;
typedef struct torrent_info{
	char info_hash[20];
	char peer_id[20];
	int piece_num;
	char* bitfield;
}torrent_info;
typedef struct p2p_thread{
	int connfd;
	int is_connect;
	char ip[20];
	int port;
}p2p_thread;
typedef struct download_piece{
	int index;
	int sub_piece_size;
	int sub_piece_num;
	int* sub_piece_state;
	int download_num;
	ListHead list;
}download_piece;


void* p2p_run_thread(void* param);
#endif