#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include "metamac.h"

struct tdma_param {
	int frame_offset;
	int frame_length;
	int slot_assignment;
};

//void tdma_init(struct tdma_param *param, int frame_offset, int frame_length, int slot_assignment);
double tdma_emulate(void *param, int slot_num, int offset, struct metamac_slot previous_slot);

struct aloha_param {
	double persistence;
};

//void aloha_init(struct aloha_param *param, double persistance);
double aloha_emulate(void *param, int slot_num, int offset, struct metamac_slot previous_slot);

#endif // PROTOCOLS_H