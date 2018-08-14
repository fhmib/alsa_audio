#CC = gcc
CC = arm-xilinx-linux-gnueabi-gcc
O_FLAG = -L/home/rzxt/alsa/build/alsa_lib/lib -lasound -lpthread
C_FLAG = -I/home/rzxt/alsa/build/alsa_lib/include

audio: audio.o g726codec.o
	$(CC) -o audio audio.o g726codec.o $(O_FLAG)

audio.o: audio.c audio.h
	$(CC) -c audio.c $(C_FLAG)

g726codec.o: g726codec.c g726codec.h
	$(CC) -c g726codec.c

clean:
	rm -f *.o audio
