CC=gcc
SRC=shmem.c ../rpi_shmem.c
HDR=../rpi_shmem.h mva_test.h
OUT=test
HDR2=../mvaring.h mva_test.h ../rpi_shmem.h
SRC2=ring.c ../mvaring.c ../rpi_shmem.c
OUT2=ringtest
HDR3=../mvaring.h ../rpi_shmem.h ../adc_common.h
SRC3=adc-simulate.c ../mvaring.c ../rpi_shmem.c
OUT3=adc-simulate
CFLAGS=-Wall -ggdb
LDFLAGS=-lpthread -lm

all: $(OUT) $(OUT2) $(OUT3)

$(OUT): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

$(OUT2): $(SRC2) $(HDR2)
	$(CC) $(CFLAGS) -o $(OUT2) $(SRC2)

$(OUT3): $(SRC3) $(HDR3)
	$(CC) $(CFLAGS) -o $(OUT3) $(SRC3) $(LDFLAGS)

clean:
	rm -rf $(OUT) $(OUT2) $(OUT3)
