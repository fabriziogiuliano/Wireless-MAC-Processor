#ifndef METAMAC_H
#define METAMAC_H

#include "libb43.h"

typedef unsigned char uchar;
typedef unsigned int uint;

typedef enum {
	FLAG_VERBOSE = 1,
	FLAG_LOGGING = 2,
	FLAG_READONLY = 4
} metamac_flag_t;

struct metamac_slot {
	int slot_num;
	/* 1 represents that there was a packet waiting to be transmitted,
	0 represents that there was no packet waiting to be transmitted. */
	uchar packet_queued : 1;
	/* 1 represents that the running protocol transmitted a frame,
	0 represents that no frame was transmitted. */
	uchar transmitted : 1;
	/* 1 represents that the channel was used by another node, 0
	represents channel was not used by another node. */
	uchar channel_busy : 1;
};

typedef double (*protocol_emulator)(void *param, int slot_num, struct metamac_slot previous_slot);

struct protocol {
	/* Unique identifier. */
	int id;
	/* Readable name, such as "TDMA (slot 1)". */
	char *name;
	/* Path to the compiled (.txt) FSM implementation. */
	char *fsm_path;
	/* Protocol emulator for determining decisions of protocol locally. */
	protocol_emulator emulator;
	/* Parameter for protocol emulator. */
	void *parameter;
};

struct protocol_suite {
	/* Total number of protocols. */
	int num_protocols;
	/* Index of best protocol. Initially -1. */
	int active_protocol;
	/* Index of protocols in slots. -1 Indicated invalid */
	int slots[2];
	/* Which slot is active. 0 indicates neither are active. */
	int active_slot;
	/* Array of all protocols. */
	struct protocol *protocols;
	/* Array of weights corresponding to protocols. */
	double *weights;
	/* Factor used in computing weights. */
	double eta;
	/* Slot information for last to be emulated. */
	struct metamac_slot last_slot;
};

void free_protocol(struct protocol *proto);
void init_protocol_suite(struct protocol_suite *suite, int num_protocols, double eta);
void free_protocol_suite(struct protocol_suite *suite);
void update_weights(struct protocol_suite *suite, struct metamac_slot slot);
void metamac_init(struct debugfs_file * df, struct protocol_suite *suite, metamac_flag_t flags);
int metamac_loop(struct debugfs_file * df, struct protocol_suite *suite, metamac_flag_t flags);

extern int metamac_loop_break;

#endif // METAMAC_H