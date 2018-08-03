#include "audio.h"

U8 sa;      //self address

tshare_t tshare = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    1,
    1,
    1
};

node_pbuf_t node_pbuf[32];
pthread_mutex_t recv_mutex;

int main(int argc, char *argv[])
{
    int rval = 0;
    int stop = 0;
    pthread_t recd_tid = -1;
    pthread_t play_tid = -1;
    pthread_t recv_tid = -1;

    if(argc != 2){
        EPT("run %s must provide the address of itself node\n", argv[0]);
        rval = 1;
        goto process_exit;
    }
    else{
        sa = (U8)atoi(argv[1]);
        if(sa > 31 || sa < 1){
            EPT("run %s must provide currect address\n", argv[0]);
            rval = 2;
            goto process_exit;
        }
    }

    rval = main_init();

    rval = pthread_create(&recv_tid, NULL, recv_thread, NULL);
    if(rval != 0){
        EPT("create receivce thread failed!\n");
        rval = 1;
        goto process_exit;
    }

    rval = pthread_create(&play_tid, NULL, play_thread, NULL);
    if(rval != 0){
        EPT("create play thread failed!\n");
        rval = 1;
        goto process_exit;
    }

    sleep(1);

    rval = pthread_create(&recd_tid, NULL, record_thread, NULL);
    if(rval != 0){
        EPT("create record thread failed!\n");
        rval = 1;
        goto process_exit;
    }

    pthread_mutex_lock(&tshare.mutex);
    while(0 == stop){
        pthread_cond_wait(&tshare.cond, &tshare.mutex);
        EPT("play_run = %d, recv_run = %d, record_run = %d\n", tshare.play_run, tshare.recv_run, tshare.record_run);
        stop = 1;
    }
    pthread_mutex_unlock(&tshare.mutex);

process_exit:
    exit(rval);
}

int main_init()
{
    int rval = 0;

    rval = pthread_mutex_init(&recv_mutex, NULL);
    if(rval){
        EPT("recv_mutex initialized failed! %s\n", strerror(errno));
        rval = 1;
        goto func_exit;
    }

    memset(node_pbuf, 0, NODE_PBUF_SIZE * 32);

func_exit:
    return rval;
}

//record and sendto socket
void *record_thread(void *arg)
{
    int rval = 0;
    int client_fd = -1;
    socklen_t len;
    struct sockaddr_in dest;

    //create socket
    rval = socket_create_cli(&client_fd, &dest);
    if(rval){
        rval = 1;
        goto thread_return;
    }

    //add recording code here

    //socket
    rval = udp_send(client_fd, dest);

thread_return:
    pthread_mutex_lock(&tshare.mutex);
    tshare.record_run = 0;
    pthread_cond_signal(&tshare.cond);
    pthread_mutex_unlock(&tshare.mutex);
    sleep(1);
    pthread_exit((void*)&rval);
}

int socket_create_cli(int *fd, struct sockaddr_in *dest)
{
    int rval = 0;

    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        EPT("create socket failed! %s\n", strerror(errno));
        rval = 1;
        goto func_exit;
    }

    memset(dest, 0, sizeof(struct sockaddr_in));
    dest->sin_family = AF_INET;
    dest->sin_addr.s_addr = inet_addr(SERVER_IP);
    dest->sin_port = htons(SERVER_PORT);

func_exit:
    return rval;
}

int udp_send(int client_fd, struct sockaddr_in dest)
{
    socklen_t len;
    int rval = 0;
    trans_data msg;

    int file_fd = 0;
    int ret;

    msg.node = sa;
    msg.seq = 0;

    file_fd = open("./output.raw", O_RDONLY);
    if(file_fd == -1){
        EPT("open file failed! %s\n", strerror(errno));
        rval = 1;
        goto func_exit;
    }

    len = sizeof(dest);

    while(1){
        ret = read(file_fd, msg.data, TRANS_DATA_SIZE);
        if(ret == 0){
            EPT("read end of file\n");
            close(file_fd);
            while(1) sleep(10);
        }
        else if(ret < 0){
            EPT("read error! %s\n", strerror(errno));
            close(file_fd);
            rval = 2;
            goto func_exit;
        }

        rval = sendto(client_fd, &msg, ret + HEAD_LENGTH, 0, (struct sockaddr*)&dest, len);
        msg.seq++;
        usleep(1);
    }

func_exit:
    return rval;
}

//receive from socket
void *recv_thread(void *arg)
{
    int rval = 0;
    int server_fd = -1;
    socklen_t len;
    struct sockaddr_in src;

    //create socket
    rval = socket_create_ser(&server_fd, &src);
    if(rval){
        rval = 1;
        goto thread_return;
    }

    udp_recv(server_fd);

    rval = 0;

thread_return:
    if(server_fd != -1)
        close(server_fd);
    pthread_mutex_lock(&tshare.mutex);
    tshare.recv_run = 0;
    pthread_cond_signal(&tshare.cond);
    pthread_mutex_unlock(&tshare.mutex);
    sleep(1);
    pthread_exit((void*)&rval);
}

