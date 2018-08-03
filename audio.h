#ifndef _AUDIO_H
#define _AUDIO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <alsa/asound.h>

#define SERVER_PORT         8888
#define SERVER_IP           "127.0.0.1"

#define BUFFER_SIZE         128          //size of cyclic buffer
#define PERIOD_FRAMES       32          //number of frames with a period
#define CHANNEL_NUM         2
#define PERIOD_BYTES        (2*PERIOD_FRAMES*CHANNEL_NUM)
#define TRANS_DATA_SIZE     (PERIOD_BYTES * 10)
#define AUDIO_DATA_SIZE     TRANS_DATA_SIZE

#define EPT(format, ...)    do{\
                                char _pstr[512];\
                                sprintf(_pstr, "%%s,%%d:%s", format);\
                                fprintf(stderr, _pstr, __func__, __LINE__, ##__VA_ARGS__);\
                            }while(0);
#define MYMAX(a,b)            ((a > b) ? a : b)
#define MYMIN(a,b)            ((a > b) ? b : a)

typedef unsigned char   U8;
typedef unsigned short  U16;
typedef unsigned int    U32;

typedef struct _cyc_data_t{
    char buf[BUFFER_SIZE][AUDIO_DATA_SIZE];
    U32 size[BUFFER_SIZE];
    U32 head;
    U32 tail;
}cyc_data_t;
#define CYC_DATA_SIZE       sizeof(cyc_data_t)

typedef struct _node_pbuf_t{
    U8 valid;
    U32 seq;
    U32 loseq;
    cyc_data_t *pdata;
}node_pbuf_t;
#define NODE_PBUF_SIZE      sizeof(node_pbuf_t)

typedef struct _tshare_t{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int play_run;
    int recv_run;
    int record_run;
}tshare_t;

typedef struct _trans_data{
    U8 node;
    U32 seq;
    char data[TRANS_DATA_SIZE];
}trans_data;
#define MSG_LENGTH          sizeof(trans_data)
#define HEAD_LENGTH         (MSG_LENGTH - TRANS_DATA_SIZE)

int main_init(void);

void *record_thread(void*);
int socket_create_cli(int*, struct sockaddr_in*);
int udp_send(int, struct sockaddr_in);

void *recv_thread(void*);
int socket_create_ser(int*, struct sockaddr_in*);
int udp_recv(int);

void *play_thread(void*);

#endif