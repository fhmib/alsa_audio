CC = gcc
O_FLAG = -lpthread

audio: audio.o
	$(CC) -o audio audio.o $(O_FLAG)

audio.o: audio.c
	$(CC) -c audio.c

clean:
	rm -f *.o audio
