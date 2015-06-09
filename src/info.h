#include "btdata.h"
#ifndef _INFO_H_
#define _INFO_H_

struct globalArgs_t{
    char* torrentpath;
    int  isseed;
    int  port;
    int  verbose;
};
extern struct globalArgs_t globalArgs;

struct globalInfo_t{
    char    ip[16];
    char    tracker_ip[16];
    char    id[20];
    int     port;
    int     tracker_port;
    int     done;
    int     uploaded;
    int     downloaded;
    int     left;
    char    *bitfield;
    torrentmetadata_t   *torrentmeta;
    tracker_data        *tracker_response;
};

extern struct globalInfo_t globalInfo;
extern int listenfd;
#endif
