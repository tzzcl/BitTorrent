#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <arpa/inet.h>
#include "file.h"
#include "sha1.h"
#include "info.h"
static char set_bit[8] = {1,2,4,8,16,32,64,128};

static inline void set_bit_at_index(char *info, int index, int bit){
    assert(bit == 0 || bit == 1);
    int offset = 7 - index%8;
    if(bit)
        info[index/8] = info[index/8] | set_bit[offset];
    else
        info[index/8] = info[index/8] & (~set_bit[offset]);
}
int filesize(FILE* fp){
	int cur=ftell(fp);
	fseek(fp,0,SEEK_END);
	int size=ftell(fp);
	fseek(fp,cur,SEEK_SET);
	return size;
}
int exist(char* filepath){
	if (access(filepath,F_OK)!=-1)
		return 1;
	return 0;
}
FILE* createfile(char* filepath,int size){
	if (!exist(filepath))
	{
		printf("create %s\n",filepath);
		int i=strlen(filepath)-1;
		for (;i>=0;i--)
			if (filepath[i]=='/') break;
		if (i>=0)
		{
			char dir[80];
			memset(dir,0,80);
			memcpy(dir,filepath,i+1);
			if (!exist(dir)){
				printf("create dir %s\n",dir);
				mkdir(dir,0777);
			}
		}
		int fd=creat(filepath,0644);
		if (fd==-1)
		{
			int temp=errno;
			printf("%s\n", strerror(temp));
            			return NULL;
		}
		close(fd);
	}
	FILE* fp=fopen("filepath","r+");
	if (fp==NULL)
	{
		int temp=errno;
		printf("%s\n", strerror(temp));
            		return NULL;
	}
	int nowsize=filesize(fp);
	if (nowsize<size)
	{
		fseek(fp,size-1,SEEK_SET);
		fputc(' ',fp);
	}
	fseek(fp,0,SEEK_SET);
	return fp;
}
char* get_block(int index){
	return NULL;
}
void set_block(int index,int begin,int length,char* block){

}