#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <err.h>
#include <stdint.h>

#include "metamac.h"
#include "protocols.h"
#include "vars.h"
#include "dataParser.h"


void free_protocol(struct protocol *proto)
{
	free(proto->name);
	free(proto->fsm_path);
	free(proto->parameter);
	free(proto);
}

/* Performs the computation for emulating the suite of protocols
for a single slot, and adjusting the weights. */
void update_weights(struct protocol_suite* suite, struct metamac_slot current_slot)
{
	/* If there is no packet queued for this slot, consider all protocols to be correct
	and thus the weights will not change. */
	if (current_slot.packet_queued) {
		/* z represents the correct decision for this slot - transmit if the channel
		is idle (1.0) or defer if it is busy (0.0) */
		double z = (!current_slot.channel_busy) ? 1.0 : 0.0;

		for (int p = 0; p < suite->num_protocols; ++p) {
			/* d is the decision of this component protocol - between 0 and 1 */
			double d = suite->protocols[p].emulator(suite->protocols[p].parameter, 
				current_slot.slot_num, suite->last_slot);
			suite->weights[p] *= exp(-(suite->eta) * fabs(d - z));
		}

		/* Normalize the weights */
		double s = 0;
		for (int p = 0; p < suite->num_protocols; ++p) {
			s += suite->weights[p];
		}

		for (int p = 0; p < suite->num_protocols; ++p) {
			suite->weights[p] /= s;
		}
	}

	suite->last_slot = current_slot;
}

void init_protocol_suite(struct protocol_suite *suite, int num_protocols, double eta)
{
	suite->num_protocols = num_protocols;
	suite->active_protocol = -1;
	suite->slots[0] = -1;
	suite->slots[1] = -1;
	suite->active_slot = -1;

	suite->protocols = (struct protocol*)calloc(num_protocols, sizeof(struct protocol));
	if (suite->protocols == NULL) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	suite->weights = (double*)malloc(sizeof(double) * num_protocols);
	if (suite->weights == NULL) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	for (int p = 0; p < num_protocols; ++p) {
		suite->weights[p] = 1.0 / num_protocols;
	}

	suite->eta = eta;
	suite->last_slot.slot_num = -1;
	suite->last_slot.packet_queued = 0;
	suite->last_slot.transmitted = 0;
	suite->last_slot.channel_busy = 0;
}

void free_protocol_suite(struct protocol_suite *suite)
{
	free(suite->protocols);
	free(suite->weights);
	free(suite);
}

void metamac_init(struct debugfs_file * df, struct protocol_suite *suite, metamac_flag_t flags)
{
	if (suite->num_protocols < 1) {
		return;
	}

	/* Best protocol could be already initialized based on predictions/heuristics */
	if (suite->active_protocol < 0) {
		/* Select the best protocol based on weights. At this point, they
		should be the same, so the first protocol will be selected. */
		int p = 0;
		double w = suite->weights[0];
		for (int i = 1; i < suite->num_protocols; ++i) {
			if (suite->weights[i] > w) {
				p = i;
				w = suite->weights[i];
			}
		}

		suite->active_protocol = p;
	}

	if (flags & FLAG_READONLY) {
		suite->slots[0] = -1;
		suite->slots[1] = -1;
	} else {
		struct options opt;
		opt.load = "1";
		opt.name_file = suite->protocols[suite->active_protocol].fsm_path;
		bytecodeSharedWrite(df, &opt);

		suite->slots[0] = suite->active_protocol;
		suite->slots[1] = -1;
		suite->active_slot = 0;
	}
}

static void metamac_display(unsigned long loop, struct protocol_suite *suite)
{
	if (loop > 0) {
		/* Reset cursor upwards by the number of protocols we will be printing. */
		printf("\x1b[%dF", suite->num_protocols);
	}

	int i;
	for (i = 0; i < suite->num_protocols; i++) {
		printf("%c %5.3f %s\n",
			suite->active_protocol == i ? '*' : ' ',
			suite->weights[i],
			suite->protocols[i].name);
	}
}

