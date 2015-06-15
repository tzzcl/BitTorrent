#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "util.h"
#include "list.h"
#include "btdata.h"
#include "pwp.h"
#include "bencode.h"
#include "ui.h"
#define DEBUG(x) x
//#define MAXLINE 4096 
// pthread数据
extern ListHead p2p_cb_head;
extern ListHead download_piece_head;
extern pthread_mutex_t p2p_mutex;
extern pthread_mutex_t download_mutex;
extern pthread_mutex_t first_req_mutex;
extern pthread_mutex_t piece_count_mutex;
extern int* piece_counter;
char * name;
int listenfd;
void init()
{
  list_init(&p2p_cb_head);
  list_init(&download_piece_head);
  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_settype(&mutex_attr,PTHREAD_MUTEX_RECURSIVE_NP);	
  pthread_mutex_init(&p2p_mutex,&mutex_attr);
  pthread_mutex_init(&download_mutex,&mutex_attr);
  pthread_mutex_init(&first_req_mutex,&mutex_attr);
  pthread_mutex_init(&piece_count_mutex,&mutex_attr);
  g_done = 0;
  g_tracker_response = NULL;
}
void *show_speed(void *arg){
    int old_download = g_downloaded;
    char info[50];
    for(;;){
        sleep(2);
        int current_download = g_downloaded;
        double speed = (double)(current_download - old_download)/3.0;
        double proportion = (double)current_download/(double)g_torrentmeta->length;
        int index = (proportion >= 1)?0:(49 - (int)(proportion * 50));
        sprintf(info,"speed:%5.1fKB/s", speed / 1024);
        update_speed(info);
        old_download = current_download;
    }
}

