CC=gcc
SRC=shmem.c ../rpi_shmem.c
HDR=../rpi_shmem.h
OUT=test
CFLAGS=-Wall -ggdb

all: $(OUT)

$(OUT): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -rf $(OUT)
