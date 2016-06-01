#include "protocols.h"

/*void tdma_init(struct tdma_param *param, int frame_offset, int frame_length, int slot_assignment)
{
	param->frame_offset = frame_offset;
	param->frame_length = frame_length;
	param->slot_assignment = slot_assignment;
}*/

double tdma_emulate(void *param, int slot_num, int offset, struct metamac_slot previous_slot)
{
	slot_num += offset;
	struct tdma_param *tdma_params = (struct tdma_param*)param;
	return ((slot_num - tdma_params->frame_offset) % tdma_params->frame_length) ==
		tdma_params->slot_assignment ? 1.0 : 0.0;
}

double aloha_emulate(void *param, int slot_num, int offset, struct metamac_slot previous_slot)
{
	/* Determine if this is the first slot that this packet has been queued
	to transmit. */
	double pers= ((struct aloha_param*)param)->persistence; 

/*
TX - BUSY
1	1	BAD
1	0	GOOD
0	1	GOOD
0	0	BAD 	
*/
//	int tx_trial = (((double)rand()) < pers?1:0);
	double tx_trial_f = ((double)rand()/(double)RAND_MAX);
	int tx_trial = tx_trial_f <pers ? 1:0;
	double X = (double)(tx_trial);
	fprintf(stderr,"pers=%.2f,tx_trial_f=%.2f,tx_trial=%d,channel_busy=%d,X=%.2f\n",pers,tx_trial_f,tx_trial,previous_slot.channel_busy,X);


//	if (previous_slot.slot_num != slot_num || /* Previous slot was not recorded. Assume nothing queued. */
//		!previous_slot.packet_queued ||
//		(previous_slot.transmitted && previous_slot.transmit_success)) {
//		/* First slot this packet has been queued. Transmit. */
//		return 1.0;
//	} else {
//		/* Transmit with a probability of the persistance parameter. */
//		return ((struct aloha_param*)param)->persistence;
//	}
	return X;

}
