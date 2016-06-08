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
	return ((struct aloha_param*)param)->persistence;
}