static void metamac_switch(struct debugfs_file *df, struct protocol_suite *suite)
{
	/* Identify the best and second-best protocols. */
	int best = 0, second = 0;
	for (int i = 0; i < suite->num_protocols; i++) {
		if (suite->weights[i] > suite->weights[best]) {
			second = best;
			best = i;
		}
	}

	if (best != suite->active_protocol) {
		/* Protocol switch necessitated. */
		struct options opt;

		if (best == suite->slots[0]) {
			opt.active = "1";
			writeAddressBytecode(df, &opt);
			suite->active_slot = 0;

		} else if (best == suite->slots[1]) {
			opt.active = "2";
			writeAddressBytecode(df, &opt);
			suite->active_slot = 1;

		} else if (second == suite->slots[0]) {
			/* If second best protocol is already in slot 1, then load
			best into slot 2. */
			opt.load = "2";
			opt.name_file = suite->protocols[best].fsm_path;
			bytecodeSharedWrite(df, &opt);
			suite->slots[1] = best;
			suite->active_slot = 1;

		} else {
			opt.load = "1";
			opt.name_file = suite->protocols[best].fsm_path;
			bytecodeSharedWrite(df, &opt);
			suite->slots[0] = best;
			suite->active_slot = 0;
		}

		suite->active_protocol = best;
	}
}

#define BAD_RECEPTION       0x00FA
#define BUSY_SLOT           0x00FC

volatile int metamac_loop_break = 0;

int metamac_read_loop(struct metamac_queue *queue, struct debugfs_file *df,
	metamac_flag_t flags, int slot_time, int read_interval)
{
	unsigned long slot_num = 0L, read_num = 0L;
	int slot_index, last_slot_index;
	uint64_t tsf, last_tsf, initial_tsf;

	struct timespec start_time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);

	getTSFRegs(df, &initial_tsf);
	tsf = initial_tsf;
	slot_index = shmRead16(df, B43_SHM_REGS, COUNT_SLOT) & 0x7;

	while (metamac_loop_break == 0) {

		struct timespec current_time;
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		uint64_t loop_start = (current_time.tv_sec - start_time.tv_sec) * 1000000L +
			(current_time.tv_nsec - start_time.tv_nsec) / 1000L;
		
		last_tsf = tsf;
		getTSFRegs(df, &tsf);
		last_slot_index = slot_index;
		slot_index = shmRead16(df, B43_SHM_REGS, COUNT_SLOT) & 0x7;

		uint packet_queued = shmRead16(df, B43_SHM_SHARED, PACKET_TO_TRANSMIT);
		uint transmitted = shmRead16(df, B43_SHM_SHARED, MY_TRANSMISSION);
		uint transmit_success = shmRead16(df, B43_SHM_SHARED, SUCCES_TRANSMISSION);
		uint transmit_other = shmRead16(df, B43_SHM_SHARED, OTHER_TRANSMISSION);
		uint bad_reception = shmRead16(df, B43_SHM_SHARED, BAD_RECEPTION);
		uint busy_slot = shmRead16(df, B43_SHM_SHARED, BUSY_SLOT);
		uint channel_busy = (transmitted & ~transmit_success) | transmit_other | bad_reception | busy_slot;

		int slots_passed = slot_index - last_slot_index;
		slots_passed = slots_passed < 0 ? slots_passed + 8 : slots_passed;
		int64_t actual = ((int64_t)tsf) - ((int64_t)last_tsf);

		if (actual < 0) {
			fprintf(stderr, "Received TSF difference of %lld between consecutive reads.\n", (long long)actual);
			metamac_loop_break = 1; // Stop the process loop.
			break;
		}

		int64_t min_diff = abs(actual - slots_passed * slot_time);
		int64_t diff;

		/* Suppose last_slot_index is 7 and slot_index is 5. Then, since the slot
		is a value mod 8 we know the actual number of slots which have passed is
		>= 6 and congruent to 6 mod 8. Using the TSF counter from the network card,
		we find the most likely number of slots which have passed. */
		while ((diff = abs(actual - (slots_passed + 8) * slot_time)) < min_diff) {
			slots_passed += 8;
			min_diff = diff;
		}

		/* Because the reads are not atomic, the values for the slot after the one
		indicated by slot_index are effectively unstable and could change between
		the reads for the different feedback variables. Thus, only the last 7 slots
		can be considered valid. If more than 7 slots have passed, we have to inject
		empty slots to maintain the synchronization. Note that the 7th most recent
		slot is at an offset of -6 relative to the current slot, hence the -1. */
		int slot_offset = slots_passed - 1;
		for (; slot_offset > 6; slot_offset--) {
			struct metamac_slot slot = {
				.slot_num = slot_num++,
				.read_num = read_num,
				.host_time = loop_start,
				.tsf_time = tsf - initial_tsf,
				.slot_index = slot_index,
				.slots_passed = slots_passed,
				.filler = 1,
				.packet_queued = 0,
				.transmitted = 0,
				.transmit_success = 0,
				.transmit_other = 0,
				.bad_reception = 0,
				.busy_slot = 0,
				.channel_busy = 0
			};

			queue_multipush(queue, &slot, 1);
		}

		struct metamac_slot slots[8];
		int ai = 0;

		for (; slot_offset >= 0; slot_offset--) {
			int si = slot_index - slot_offset;
			si = si < 0 ? si + 8 : si;

			slots[ai].slot_num = slot_num++;
			slots[ai].read_num = read_num;
			slots[ai].host_time = loop_start;
			slots[ai].tsf_time = tsf - initial_tsf;
			slots[ai].slot_index = slot_index;
			slots[ai].slots_passed = slots_passed;
			slots[ai].filler = 0;
			slots[ai].packet_queued = (packet_queued >> si) & 1;
			slots[ai].transmitted = (transmitted >> si) & 1;
			slots[ai].transmit_success = (transmit_success >> si) & 1;
			slots[ai].transmit_other = (transmit_other >> si) & 1;
			slots[ai].bad_reception = (bad_reception >> si) & 1;
			slots[ai].busy_slot = (busy_slot >> si) & 1;
			slots[ai].channel_busy = (channel_busy >> si) & 1;
			ai++;
		}

		queue_multipush(queue, slots, ai);

		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		uint64_t loop_end = (current_time.tv_sec - start_time.tv_sec) * 1000000L +
			(current_time.tv_nsec - start_time.tv_nsec) / 1000L;

		int64_t delay = ((int64_t)loop_start) + read_interval - ((int64_t)loop_end);
		if (delay > 0) {
			usleep(delay);
		}

		read_num++;
	}

	usleep(10000);
	queue_signal(queue);

	return metamac_loop_break;
}

