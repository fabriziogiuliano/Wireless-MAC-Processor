#ifndef METAMAC_H
#define METAMAC_H

struct meta_slot {
	int slot_num;
	/* 1 represents that there was a packet waiting to be transmitted,
	0 represents that there was no packet waiting to be transmitted. */
	char packet_queued : 1;
	/* 1 represents that the running protocol transmitted a frame,
	0 represents that no frame was transmitted. */
	char transmitted : 1;
	/* 1 represents that the channel was used by another node, 0
	represents channel was not used by another node. */
	char channel_busy : 1;
};

typedef double (*protocol_emulator)(void *param, int slot_num, struct meta_slot previous_slot);

struct parametrized_emulator {
	int id;
	char *name;
	protocol_emulator emulator;
	void *parameter;
};

struct protocol_suite {
	int num_protocols;
	struct parametrized_emulator *emulators;
	double *weights;
	double eta;
	struct meta_slot last_slot;
};

void init_protocol_suite(struct protocol_suite *suite, int num_protocols, double eta);
void update_weights(struct protocol_suite *suite, struct meta_slot slot);

#endif // METAMAC_H