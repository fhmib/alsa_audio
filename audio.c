#include "audio.h"

U8 sa;      //self address

tshare_t tshare = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    1,
    1,
    1
};

node_pbuf_t node_pbuf[32];              //buffer which stores each node's info
mix_buf_t mix_buf[MIX_CHANNEL_COUNT];   //for mixer
pthread_mutex_t recv_mutex;

#if CAPDATA_TEST
//for capture data test
U8 cap_flag;                            //for capture socket data to file
int cap_fd;
#endif

int main(int argc, char *argv[])
{
    int rval = 0;
    int stop = 0;
    struct sigaction act;
#if CAPDATA_TEST
    struct sigaction cap_act;
#endif
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

    act.sa_flags = 0;
    act.sa_handler = dmm;
    sigaction(SIGALRM, &act, 0);
#if CAPDATA_TEST
    cap_act.sa_flags = 0;
    cap_act.sa_handler = cap_data;
    sigaction(SIGUSR1, &cap_act, 0);
#endif
    alarm(10);

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
    main_exit();
    exit(rval);
}

int main_init()
{
    int rval = 0;

#if CAPDATA_TEST
    cap_flag = 0;
#endif

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

int main_exit(){
    int rval = 0;
    int i;

    for(i = 0; i < 32; i++){
        if(node_pbuf[i].pdata != NULL){
            free(node_pbuf[i].pdata);
            node_pbuf[i].pdata = NULL;
        }
    }

    return rval;
}

//record and sendto socket
void *record_thread(void *arg)
{
    int rval = 0;
    int client_fd = -1;
    socklen_t len;
    struct sockaddr_in dest;
    snd_pcm_t *handle = NULL;

    //create socket
    rval = socket_create_cli(&client_fd, &dest);
    if(rval){
        EPT("create socket failed\n");
        rval = 1;
        goto thread_return;
    }

    //add recording code here
    rval = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if(rval){
        EPT("unable to open pcm device, rval = %d: %s\n", rval, snd_strerror(rval));
        rval = 2;
        goto thread_return;
    }
    rval = set_pcm_params(handle);
    if(rval){
        EPT("set pcm parameters failed!\n");
        rval = 2;
        goto thread_return;
    }

    //socket send
    rval = udp_send(client_fd, dest, handle);

thread_return:
    pthread_mutex_lock(&tshare.mutex);
    tshare.record_run = 0;
    pthread_cond_signal(&tshare.cond);
    pthread_mutex_unlock(&tshare.mutex);
    sleep(1);
    pthread_exit((void*)&rval);
}

//creat socket
int socket_create_cli(int *fd, struct sockaddr_in *dest)
{
    int rval = 0;
    int optval = 1;

    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        EPT("create socket failed! %s\n", strerror(errno));
        rval = 1;
        goto func_exit;
    }

    setsockopt(*fd, SOL_SOCKET, SO_BROADCAST | SO_REUSEADDR, &optval, sizeof(int));

    memset(dest, 0, sizeof(struct sockaddr_in));
    dest->sin_family = AF_INET;
    dest->sin_addr.s_addr = inet_addr(SERVER_IP);
    dest->sin_port = htons(SERVER_PORT);

func_exit:
    return rval;
}

//send audio data through socket
int udp_send(int client_fd, struct sockaddr_in dest, snd_pcm_t *handle)
{
    socklen_t len;
    int rval = 0;
    trans_data msg;

    int file_fd = 0;
    int ret;
    int fflag;

    fflag = 1;
    msg.node = sa;
    msg.seq = 0;

    len = sizeof(dest);

    while(1){

        ret = snd_pcm_readi(handle, msg.data, PERIOD_FRAMES);

        if(ret == -EPIPE){
            //EPIPE means overrun
            EPT("overrun!\n");
            snd_pcm_prepare(handle);
        }
        else if(ret < 0){
            EPT("error from read. ret = %d error:%s\n", ret, snd_strerror(ret));
        }
        else if(ret != (int)PERIOD_FRAMES){
            EPT("short read, read %d frames\n", ret);
        }

        if(fflag && (msg.seq < 50)){
            msg.seq++;
            continue;
        }

        fflag = 0;

        //if((((int*)msg.data)[0] == 0) && (((int*)(msg.data+((ret-1)*2*CHANNEL_NUM)))[0] == 0)) continue;
        rval = sendto(client_fd, &msg, (ret * 2 * CHANNEL_NUM) + HEAD_LENGTH, 0, (struct sockaddr*)&dest, len);
        //EPT("I've send msg to server, rval = %d\n", rval);
        msg.seq++;
        //usleep(1);
    }

func_exit:
    return rval;
}

