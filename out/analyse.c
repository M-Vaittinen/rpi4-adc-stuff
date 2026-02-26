#include <stdio.h>

int main()
{
	FILE *rf;
	unsigned int ns, adc, gpio;
	unsigned int prevgpio = 0xabba, prevtime = 0xabba;
	int ret;


	rf = fopen("data_out", "r");

	while (3 == (ret = fscanf(rf, "%u\t%u\t0x%x\n", &ns, &adc, &gpio))) {
		if (gpio != prevgpio) {
			prevgpio = gpio;
			if (prevtime != 0xabba) {
				printf("Toggle %u\n", (ns - prevtime) / 1000000);
			}
			prevtime = ns;
		}

	}
	printf("fscanf returned %d\n", ret);

	return 0;
}
