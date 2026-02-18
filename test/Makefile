CC=gcc
SRC=shmem.c ../rpi_shmem.c
HDR=../rpi_shmem.h mva_test.h
OUT=test
HDR2=../mvaring.h mva_test.h ../rpi_shmem.h
SRC2=ring.c ../mvaring_ring.c ../rpi_shmem.c
OUT2=ringtest
CFLAGS=-Wall -ggdb

all: $(OUT) $(OUT2)

$(OUT): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

$(OUT2): $(SRC2) $(HDR2)
	$(CC) $(CFLAGS) -o $(OUT2) $(SRC2)

clean:
	rm -rf $(OUT) $(OUT2)
