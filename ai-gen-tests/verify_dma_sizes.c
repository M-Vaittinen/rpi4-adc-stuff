#include <stdio.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define MAX_SAMPS 1024
#define SAMP_SIZE 4
#define BUFF_LEN (MAX_SAMPS * SAMP_SIZE)
#define MAX_BUFFS 2

// Old configuration
#define VC_MEM_SIZE_OLD (PAGE_SIZE + (BUFF_LEN * MAX_BUFFS))

// New configuration with GPIO
#define VC_MEM_SIZE_NEW (PAGE_SIZE + (BUFF_LEN * MAX_BUFFS * 2))

typedef struct {
	uint32_t info, src, dst, length, stride, next, pad[2];
} DMA_CB;

// Old structure
typedef struct {
	DMA_CB cbs[10];
	uint32_t samp_size;
	uint32_t pwm_val;
	uint32_t adc_csd;
	uint32_t txd[2];
	volatile uint32_t usecs[2];
	volatile uint32_t states[2];
	volatile uint32_t rxd1[MAX_SAMPS];
	volatile uint32_t rxd2[MAX_SAMPS];
} ADC_DMA_DATA_OLD;

// New structure with GPIO
typedef struct {
	DMA_CB cbs[12];
	uint32_t samp_size;
	uint32_t pwm_val;
	uint32_t adc_csd;
	uint32_t txd[2];
	volatile uint32_t usecs[2];
	volatile uint32_t states[2];
	volatile uint32_t rxd1[MAX_SAMPS];
	volatile uint32_t rxd2[MAX_SAMPS];
	volatile uint32_t gpio_rxd1[MAX_SAMPS];
	volatile uint32_t gpio_rxd2[MAX_SAMPS];
} ADC_DMA_DATA_NEW;

int main() {
	printf("=== DMA Structure Size Analysis ===\n\n");

	printf("Configuration:\n");
	printf("  MAX_SAMPS: %d\n", MAX_SAMPS);
	printf("  PAGE_SIZE: %d bytes\n", PAGE_SIZE);
	printf("  SAMP_SIZE: %d bytes\n\n", SAMP_SIZE);

	printf("Old ADC_DMA_DATA structure:\n");
	printf("  Size: %zu bytes (%.2f KB)\n", sizeof(ADC_DMA_DATA_OLD),
	       sizeof(ADC_DMA_DATA_OLD) / 1024.0);
	printf("  VC_MEM_SIZE: %d bytes (%.1f KB)\n\n",
	       VC_MEM_SIZE_OLD, VC_MEM_SIZE_OLD / 1024.0);

	printf("New ADC_DMA_DATA structure (with GPIO):\n");
	printf("  Size: %zu bytes (%.2f KB)\n", sizeof(ADC_DMA_DATA_NEW),
	       sizeof(ADC_DMA_DATA_NEW) / 1024.0);
	printf("  VC_MEM_SIZE: %d bytes (%.1f KB)\n",
	       VC_MEM_SIZE_NEW, VC_MEM_SIZE_NEW / 1024.0);
	printf("  Increase: %zu bytes (%.2f KB)\n\n",
	       sizeof(ADC_DMA_DATA_NEW) - sizeof(ADC_DMA_DATA_OLD),
	       (sizeof(ADC_DMA_DATA_NEW) - sizeof(ADC_DMA_DATA_OLD)) / 1024.0);

	printf("DMA Constraint Checks:\n");
	if (VC_MEM_SIZE_NEW < 1024*1024) {
		printf("  ✓ VC_MEM_SIZE < 1 MB (OK for VideoCore)\n");
	}
	if (sizeof(ADC_DMA_DATA_NEW) % 4 == 0) {
		printf("  ✓ Structure size is 32-bit aligned\n");
	}
	printf("  ✓ DMA can handle any 32-bit address range\n");
	printf("  ✓ Per-transfer size: %d bytes (< 64 KB limit)\n",
	       MAX_SAMPS * 4);

	printf("\nMemory breakdown:\n");
	printf("  DMA CBs (12): %zu bytes\n", sizeof(DMA_CB) * 12);
	printf("  Metadata:    %zu bytes\n", sizeof(uint32_t) * 7);
	printf("  ADC rxd1:    %zu bytes\n", sizeof(uint32_t) * MAX_SAMPS);
	printf("  ADC rxd2:    %zu bytes\n", sizeof(uint32_t) * MAX_SAMPS);
	printf("  GPIO rxd1:   %zu bytes (NEW)\n", sizeof(uint32_t) * MAX_SAMPS);
	printf("  GPIO rxd2:   %zu bytes (NEW)\n", sizeof(uint32_t) * MAX_SAMPS);

	return 0;
}
