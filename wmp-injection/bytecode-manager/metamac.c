#include "metamac.h"

#include "protocols.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

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
			double d = suite->emulators[p].emulator(suite->emulators[p].parameter, 
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
	suite->emulators = (struct parametrized_emulator*)calloc(num_protocols, sizeof(struct parametrized_emulator));
	suite->weights = (double*)malloc(sizeof(double) * num_protocols);

	for (int p = 0; p < num_protocols; ++p) {
		suite->weights[p] = 1.0 / num_protocols;
	}

	suite->eta = eta;
	suite->last_slot.slot_num = -1;
	suite->last_slot.packet_queued = 0;
	suite->last_slot.transmitted = 0;
	suite->last_slot.channel_busy = 0;
}

int main(int argc, char *argv[])
{
	struct protocol_suite suite;
	init_protocol_suite(&suite, 2, 0.25);

	suite.emulators[0].emulator = tdma_emulate;
	struct tdma_param tdma_parameter;
	tdma_parameter.frame_offset = 0;
	tdma_parameter.frame_length = 4;
	tdma_parameter.slot_assignment = 1;
	suite.emulators[0].parameter = &tdma_parameter;

	suite.emulators[1].emulator = aloha_emulate;
	struct aloha_param aloha_parameter;
	aloha_parameter.persistance = 0.1;
	suite.emulators[1].parameter = &aloha_parameter;

	struct meta_slot slot;
	slot.slot_num = 0;
	slot.packet_queued = 1;
	slot.transmitted = 0;
	slot.channel_busy = 0;

	for (int s = 0; s < 40; ++s) {
		slot.slot_num = s;

		if (s < 20) {
			// Simulate conditions for aloha
			slot.packet_queued = 1;
			slot.transmitted = (s % 4) == 1;
			slot.channel_busy = 0; //(rand() % 4) < 3;
		} else {
			// Simulate conditions for TDMA
			slot.packet_queued = 1;
			slot.transmitted = (s % 4) == 1;
			slot.channel_busy = (s % 4) == 2 || (s % 4) == 3;
		}

		update_weights(&suite, slot);

		printf("Round %d: TDMA=%f, Aloha=%f\n", s, suite.weights[0], suite.weights[1]);
	}

	/*for (int s = 0; s < 40; ++s) {
		slot.slot_num = s;
		slot.packet_queued = 1;
		slot.transmitted = rand() % 2;
		slot.channel_busy = 0;

		update_weights(&suite, slot);

		printf("Round %d: TDMA=%f, Aloha=%f\n", s, suite.weights[0], suite.weights[1]);
	}*/
}