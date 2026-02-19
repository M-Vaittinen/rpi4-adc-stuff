CFLAGS=-Wall
SRC=rpi_adc_stream.c rpi_dma_utils.c rpi_shmem.c mvaring.c
HDR2=mvaring.h rpi_shmem.h common.h adc_common.h
SRC2=rpi_data_buff_extract.c rpi_shmem.c mvaring.c
OUT=rpi_adc_stream
OUT2=rpi_adc_bufextract
HDR=rpi_dma_utils.h mvaring.h rpi_shmem.h common.h adc_common.h
DISPOUT=test-ui
DISPSRC=rpi_opengl_graph.c
DISPLDFLAGS=-lm -lglut -lGLEW -lGL
CC=gcc

all: $(OUT) $(DISPOUT) $(OUT2)
$(OUT): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

$(OUT2): $(SRC2) $(HDR2)
	$(CC) $(CFLAGS) -o $(OUT2) $(SRC2)

$(DISPOUT): $(DISPSRC) $(HDR)
	$(CC) $(CFLAGS) -o $(DISPOUT) $(DISPSRC) $(DISPLDFLAGS)

clean:
	rm -rf $(DISPOUT) $(OUT) $(OUT2)
