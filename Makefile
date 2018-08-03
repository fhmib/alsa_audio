#CC = gcc
CC = arm-xilinx-linux-gnueabi-gcc
O_FLAG = -L/home/rzxt/alsa/build/alsa_lib/lib -lasound -lpthread
C_FLAG = -I/home/rzxt/alsa/build/alsa_lib/include

audio: audio.o
	$(CC) -o audio audio.o $(O_FLAG)

audio.o: audio.c audio.h
	$(CC) -c audio.c $(C_FLAG)

clean:
	rm -f *.o audio
