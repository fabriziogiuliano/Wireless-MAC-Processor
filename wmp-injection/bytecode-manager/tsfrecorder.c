#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>

#include "libb43.h"

int break_loop = 0;

void sig_handler(int signum)
{
	fprintf(stderr, "SIGINT received, exiting...");
	break_loop = 1;
}

int main(int argc, char *argv[])
{
	struct debugfs_file df;
	init_file(&df);
	uint64_t last, current;
	long reads = 0;
	getTSFRegs(&df, &last);

	while (break_loop == 0) {
		getTSFRegs(&df, &current);
		reads++;

		if (abs(current - last) > 10000) {
			if (reads > 2) {
				printf("... %ld reads ...\n", reads - 2);
			}
			if (reads > 1) {
				printf("%lld\n", (long long)last);
			}
			printf("%lld\n", (long long)current);
			reads = 0;
		}

		last = current;
	}

	return 0;
}