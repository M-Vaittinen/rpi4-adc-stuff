#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define MAX_SAMPS 1024

typedef struct {
	uint32_t info, src, dst, length, stride, next, pad[2];
} DMA_CB;

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
	printf("=== Structure Layout Analysis ===\n\n");
	
	printf("DMA_CB structure:\n");
	printf("  sizeof(DMA_CB): %zu bytes\n", sizeof(DMA_CB));
	printf("  Expected: 8 fields * 4 bytes = 32 bytes\n\n");
	
	printf("ADC_DMA_DATA_NEW structure:\n");
	printf("  Total size: %zu bytes\n\n", sizeof(ADC_DMA_DATA_NEW));
	
	printf("Field offsets and sizes:\n");
	printf("  cbs[12]:      offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, cbs), sizeof(((ADC_DMA_DATA_NEW*)0)->cbs));
	printf("  samp_size:    offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, samp_size), sizeof(uint32_t));
	printf("  pwm_val:      offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, pwm_val), sizeof(uint32_t));
	printf("  adc_csd:      offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, adc_csd), sizeof(uint32_t));
	printf("  txd[2]:       offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, txd), sizeof(((ADC_DMA_DATA_NEW*)0)->txd));
	printf("  usecs[2]:     offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, usecs), sizeof(((ADC_DMA_DATA_NEW*)0)->usecs));
	printf("  states[2]:    offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, states), sizeof(((ADC_DMA_DATA_NEW*)0)->states));
	printf("  rxd1[1024]:   offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, rxd1), sizeof(((ADC_DMA_DATA_NEW*)0)->rxd1));
	printf("  rxd2[1024]:   offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, rxd2), sizeof(((ADC_DMA_DATA_NEW*)0)->rxd2));
	printf("  gpio_rxd1[]:  offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, gpio_rxd1), sizeof(((ADC_DMA_DATA_NEW*)0)->gpio_rxd1));
	printf("  gpio_rxd2[]:  offset %3zu, size %4zu bytes\n", 
	       offsetof(ADC_DMA_DATA_NEW, gpio_rxd2), sizeof(((ADC_DMA_DATA_NEW*)0)->gpio_rxd2));
	
	printf("\nCalculation check:\n");
	size_t expected = sizeof(DMA_CB) * 12 + 4 + 4 + 4 + 8 + 8 + 8 + 
	                  4096 + 4096 + 4096 + 4096;
	printf("  Manual sum: %zu bytes\n", expected);
	printf("  Actual:     %zu bytes\n", sizeof(ADC_DMA_DATA_NEW));
	if (expected != sizeof(ADC_DMA_DATA_NEW)) {
		printf("  Padding:    %zu bytes (compiler added)\n", 
		       sizeof(ADC_DMA_DATA_NEW) - expected);
	}
	
	printf("\n=== Alignment check ===\n");
	printf("  Structure alignment: %zu bytes\n", _Alignof(ADC_DMA_DATA_NEW));
	printf("  DMA_CB alignment:    %zu bytes\n", _Alignof(DMA_CB));
	
	return 0;
}
