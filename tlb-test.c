#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	volatile char *buffer, *pbuf;
	int size = 40960, step = 4096, delay = 0, repeat = 1, remainder = 1;
	int i, quiet = 0;

	for(i = 1; i < argc; i ++){
		switch(argv[i][0]) {
		case 'T':
			sscanf(&argv[i][1], "%d", &size);
			break;
		case 'P':
			sscanf(&argv[i][1], "%d", &step);
			break;
		case 'D':
			sscanf(&argv[i][1], "%d", &delay);
			break;
		case 'R':
			sscanf(&argv[i][1], "%d", &repeat);
			break;
		case 'M':
			sscanf(&argv[i][1], "%d", &remainder);
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			printf("invalid parameters\n");
			return -1;
		}
	}
	if (!quiet)
		printf("test with total %d step %d delay %d repeat %d, remainder %d\n", size, step, delay, repeat, remainder);
	if (step > size) {
		printf("error: step %d > size %d\n", step, size);
		return -1;
	}

	buffer = malloc(size);
	if (buffer == NULL) {
		printf("error: can't alloc %d bytes\n", size);
		return -1;
	}

	do {
		if (!quiet) printf("round %d\n", repeat);
		for(pbuf = buffer, i = 0; pbuf < buffer + size; pbuf += step) {
			if (delay && i % remainder == 0) {
				if (!quiet)
					printf("%d addr %p\n", i, pbuf);
				sleep(delay);
			}
			pbuf[0] = (char)i++;
		}
	} while (--repeat > 0);

	if (!quiet) {
		printf("%d finish, press any key to continue...", i);
		getchar();
	}

	free((void*)buffer);

	return 0;
}
