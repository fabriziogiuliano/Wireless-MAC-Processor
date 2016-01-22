#ifndef METAMAC_H
#define METAMAC_H

#include "libb43.h"

#include "queue.h"

typedef unsigned char uchar;
typedef unsigned int uint;

typedef enum {
	FLAG_VERBOSE = 1,
	FLAG_LOGGING = 2,
	FLAG_READONLY = 4,
	FLAG_CYCLE = 8,
	FLAG_ETA_OVERRIDE = 16,
	FLAG_USE_BUSY = 32
} metamac_flag_t;

struct metamac_slot {
	unsigned long slot_num;
	unsigned long read_num;
	uint64_t host_time;
	uint64_t tsf_time;
	int slot_index;
	int slots_passed;

	/* Indicates if this slot was filled in because of a delay in
	reading from the board. */
	uchar filler : 1;
	/* Indicates that a packet was waiting to be transmitted in this slot. */
	uchar packet_queued : 1;
	/* Indicates that a transmission was attempted in this slot. */
	uchar transmitted : 1;
	/* Indicates that a transmission was successful in this slot. */
	uchar transmit_success : 1;
	/* Various measures for whether another node attempted to transmit. */
	uchar transmit_other : 1;
	uchar bad_reception : 1;
	uchar busy_slot : 1;
	/* Indicates that either a transmission attempt was unsuccessful
	in this slot or another node attempted a transmission. */
	uchar channel_busy : 1;
};

typedef double (*protocol_emulator)(void *param, int slot_num, int offset, struct metamac_slot previous_slot);

struct fsm_param {
	/* Parameter number. */
	int num;
	/* Parameter value. */
	int value;
	/* Linked list. */
	struct fsm_param *next;
};

struct protocol {
	/* Unique identifier. */
	int id;
	/* Readable name, such as "TDMA (slot 1)". */
	char *name;
	/* Path to the compiled (.txt) FSM implementation. */
	char *fsm_path;
	/* Parameters for the FSM. */
	struct fsm_param *fsm_params;
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
	/* Offset of slots numbering from read loop to slot numbering for TDMA. */
	int slot_offset;
	/* Array of all protocols. */
	struct protocol *protocols;
	/* Array of weights corresponding to protocols. */
	double *weights;
	/* Factor used in computing weights. */
	double eta;
	/* Slot information for last to be emulated. */
	struct metamac_slot last_slot;
	/* Time of last protocol update. */
	struct timespec last_update;
	/* Indicates whether protocols should be cycled. */
	uchar cycle : 1;
};

void free_protocol(struct protocol *proto);
void init_protocol_suite(struct protocol_suite *suite, int num_protocols, double eta,  metamac_flag_t metamac_flags);
void free_protocol_suite(struct protocol_suite *suite);
void update_weights(struct protocol_suite *suite, struct metamac_slot slot);
void metamac_init(struct debugfs_file * df, struct protocol_suite *suite, metamac_flag_t flags);

int metamac_read_loop(struct metamac_queue *queue, struct debugfs_file *df,
	metamac_flag_t flags, int slot_time, int read_interval);
int metamac_process_loop(struct metamac_queue *queue, struct debugfs_file *df,
	struct protocol_suite *suite, metamac_flag_t flags, const char *logpath);

extern volatile int metamac_loop_break;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#endif // METAMAC_H