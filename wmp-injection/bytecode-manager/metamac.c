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
void update_weights(struct protocol_suite* suite, struct meta_slot current_slot)
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
	suite->best_protocol = -1;
	suite->slot1_proto = -1;
	suite->slot2_proto = -1;
	suite->active_slot = 0;

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
	int i;
	for (i = 0; i < suite->num_protocols; i++) {
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
	if (suite->best_protocol < 0) {
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

		suite->best_protocol = p;
	}

	if (flags & FLAG_READONLY) {
		suite->slot1_proto = -1;
		suite->slot2_proto = -1;
	} else {
		struct options opt;
		opt.load = "1";
		opt.name_file = suite->protocols[suite->best_protocol].fsm_path;
		bytecodeSharedWrite(df, &opt);

		suite->slot1_proto = suite->best_protocol;
		suite->slot2_proto = -1;
		suite->active_slot = 1;
	}
}

int metamac_loop_break = 0;

int metamac_loop(struct debugfs_file *df, struct protocol_suite *suite, metamac_flag_t flags)
{
	/* Metamac read loop adapted from code developed by Domenico Garlisi.
	See bytecode-work.c:readSlotTimeValue. */

	int i,j,k;
	unsigned int slot_count;
	unsigned int prev_slot_count;
	unsigned int slot_count_var;
	unsigned int packet_queued;
	unsigned int transmitted;
	unsigned int transmit_success;
	unsigned int transmit_other;
	unsigned int channel_busy;
	
	long int usec;
	long int usec_from_start;
	long int usec_from_current;
	
	struct timeval starttime, finishtime;
	struct timeval start7slot, finish7slot;
	
	struct options opt;
	
	FILE * log_slot_time;
	if (flags & FLAG_LOGGING) {
		char fname_template[] = "/tmp/metamac-log-XXXXXX";
		char *fname = mktemp(fname_template);
		if (fname == NULL || *fname == '\0') {
			err(EXIT_FAILURE, "Unable to generate unique log name");
		}

		log_slot_time = fopen(fname, "w+");
		if (log_slot_time == NULL) {
			err(EXIT_FAILURE, "Unable to open log file");
		}

		fprintf(log_slot_time, "num-row,num-read,um-from-start-real,um-from-start-compute,um-diff-time,count-slot,count-slot-var,packet_queued,transmitted,transmit_success,transmit_other\n");
		printf("Logging to %s\n", fname);
	} else {
		log_slot_time = NULL;
	}

	gettimeofday(&starttime, NULL);
	usec_from_current = 0;
	start7slot= starttime;
	k=0;

	metamac_loop_break = 0;
	while (metamac_loop_break == 0) {
	  
		/* Changing the active bytecode takes between 20 and 80 ms. */
		/*writeAddressBytecode(df,&opt);*/
	
		prev_slot_count = 0x000F & shmRead16(df, B43_SHM_REGS, COUNT_SLOT);	//get current time slot number
		for(j = 0; j < 84; j++){
			/* Read slot results every 12ms, completing 84 cycles each second. */

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
			if ((prev_slot_count == (slot_count & 0x000F)) | usec > 16000 | j == 0)
			{
				// if last cycle is over 16ms or if we change bytecode, we fill time sloc with 0, no information for this slot time
				//printf("read error\n");
				if(usec > 100000)
				  exit(1);
				while(1){
					if (flags & FLAG_LOGGING) {
						fprintf(log_slot_time,"%d,%d,%ld,%ld,%ld,%d,0,0,0,0,0\n",
							k, j, usec_from_start, usec_from_current, usec, slot_count & 0x000F);
					}

					k++;
					usec_from_current += 2200;		
					if(usec_from_current > usec_from_start)
					    break;
				}
			}
			else
			{
				// we extract metaMAC parametes from registers and put it in the log file
				slot_count_var = prev_slot_count;
				for(i=0; i<7; i++)	// we get a maximum of 7 time slots, to safe, we not get the current 
				{
					if (flags && FLAG_LOGGING) {
						fprintf(log_slot_time,"%d,%d,%ld,%ld,%ld,%d,%d,%01x,%01x,%01x,%01x\n",
							k, j, usec_from_start, usec_from_current, usec, slot_count & 0x000F, slot_count_var,
							(packet_queued>>slot_count_var) & 0x0001, (transmitted>>slot_count_var) & 0x0001,
							(transmit_success>>slot_count_var) & 0x0001, (transmit_other>>slot_count_var) & 0x0001);
					}

					k++;
					usec_from_current += 2200;
					if(slot_count_var==7) {	// we increase module 7
					    slot_count_var=0;
					} else {
					    slot_count_var++;
					}
					
					if(slot_count_var == (slot_count & 0x000F)) { //we read to the last slot time
					    break;
					}
				}
			}
		}
	
	}

	if (flags & FLAG_LOGGING) {
		fclose(log_slot_time);
	}

	return metamac_loop_break;
}

/*void metamac_loop(struct debugfs_file * df, struct protocol_suite *suite)
{
	//metamac_init(df, suite);

	unsigned long slot_num = 0;

	while (1) {
		// Active protocol is only updated (if necessary) every 1 sec
		unsigned int slot_count = shmRead16(df, B43_SHM_REGS, COUNT_SLOT);
		unsigned int prev_slot_count;

		for (int i = 0; i < 84; ++i) { // This loop takes up oone second
			usleep(7000); // delay

			prev_slot_count = slot_count & 0x0F;
			slot_count = shmRead16(df, B43_SHM_REGS, COUNT_SLOT);
			unsigned int packet_queued = shmRead16(df, B43_SHM_SHARED, PACKET_TO_TRANSMIT);
			unsigned int transmitted  = shmRead16(df, B43_SHM_SHARED, MY_TRANSMISSION);
			unsigned int transmit_success = shmRead16(df, B43_SHM_SHARED, SUCCES_TRANSMISSION);
			unsigned int transmit_other =shmRead16(df, B43_SHM_SHARED, OTHER_TRANSMISSION);
			unsigned int channel_busy = (transmitted & ~transmit_success) | transmit_other;

			// Debugging.
			printf("slot_count=%d\n", slot_count);

			unsigned int slot = prev_slot_count;
			for (int j = 0; j < 7; ++j) {
				struct meta_slot slot_data;
				slot_data.slot_num = slot_num++;
				slot_data.packet_queued = (packet_queued >> slot) & 1;
				slot_data.transmitted = (transmitted >> slot) & 1;
				slot_data.channel_busy = (channel_busy >> slot) & 1;

				update_weights(suite, slot_data);

				slot = (slot + 1) % 7;
				if (slot == (slot_count & 0x0F)) {
					break;
				}
			}
		}

		// This is where the active protocol will be updated once that is implemented

		printf("Slot %ld: ", slot_num);
		for (int i = 0; i < suite->num_protocols; ++i) {
			printf("%s=%f ", suite->protocols[i].name, suite->weights[i]);
		}
		printf("\n");
	}
}*/