//receive data from socket and process them
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
    int optval = 1;

    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        EPT("create socket failed! %s\n", strerror(errno));
        rval = 1;
        goto func_exit;
    }

    setsockopt(*fd, SOL_SOCKET, SO_BROADCAST | SO_REUSEADDR, &optval, sizeof(int));

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

//receive from socket and analyze data
int udp_recv(int client_fd)
{
    socklen_t len;
    struct sockaddr_in dest;
    int rval = 0, ret;
    char buf[MSG_LENGTH];

#if CAPDATA_TEST
    char capdata[MSG_LENGTH];
    int capret;
#endif

    trans_data *pmsg;
    cyc_data_t *cyc_data;

#if TIME_TEST_RECV
    int time_fd;
    char time_buf[256];
    long d_value;
    struct timeval start;
    struct timeval end;

    time_fd = open("time.log", O_RDWR | O_TRUNC | O_CREAT);
#endif

    U8 index;

    len = sizeof(dest);

    while(1){
#if TIME_TEST_RECV
        gettimeofday(&start, NULL);
#endif
        memset(buf, 0, MSG_LENGTH);
        ret = recvfrom(client_fd, buf, MSG_LENGTH, 0, (struct sockaddr*)&dest, &len);
        //EPT("receive msg, ret = %d\n", ret);
        if(ret == 0) continue;

        pmsg = (trans_data*)buf;

#if LOCAL_TEST
        if(sa == pmsg->node){
            //EPT("recive from the same node address, drop packet!\n");
            continue;
        }
#endif
	
        index = pmsg->node - 1;

        if((MYMAX(node_pbuf[index].seq, pmsg->seq) - MYMIN(node_pbuf[index].seq, pmsg->seq)) > 100){
            node_pbuf[index].seq = pmsg->seq;
        }
        else if(node_pbuf[index].seq > pmsg->seq){
            EPT("drop unsequence packet!\n");
            continue;
        }

        //drop the first 50 packets
        /*
        if(node_pbuf[index].seq < 50){
            continue;
        }*/

        pthread_mutex_lock(&recv_mutex);

        if(!node_pbuf[index].valid){
            //create buffer
            //EPT("malloc node_pbuf[%d].pdata\n", index);
            node_pbuf[index].pdata = (cyc_data_t*)malloc(CYC_DATA_SIZE);
            node_pbuf[index].valid = 1;
            node_pbuf[index].pdata->head = 0;
            node_pbuf[index].pdata->tail = 1;
        }

        cyc_data = node_pbuf[index].pdata;

        memcpy(cyc_data->buf[cyc_data->tail], pmsg->data, AUDIO_DATA_SIZE);
        cyc_data->size[cyc_data->tail] = ret - HEAD_LENGTH;
        if(cyc_data->tail == BUFFER_SIZE - 1) cyc_data->tail = 0;
        else cyc_data->tail++;

        node_pbuf[index].seq++;
        node_pbuf[index].loseq++;

#if CAPDATA_TEST
        //for capture data test
        if(cap_flag){
            sprintf(capdata, "\n***node-seq:%d-%d***\n\0", pmsg->node, pmsg->seq);
            capret = write(cap_fd, capdata, strlen(capdata));
            capret = write(cap_fd, pmsg->data, ret - HEAD_LENGTH);
            //EPT("%d\n", capret);
        }
#endif

        //for test
        //EPT("I recvice a packet from node[%u], seq = %d ,head = %u, tail = %u, size = %u\n", (U32)pmsg->node, pmsg->seq, node_pbuf[index].pdata->head, node_pbuf[index].pdata->tail, ret - HEAD_LENGTH);
        //EPT("ret = %d, MSG_LENGTH = %lu, HEAD_LENGTH = %lu\n", ret, MSG_LENGTH, HEAD_LENGTH);

        pthread_mutex_unlock(&recv_mutex);
#if TIME_TEST_RECV
        gettimeofday(&end, NULL);
        d_value = (end.tv_sec*1000000+end.tv_usec) - (start.tv_sec*1000000+start.tv_usec);
        sprintf(time_buf, "node = %d, d_value = %ld\n\0", pmsg->node, d_value);
        write(time_fd, time_buf, strlen(time_buf));
#endif
    }

    rval = 0;

func_exit:
    return rval;
}

