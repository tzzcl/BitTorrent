#ifndef _FILE_H_
#define _FILE_H_
#include <stdio.h>

struct fileinfo_t{
	FILE* fp;
	char filename[80];
	int begin_index;
	int size;
};
typedef struct file_info_t file_info;
int filesize(FILE *fp);
int exists(char *filepath);
FILE *createfile(char *filepath, int size);
int get_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int store_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int get_sub_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size);
int store_sub_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size);
void get_block(int index, int begin, int length, char *block);
void set_block(int index, int begin, int length, char *block);
char *gen_bitfield(char *piece_hash, int piece_len, int piece_num);
#endif