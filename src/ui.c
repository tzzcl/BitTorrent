#include "ui.h"

WINDOW *title,*pieces,*speed,*infow;
char *piece;
char *info[12];

void *update_download() {
	for (;;) {
		int i,len=strlen(piece),line=0;
		for (i=0;i<len;i++) {
			if (i%(COLS-2)==0) move(5+(++line),1);
			addch(piece[i]);
		}
		refresh();
		usleep(5000);
	}
}

void init_window(const char* filename) {
	initscr();
	refresh();
	title = newwin(3,COLS,0,0);
	curs_set(0);
	noecho();
	box(title,ACS_VLINE,ACS_HLINE);
	mvwaddstr(title,1,1,"BitTorrent Client");
	wrefresh(title);
	pieces = newwin(LINES-20,COLS,3,0);
	box(pieces,ACS_VLINE,ACS_HLINE);
	mvprintw(4,1,"Filename: %s",filename);
	wrefresh(pieces);
	infow = newwin(14,COLS,LINES-17,0);
	box(infow,ACS_VLINE,ACS_HLINE);
	wrefresh(infow);
	speed = newwin(3,COLS,LINES-3,0);
	box(speed,ACS_VLINE,ACS_HLINE);
	wrefresh(speed);
	pthread_t p_update;
	pthread_create(&p_update,NULL,update_download,NULL);
	memset(info,NULL,sizeof(info));
}

void update_speed(const char* info) {
	mvprintw(LINES-2,1,"%s",info);
	refresh();
}

void update_info(const char* newinfo) {
	for (int i=11;i>0;--i) {
		if (info[i]!=NULL) free(info[i]);
		if (info[i-1]!=NULL) {
			info[i]=malloc(strlen(info[i-1])+1);
			strcpy(info[i],info[i-1]);
		}
	}
	if (info[0]!=NULL) free(info[0]);
	info[0]=malloc(strlen(newinfo)+1);
	strcpy(info[0],newinfo);
	for (int i=11;i>=0;--i) {
		if (info[i]!=NULL) mvaddstr(LINES-5-i,1,info[i]);
	}
	refresh();
}

void exit_window() {
	wclear(title);
	wclear(pieces);
	wclear(infow);
	wclear(speed);
	delwin(title);
	delwin(pieces);
	delwin(infow);
	delwin(speed);
	curs_set(1);
	echo();
	clear();
	refresh();
	endwin();
}


