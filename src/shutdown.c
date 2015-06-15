#include "btdata.h"
#include "util.h"
#include "ui.h"
extern int g_done;
extern int listenfd;
// 正确的关闭客户端
void client_shutdown(int sig)
{
  // 设置全局停止变量以停止连接到其他peer, 以及允许其他peer的连接. Set global stop variable so that we stop trying to connect to peers and
  // 这控制了其他peer连接的套接字和连接到其他peer的线程.
	exit_window();
	g_done=1;
    int sockfd = connect_to_host(g_tracker_ip, g_tracker_port);
	
    int request_len = 0;
    char *request = make_tracker_request(BT_STOPPED, &request_len);
    send(sockfd, request, request_len, 0);
    free(request);
    close(sockfd);
    close(g_peerport);
    close(listenfd);
    int i;
    for (i = 0; i <g_torrentmeta->filenum; i++){
		if (g_torrentmeta->flist[i].fp!=NULL)
        	fclose(g_torrentmeta->flist[i].fp);
    }
    sleep(1);
    exit(0);
}