void *daemon_listen(void *arg){
    int sockfd = (int)arg;

    printf("daemon thread is running\n");
    for(;;){
        struct sockaddr_in cliaddr;
        socklen_t cliaddr_len;
        cliaddr_len = sizeof(struct sockaddr_in);

        pthread_testcancel();

        int connfd = accept(sockfd, (struct sockaddr *)&cliaddr, &cliaddr_len);
        if (connfd < 0){
            int tmp = errno;
            printf("Error when accept socket:%s\n", strerror(tmp));
            continue;
        }
        printf("receive a connect from %s\n", inet_ntoa(cliaddr.sin_addr));
        pthread_t tid;
        p2p_thread *param = (p2p_thread *)malloc(sizeof(p2p_thread));
        param->connfd = connfd;
        param->is_connect = 0;
        strcpy(param->ip, inet_ntoa(cliaddr.sin_addr));
        if (pthread_create(&tid, NULL, p2p_run_thread, param) != 0){
            printf("Error when create thread accept request\n");
        } else {
            printf("Success create thread to accept request\n");
        }
    }

    return NULL;
}
int main(int argc, char **argv) 
{
  int sockfd = -1;
  char rcvline[MAXLINE];
  char tmp[MAXLINE];
  int i;
 
// 注意: 你可以根据自己的需要修改这个程序的使用方法
  if(argc < 4)
  {
    printf("Usage: SimpleTorrent <torrent file> <ip of this machine (XXX.XXX.XXX.XXX form)> <downloaded file location> [isseed]\n");
    printf("\t isseed is optional, 1 indicates this is a seed and won't contact other clients\n");
    printf("\t 0 indicates a downloading client and will connect to other clients and receive connections\n");
    exit(-1);
  }
  name=argv[3];
  int iseed = 0;
  if( argc > 4 ) {
    iseed = !!atoi(argv[4]);
  }
    
  if( iseed ) {
    printf("SimpleTorrent running as seed.\n");
  }
  
  init();
  srand(time(NULL));

  int val[5];
  for(i=0; i<5; i++)
  {
    val[i] = rand();
  }
  memcpy(g_my_id,(char*)val,20);
  DEBUG(for (int i=0;i<20;i++) printf("%02X ",(unsigned char)g_my_id[i]);puts("");)
  strncpy(g_my_ip,argv[2],strlen(argv[2]));
  g_my_ip[strlen(argv[2])+1] = '\0';
  
  g_filename = argv[3];

  g_torrentmeta = parsetorrentfile(argv[1]);
  memcpy(g_infohash,g_torrentmeta->info_hash,20);

  g_filelen = g_torrentmeta->length;
  g_num_pieces = g_torrentmeta->num_pieces;

  for (i = 0; i <g_torrentmeta->filenum; i++){
        g_torrentmeta->flist[i].fp = createfile(g_torrentmeta->flist[i].filename, g_torrentmeta->flist[i].size);
    }
   g_bitfield = gen_bitfield(g_torrentmeta->pieces,g_torrentmeta->piece_len, g_torrentmeta->num_pieces);
  g_filedata = (char*)malloc(g_filelen*sizeof(char));
  piece_counter = (int *)malloc(sizeof(int)*g_torrentmeta->num_pieces);
  memset(piece_counter,0,sizeof(int)*g_torrentmeta->num_pieces);
  announce_url_t* announce_info;
  announce_info = parse_announce_url(g_torrentmeta->announce);
  // 提取tracker url中的IP地址
  printf("HOSTNAME: %s\n",announce_info->hostname);
  struct hostent *record;
  record = gethostbyname(announce_info->hostname);
  if (record==NULL)
  { 
    printf("gethostbyname(%s) failed", announce_info->hostname);
    exit(1);
  }
  struct in_addr* address;
  address =(struct in_addr * )record->h_addr_list[0];
  printf("Tracker IP Address: %s\n", inet_ntoa(* address));
  strcpy(g_tracker_ip,inet_ntoa(*address));
  g_tracker_port = announce_info->port;

  free(announce_info);
  announce_info = NULL;
  g_peerport=rand()%10000+10000;
  listenfd=make_listen_port(g_peerport);
  if (listenfd==0)
  {
    printf("Error when create socket for binding:%s\n", strerror(errno));
    exit(-1);
  }
  pthread_t p_daemon;
  if (pthread_create(&p_daemon, NULL, daemon_listen, (void *)listenfd) != 0){
        int tmp = errno;
        printf("Error when create daemon thread: %s\n", strerror(tmp));
        return -1;
  }
  pthread_t p_speed;
  if (pthread_create(&p_speed, NULL, show_speed, NULL) != 0){
        int tmp = errno;
        printf("Error when create show_speed thread: %s\n", strerror(tmp));
        return -1;
  }
  // 初始化
  // 设置信号句柄
  signal(SIGINT,client_shutdown);

  // 设置监听peer的线程

  // 定期联系Tracker服务器
  int firsttime = 1;
  int mlen;
  char* MESG;
  MESG = make_tracker_request(BT_STARTED,&mlen);
  while(!g_done)
  {
    if(sockfd <= 0)
    {
      //创建套接字发送报文给Tracker
      printf("Creating socket to tracker...\n");
      sockfd = connect_to_host(g_tracker_ip, g_tracker_port);
    }
    
    printf("Sending request to tracker...\n");
    
    if(!firsttime)
    {
      free(MESG);
      // -1 指定不发送event参数
      MESG = make_tracker_request(-1,&mlen);
      printf("MESG: ");
      for(i=0; i<mlen; i++)
        printf("%c",MESG[i]);
      printf("\n");
    }
    send(sockfd, MESG, mlen, 0);
    firsttime = 0;
    
    memset(rcvline,0x0,MAXLINE);
    memset(tmp,0x0,MAXLINE);
    
    // 读取并处理来自Tracker的响应
    tracker_response* tr;
    tr = preprocess_tracker_response(sockfd); 
   
    // 关闭套接字, 以便再次使用
    shutdown(sockfd,SHUT_RDWR);
    close(sockfd);
    sockfd = 0;

    printf("Decoding response...\n");
    char* tmp2 = (char*)malloc(tr->size*sizeof(char));
    memcpy(tmp2,tr->data,tr->size*sizeof(char));

    printf("Parsing tracker data\n");
    g_tracker_response = get_tracker_data(tmp2,tr->size);
    
    if(tmp[0])
    {
      free(tmp2);
      tmp2 = NULL;
    }

    printf("Num Peers: %d\n",g_tracker_response->numpeers);
    for(i=0; i<g_tracker_response->numpeers; i++)
    {
      //printf("Peer id: %d\n",g_tracker_response->peers[i].id);
      printf("Peer id: ");
      int idl;
      for(idl=0; idl<20; idl++)
        printf("%02X ",(unsigned char)g_tracker_response->peers[i].id[idl]);
      printf("\n");
      printf("Peer ip: %s\n",g_tracker_response->peers[i].ip);
      printf("Peer port: %d\n",g_tracker_response->peers[i].port);
    }
	piece = malloc(g_torrentmeta->num_pieces+1);
	piece[g_torrentmeta->num_pieces]=0;
	for (i=0;i<g_torrentmeta->num_pieces;++i) {
		unsigned char ch = g_bitfield[i/8];
 		int offset = 7 - i%8;
		if ((ch >> offset) & 1) piece[i]='@';
		else piece[i]='-';
	}
	init_window(g_torrentmeta->name);
    for (i = 0; i <g_tracker_response->numpeers; i++){
            if (!valid_ip(g_tracker_response->peers[i].ip)){
                pthread_t tid;
                p2p_thread *param = (p2p_thread *)malloc(sizeof(p2p_thread));
                param->is_connect = 1;
                param->port = g_tracker_response->peers[i].port;
                strcpy(param->ip,g_tracker_response->peers[i].ip);
                if (pthread_create(&tid, NULL, p2p_run_thread, param) != 0){
      //              printf("Error when create thread to connect peer\n");
                } else {
      //              printf("Success create thread to connect peer %s\n", g_tracker_response->peers[i].ip);
                }
            }
        }
	char line[100];
        sprintf(line,"sleep %d seconds\n", g_tracker_response->interval);
	update_info(line);
    // 必须等待td->interval秒, 然后再发出下一个GET请求
    sleep(g_tracker_response->interval);

  }
 
  // 睡眠以等待其他线程关闭它们的套接字, 只有在用户按下ctrl-c时才会到达这里
  sleep(2);

  exit(0);
}