//mixer and playback
void *play_thread(void *arg)
{
    int rval = 0, ret;
    int i, j;
    cyc_data_t *pdata;
    char play_buf[PERIOD_BYTES];            //for blayback
    char temp_buf[PERIOD_BYTES];            //for blayback
    U32 play_len = 0;                           //length of play_buf[]
    U32 temp_len = 0;
    snd_pcm_t *handle = NULL;

#if TIME_TEST_PLAY
    int time_fd;
    char time_buf[256];
    long d_value;
    struct timeval start;
    struct timeval end;

    time_fd = open("time.log", O_RDWR | O_TRUNC | O_CREAT);
#endif


    rval = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if(rval){
        EPT("open pcm device failed!\n");
        rval = 1;
        goto thread_return;
    }

    rval = set_pcm_params(handle);
    if(rval){
        EPT("set pcm parameters failed!\n");
        rval = 2;
        goto thread_return;
    }

    while(1){
        j = 0;
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

                if(j > MIX_CHANNEL_COUNT - 1){
                    pthread_mutex_unlock(&recv_mutex);
                    continue;
                }
                memcpy(mix_buf[j].buf, pdata->buf[pdata->head], pdata->size[pdata->head]);
                mix_buf[j].size = pdata->size[pdata->head];
                j++;

                //without mixer code
                /*ret = snd_pcm_writei(handle, pdata->buf[pdata->head], pdata->size[pdata->head]/(2*CHANNEL_NUM));
                if(ret == -EPIPE){
                    //EPIPE means underrun
                    EPT("underrun!\n");
                    snd_pcm_prepare(handle);
                }
                else if(ret < 0){
                    EPT("error from write, ret = %d: %s\n", ret, snd_strerror(ret));
                }
                else if(ret != (int)(pdata->size[pdata->head]/(2*CHANNEL_NUM))){
                    EPT("short write, write %d frames\n", ret);
                }*/
                /*
                ret = write(file_fd, pdata->buf[pdata->head], pdata->size[pdata->head]);
                */
            }

            pthread_mutex_unlock(&recv_mutex);
        }

        if(j == 0)
            continue;


#if TIME_TEST_PLAY
        gettimeofday(&start, NULL);
#endif
        rval = Mix(j, play_buf, &play_len);
#if TIME_TEST_PLAY
        gettimeofday(&end, NULL);
        d_value = (end.tv_sec*1000000+end.tv_usec) - (start.tv_sec*1000000+start.tv_usec);
        sprintf(time_buf, "j = %d, d_value = %ld\n\0", j, d_value);
        write(time_fd, time_buf, strlen(time_buf));
#endif

        if(temp_len){
            ret = snd_pcm_writei(handle, temp_buf, (int)(temp_len/(2*CHANNEL_NUM)));
            temp_len = 0;
        }

        ret = snd_pcm_writei(handle, play_buf, (int)(play_len/(2*CHANNEL_NUM)));
        //EPT("ret = %d, play_len = %d , j = %d\n", ret, play_len, j);
        if(ret == -EPIPE){
            //EPIPE means underrun
            EPT("underrun!\n");
            usleep(100000);
            snd_pcm_prepare(handle);
            temp_len = play_len;
            memcpy(temp_buf, play_buf, temp_len);
        }
        else if(ret < 0){
            EPT("error from write, ret = %d: %s\n", ret, snd_strerror(ret));
        }
        else if(ret != (int)(play_len/(2*CHANNEL_NUM))){
            EPT("short write, write %d frames\n", ret);
        }
#if TIME_TEST_PLAY
        //gettimeofday(&start, NULL);
#endif
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