int metamac_process_loop(struct metamac_queue *queue, struct debugfs_file *df,
	struct protocol_suite *suite, metamac_flag_t flags, const char *logpath)
{
	FILE * logfile;
	if (flags & FLAG_LOGGING) {
		logfile = fopen(logpath, "w+");
		if (logfile == NULL) {
			err(EXIT_FAILURE, "Unable to open log file");
		}

		fprintf(logfile, "slot_num,read_num,host_time,tsf_time,slot_index,slots_passed,filler,packet_queued,transmitted,transmit_success,transmit_other,bad_reception,busy_slot,channel_busy\n");
		printf("Logging to %s\n", logpath);
	} else {
		logfile = NULL;
	}

	unsigned long loop = 0;
	struct timespec last_update_time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &last_update_time);

	metamac_loop_break = 0;
	while (metamac_loop_break == 0) {

		struct metamac_slot slots[16];
		size_t count = queue_multipop(queue, slots, ARRAY_SIZE(slots));

		for (int i = 0; i < count; i++) {
			if (logfile != NULL) {
				fprintf(logfile, "%llu,%llu,%llu,%llu,%d,%d,%01x,%01x,%01x,%01x,%01x,%01x,%01x,%01x\n",
					(unsigned long long) slots[i].slot_num,
					(unsigned long long) slots[i].read_num,
					(unsigned long long) slots[i].host_time,
					(unsigned long long) slots[i].tsf_time,
					slots[i].slot_index,
					slots[i].slots_passed,
					slots[i].filler,
					slots[i].packet_queued,
					slots[i].transmitted,
					slots[i].transmit_success,
					slots[i].transmit_other,
					slots[i].bad_reception,
					slots[i].busy_slot,
					slots[i].channel_busy);
			}

			update_weights(suite, slots[i]);

		}

		struct timespec current_time;
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);

		unsigned long timediff = (current_time.tv_sec - last_update_time.tv_sec) * 1000000L
			+ (current_time.tv_nsec - last_update_time.tv_nsec) / 1000L;

		/* Update running protocol and the console every 1 second. */
		if (timediff > 1000000L) {
			if (!(flags & FLAG_READONLY)) {
				metamac_switch(df, suite);
			}

			if (flags & FLAG_VERBOSE) {
				metamac_display(loop++, suite);
			}

			last_update_time = current_time;
		}
	}

	if (logfile != NULL) {
		fclose(logfile);
	}

	return metamac_loop_break;
}