//create socket
int socket_create_ser(int *fd, struct sockaddr_in *src)
{
    int rval = 0;

    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        EPT("create socket failed! %s\n", strerror(errno));
        rval = 1;
        goto func_exit;
    }

    memset(src, 0, sizeof(struct sockaddr_in));
    src->sin_family = AF_INET;
    src->sin_addr.s_addr = htonl(INADDR_ANY);
    src->sin_port = htons(SERVER_PORT);

    rval = bind(*fd, (struct sockaddr*)src, sizeof(struct sockaddr));
    if(-1 == rval){
        EPT("bind failed! %s\n", strerror(errno));
        rval = 2;
        goto func_exit;
    }

    rval = 0;

func_exit:
    return rval;
}

//receive from socket function
int udp_recv(int client_fd)
{
    socklen_t len;
    struct sockaddr_in dest;
    int rval = 0, ret;
    char buf[MSG_LENGTH];
    trans_data *pmsg;

    U8 index;

    len = sizeof(dest);

    while(1){
        memset(buf, 0, MSG_LENGTH);
        ret = recvfrom(client_fd, buf, MSG_LENGTH, 0, (struct sockaddr*)&dest, &len);
        if(ret == 0) continue;

        pmsg = (trans_data*)buf;

        /*
        if(sa == pmsg->node){
            EPT("recive from the same node address, drop packet!\n");
            continue;
        }
        */

        index = pmsg->node - 1;

        if((MYMAX(node_pbuf[index].loseq, pmsg->seq) - MYMIN(node_pbuf[index].loseq, pmsg->seq)) > 1000){
            node_pbuf[index].loseq = pmsg->seq;
        }
        else if(node_pbuf[index].loseq > pmsg->seq){
            EPT("drop unsequence packet!\n");
            continue;
        }

        pthread_mutex_lock(&recv_mutex);

        if(!node_pbuf[index].valid){
            //create buffer 
            node_pbuf[index].pdata = (cyc_data_t*)malloc(CYC_DATA_SIZE);
            node_pbuf[index].valid = 1;
            node_pbuf[index].pdata->head = 0;
            node_pbuf[index].pdata->tail = 1;
        }

        memcpy(node_pbuf[index].pdata->buf[node_pbuf[index].pdata->tail], pmsg->data, AUDIO_DATA_SIZE);
        node_pbuf[index].pdata->size[node_pbuf[index].pdata->tail] = ret - HEAD_LENGTH;
        if(node_pbuf[index].pdata->tail == BUFFER_SIZE - 1) node_pbuf[index].pdata->tail = 0;
        else node_pbuf[index].pdata->tail++;

        node_pbuf[index].loseq++;

        //for test
        //EPT("I recvice a packet from node[%u], seq = %d ,head = %u, tail = %u, size = %u\n", (U32)pmsg->node, pmsg->seq, node_pbuf[index].pdata->head, node_pbuf[index].pdata->tail, ret - HEAD_LENGTH);
        //EPT("ret = %d, MSG_LENGTH = %lu, HEAD_LENGTH = %lu\n", ret, MSG_LENGTH, HEAD_LENGTH);

        pthread_mutex_unlock(&recv_mutex);
    }

    rval = 0;

func_exit:
    return rval;
}

//playback
void *play_thread(void *arg)
{
    int rval = 0;
    int i;
    cyc_data_t *pdata;

    int file_fd;
    int ret;

    file_fd = open("./output2.raw", O_RDWR);
    if(file_fd == -1){
        EPT("open file failed! %s\n", strerror(errno));
        rval = 1;
        goto thread_return;
    }

    while(1){
        for(i = 0; i < 32; i++){

            pthread_mutex_lock(&recv_mutex);

            if(!node_pbuf[i].valid){
                pthread_mutex_unlock(&recv_mutex);
                continue;
            }

            pdata = node_pbuf[i].pdata;

            if((pdata->head == pdata->tail - 1) || ((pdata->head == BUFFER_SIZE - 1) && (pdata->tail == 0))){
                pthread_mutex_unlock(&recv_mutex);
                continue;
            }
            else{
                if(pdata->head == BUFFER_SIZE - 1) pdata->head = 0;
                else pdata->head++;
                ret = write(file_fd, pdata->buf[pdata->head], pdata->size[pdata->head]);
            }

            pthread_mutex_unlock(&recv_mutex);
        }
    }

    rval = 0;

thread_return:
    pthread_mutex_lock(&tshare.mutex);
    tshare.play_run = 0;
    pthread_cond_signal(&tshare.cond);
    pthread_mutex_unlock(&tshare.mutex);
    sleep(1);
    pthread_exit((void*)&rval);
}