int set_pcm_params(snd_pcm_t *handle)
{
    int rval = 0;
    snd_pcm_hw_params_t *params;
    snd_pcm_uframes_t frames;
    U32 rate;
    int dir;

    //Allocate a hardware parameters object
    snd_pcm_hw_params_alloca(&params);

    //fill it in with defalut values
    snd_pcm_hw_params_any(handle, params);

    //set the desired hardware parameters
    //interleaved mode
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    //signed 16-bit little-endian format
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

    //single channel
    snd_pcm_hw_params_set_channels(handle, params, 2);

    //44100 bits/second sampling rate(CD quality)
    rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, &dir);

    //set period size to 32 frames
    frames = PERIOD_FRAMES;
    snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

    //write the parameters to the driver
    rval = snd_pcm_hw_params(handle, params);
    if(rval){
        EPT("unalbe to set hw parameters. rval = %d error:%s\n", rval, snd_strerror(rval));
        rval = 1;
        goto func_exit;
    }

func_exit:
    return rval;
}

//mixer
int Mix(int number, char *play_buf, U32 *pplay_len)
{
    int rval = 0;
    U32 play_len;       //length of play_buf
    int i, j, k;
    char sourcefile[MIX_CHANNEL_COUNT][2];
    char *pos;
    short data_mix;     //stores 16-bits audio data which has been processed
    U32 ret[MIX_CHANNEL_COUNT];     //indicate if the original data has been all peocessed

    pos = play_buf;
    play_len = 0;
    for(i = 0; i < number; i++)
        ret[i] = mix_buf[i].size;

    while(1){
        j = 0;

        for(i = 0; i < number; i++){
            if(ret[i] < 2) continue;
            memcpy(sourcefile[j], mix_buf[i].buf+play_len, 2);
            k = j;
            j++;
        }
        if(j > 1){
            _Mix(sourcefile, j, (char*)&data_mix);
            if(data_mix > pow(2,16-1) || data_mix < -pow(2,16-1))
                EPT("mix error\n");
        }else if(j == 1){
            data_mix = *(short*)sourcefile[k];
        }else{
            break;
        }

        memcpy(pos, &data_mix, 2);
        pos+=2;
        play_len+=2;

        for(i = 0; i < number; i++){
            if(ret[i] > 1) ret[i]-=2;
        }

    }

    if(number == 1) usleep(230);
    else if(number == 2) usleep(80);

    *pplay_len = play_len;
    return rval;
}

//归一化混音
void _Mix(char sourseFile[MIX_CHANNEL_COUNT][SIZE_AUDIO_FRAME],int number,char *objectFile)
{
    int const MAX=32767;
    int const MIN=-32768;

    double f=1;
    int output;
    int i = 0,j = 0;
    for (i=0;i<SIZE_AUDIO_FRAME/2;i++)
    {
        int temp=0;
        for (j=0;j<number;j++)
        {
            temp+=*(short*)(sourseFile[j]+i*2);
        }
        output=(int)(temp*f);
        if (output>MAX)
        {
            f=(double)MAX/(double)(output);
            output=MAX;
        }
        if (output<MIN)
        {
            f=(double)MIN/(double)(output);
            output=MIN;
        }
        if (f<1)
        {
            f+=((double)1-f)/(double)32;
        }
        *(short*)(objectFile+i*2)=(short)output;
    }
}

//dynamic memory managment
void dmm(int signal){
    int i;

    pthread_mutex_lock(&recv_mutex);
    for(i = 0; i < 32; i++){
        if(node_pbuf[i].valid){
            if(!node_pbuf[i].loseq){
                //EPT("free node_pbuf[%d].pdata\n", i);
                free(node_pbuf[i].pdata);
                node_pbuf[i].pdata = NULL;
                node_pbuf[i].valid = 0;
            }else{
                node_pbuf[i].loseq = 0;
            }
        }
    }
    pthread_mutex_unlock(&recv_mutex);

    alarm(10);
}

#if CAPDATA_TEST
void cap_data(int signal){
    pthread_mutex_lock(&recv_mutex);
    cap_flag = 1;
    cap_fd = open("./capdata.log", O_RDWR | O_TRUNC | O_CREAT);
    if(cap_fd < 0){
        EPT("open capdata.log failed! %s\n", strerror(errno));
    }
    pthread_mutex_unlock(&recv_mutex);
}
#endif
