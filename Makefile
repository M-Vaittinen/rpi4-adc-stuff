CFLAGS=-Wall
SRC=rpi_adc_stream.c rpi_dma_utils.c rpi_shmem.c
OUT=rpi_adc_stream
HDR=rpi_dma_utils.h
DISPOUT=test-ui
DISPSRC=rpi_opengl_graph.c
DISPLDFLAGS=-lm -lglut -lGLEW -lGL
CC=gcc

all: $(OUT) $(DISPOUT)
$(OUT): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

$(DISPOUT): $(DISPSRC) $(HDR)
	$(CC) $(CFLAGS) -o $(DISPOUT) $(DISPSRC) $(DISPLDFLAGS)

clean:
	rm -rf $(DISPOUT) $(OUT)
