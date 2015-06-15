
#include <curses.h>
#include <pthread.h>
#include <string.h>

#ifndef _UI_H_
#define _UI_H_


extern char *piece;

void init_window(const char *);
void exit_window();
void update_speed(const char *);
void update_info(const char *);

#endif

