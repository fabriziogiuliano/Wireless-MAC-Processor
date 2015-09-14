#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <err.h>

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
	for (int i = 0; i < suite->num_protocols; i++) {
		free_protocol(&suite->protocols[i]);
	}

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

int metamac_loop_break = 0;

int metamac_read_loop(struct metamac_queue *queue, struct debugfs_file *df, metamac_flag_t flags)
{
	/* Metamac read loop adapted from code developed by Domenico Garlisi.
	See bytecode-work.c:readSlotTimeValue. */

	unsigned long int i,j,k;
	unsigned int slot_count;
	unsigned int prev_slot_count;
	unsigned int slot_count_var;
	unsigned int packet_queued;
	unsigned int transmitted;
	unsigned int transmit_success;
	unsigned int transmit_other;
	unsigned int channel_busy;
	unsigned long slot_num = 0;
	
	long int usec;
	long int usec_from_start;
	long int usec_from_current;
	
	struct timeval starttime, finishtime;
	struct timeval start7slot, finish7slot;

	struct metamac_slot slots[16];
	int slot_index = 0;

	gettimeofday(&starttime, NULL);
	usec_from_current = 0;
	start7slot= starttime;
	k=0;

	prev_slot_count = 0x000F & shmRead16(df, B43_SHM_REGS, COUNT_SLOT);	//get current time slot number
	metamac_loop_break = 0;

	for (j = 0; metamac_loop_break == 0; j++) {

		usleep(7000);

		// Read meta-MAC value, generally we need 5ms to do it
		prev_slot_count = slot_count & 0x000F;
		slot_count = shmRead16(df, B43_SHM_REGS, COUNT_SLOT);
		packet_queued = shmRead16(df, B43_SHM_SHARED, PACKET_TO_TRANSMIT);
		transmitted = shmRead16(df, B43_SHM_SHARED, MY_TRANSMISSION);
		transmit_success = shmRead16(df, B43_SHM_SHARED, SUCCES_TRANSMISSION);
		transmit_other = shmRead16(df, B43_SHM_SHARED, OTHER_TRANSMISSION);
		channel_busy = (transmitted & ~transmit_success) | transmit_other;
		
		// Compute cycle time
		gettimeofday(&finish7slot, NULL);	    
		usec = (finish7slot.tv_sec - start7slot.tv_sec) * 1000000;
		usec += (finish7slot.tv_usec - start7slot.tv_usec);
		usec_from_start = (finish7slot.tv_sec - starttime.tv_sec) * 1000000;
		usec_from_start += (finish7slot.tv_usec - starttime.tv_usec);
		start7slot = finish7slot;
		
		// print debug values
		//printf("%d - %ld\n", j, usec);
		//printf("slot_count:0x%04X - packet_queued:0x%04X - transmitted:0x%04X - transmit_success:0x%04X - transmit_other:0x%04X\n", slot_count, packet_queued, transmitted, transmit_success, transmit_other);
		//printf("%d - %d - %s,%d,%d,%ld\n", i, count_change, buffer, 251, 0,usec);    
		  
		// check if cycle time is over, we must be sure to read at least every 16ms
		if ((prev_slot_count == (slot_count & 0x000F)) || usec > 16000 || j == 0)
		{
			// if last cycle is over 16ms or if we change bytecode, we fill time sloc with 0, no information for this slot time
			//printf("read error\n");
			if(usec > 100000) {
				exit(1);
			}

			while (1) {
				slots[slot_index].slot_num = slot_num++;
				slots[slot_index].read_num = j;
				slots[slot_index].read_usecs = usec_from_start;
				slots[slot_index].slot_calc_usecs = usec_from_current;
				slots[slot_index].usecs_diff = usec;
				slots[slot_index].slot_count = slot_count & 0x000F;
				slots[slot_index].slot_count_var = 0;
				slots[slot_index].packet_queued = 0;
				slots[slot_index].transmitted = 0;
				slots[slot_index].channel_busy = 0;
				slot_index++;

				if (slot_index >= ARRAY_SIZE(slots)) {
					queue_multipush(queue, slots, slot_index);
					slot_index = 0;
				}

				k++;
				usec_from_current += 2200;		
				if (usec_from_current > usec_from_start) {
				    break;
				}
			}
		}
		else
		{
			// we extract metaMAC parametes from registers and put it in the log file
			slot_count_var = prev_slot_count;
			for(i=0; i<7; i++)	// we get a maximum of 7 time slots, to safe, we not get the current 
			{
				slots[slot_index].slot_num = slot_num++;
				slots[slot_index].read_num = j;
				slots[slot_index].read_usecs = usec_from_start;
				slots[slot_index].slot_calc_usecs = usec_from_current;
				slots[slot_index].usecs_diff = usec;
				slots[slot_index].slot_count = slot_count & 0x000F;
				slots[slot_index].slot_count_var = slot_count_var;
				slots[slot_index].packet_queued = (packet_queued >> slot_count_var) & 1;
				slots[slot_index].transmitted = (transmitted >> slot_count_var) & 1;
				slots[slot_index].channel_busy = (channel_busy >> slot_count_var) & 1;
				slot_index++;

				if (slot_index >= ARRAY_SIZE(slots)) {
					queue_multipush(queue, slots, slot_index);
					slot_index = 0;
				}

				k++;
				usec_from_current += 2200;
				slot_count_var = (slot_count_var + 1) % 8;

				if(slot_count_var == (slot_count & 0x000F)) { //we read to the last slot time
				    break;
				}
			}
		}
	}

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

		fprintf(logfile, "slot_num,read_num,read_usecs,slot_calc_usecs,usecs_diff,slot_count,slot_count_var,packet_queued,transmitted,channel_busy\n");
		printf("Logging to %s\n", logpath);
	} else {
		logfile = NULL;
	}

	unsigned long loop = 0;
	struct timeval last_update_time;
	gettimeofday(&last_update_time, NULL);

	metamac_loop_break = 0;
	while (metamac_loop_break == 0) {

		struct metamac_slot slots[16];
		size_t count = queue_multipop(queue, slots, ARRAY_SIZE(slots));

		for (int i = 0; i < count; i++) {
			if (logfile != NULL) {
				fprintf(logfile, "%ld,%ld,%ld,%ld,%ld,%d,%d,%01x,%01x,%01x\n",
					slots[i].slot_num,
					slots[i].read_num,
					slots[i].read_usecs,
					slots[i].slot_calc_usecs,
					slots[i].usecs_diff,
					slots[i].slot_count,
					slots[i].slot_count_var,
					slots[i].packet_queued,
					slots[i].transmitted,
					slots[i].channel_busy);
			}

			update_weights(suite, slots[i]);

		}

		struct timeval current_time;
		gettimeofday(&current_time, NULL);

		unsigned long timediff = (current_time.tv_sec - last_update_time.tv_sec) * 1000000
			+ (current_time.tv_usec - last_update_time.tv_usec);

		/* Update running protocol and the console every 1 second. */
		if (timediff > 1000000) {
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
