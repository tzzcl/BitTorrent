#ifndef _FILE_H_
#define _FILE_H_
#include <stdio.h>
char* get_block(int index);
void set_block(int index,int begin,int length,char* block);
#endif