#ifndef _AUDIO_H
#define _AUDIO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>
#include <netdb.h>

#include "g726codec.h"

#define BROADCAST           0
#define UNICAST             0
#define MULTICAST           1
#define SERVER_PORT         8888
#if BROADCAST
#define SERVER_IP           "192.168.0.255"
#else
#define GROUP_IP            "224.0.1.129"
#endif
//#define SERVER_IP           "192.168.0.66"

#define NETDEV_NAME         "tap0"

#define BUFFER_SIZE         32           //size of cyclic buffer
//#define SAMPLE_RATE         44100
#define SAMPLE_RATE         8000
#define PERIOD_FRAMES       400          //number of frames with a period
#define CHANNEL_NUM         1
#define PERIOD_BYTES        (2*PERIOD_FRAMES*CHANNEL_NUM)
#define TRANS_DATA_SIZE     (PERIOD_BYTES * 1)
#define AUDIO_DATA_SIZE     TRANS_DATA_SIZE

#define MIX_CHANNEL_COUNT   3
#define SIZE_AUDIO_FRAME    2

#define NOISE_CNT           50
#define CAL_TIME_CNT        5
#define MIX_CHANNEL_COUNT   3

#define ENCODE              1
/***********************************

 RATE:
 2: 1/8 encode, 16kbps(normal use)
 3: 3/16 encode, 24kbps
 4: 1/4 encode,  32kbps(normal use)
 5: 5/16 encode, 40kbps

**********************************/
#define RATE                2

#define RECORD_MODE         0           //if open, only record and sending
#define PLAYBACK_MODE       0           //if open, only receiving and playback
#define FILE_TEST           0           //capture data to file
#define LOCAL_TEST          0           //1 is local test, 0 is not.
#define CAPDATA_TEST        0           //capture data to file through signal action
#define TIME_TEST_RECV      0           //calculate time of receiving
#define TIME_TEST_PLAY      0           //calculate time of playback

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
#if ENCODE
    g726_state_t *g726handle;
    short *DecBuf;
#endif
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

typedef struct _mix_buf_t{
    char buf[PERIOD_BYTES];
    U32 size;
}mix_buf_t;
#define MIX_BUF_SIZE        sizeof(mix_buf_t)

int main_init(void);
int main_exit(void);

void *record_thread(void*);
int socket_create_cli(int*, struct sockaddr_in*);
int udp_send(int, struct sockaddr_in, snd_pcm_t*);

void *recv_thread(void*);
int socket_create_ser(int*, struct sockaddr_in*);
int udp_recv(int);

void *play_thread(void*);
int set_pcm_params(snd_pcm_t*);
int Mix(int, char*, U32*);
void _Mix(char sourseFile[8][SIZE_AUDIO_FRAME],int number,char *objectFile);  

void dmm(int);
#if CAPDATA_TEST
void cap_data(int);
#endif

#endif
