#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "metamac.h"
#include "protocols.h"
#include "vars.h"
#include "dataParser.h"

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
	suite->slot1_protocol = -1;
	suite->slot2_protocol = -1;
	suite->active_slot = 0;

	suite->protocols = (struct protocol*)calloc(num_protocols, sizeof(struct protocol));
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

void metamac_init(struct debugfs_file * df, struct protocol_suite *suite)
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

	struct options opt;
	opt.load = "1";
	opt.name_file = suite->protocols[suite->best_protocol].fsm_path;
	bytecodeSharedWrite(df, &opt);

	suite->slot1_protocol = suite->best_protocol;
	suite->active_slot = 1;
}

void metamac_loop(struct debugfs_file * df, struct protocol_suite *suite)
{
	//metamac_init(df, suite);

	unsigned long slot_num = 0;

	while (1) {
		// Active protocol is only updated (if necessary) every 480ms,
		// which is 30 rounds of 16 ms each.
		unsigned int slot_count = 0x000F & shmRead16(df, B43_SHM_REGS, COUNT_SLOT);
		unsigned int prev_slot_count;

		for (int i = 0; i < 30; ++i) {
			usleep(7000);

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

				slot = (++slot) % 7;
				if (slot == (slot_count & 0x0F)) {
					break;
				}
			}
		}

		// This is where the active protocol will be updated once that is implemented

		printf("Slot %d: ", slot_num);
		for (int i = 0; i < suite->num_protocols; ++i) {
			printf("%s=%f ", suite->protocols[i].name, suite->weights[i]);
		}
		printf("\n");
	}
}

int main(int argc, char *argv[])
{
	struct protocol_suite suite;
	init_protocol_suite(&suite, 3, 0.25);

	suite.protocols[0].emulator = aloha_emulate;
	struct aloha_param aloha_parameter0;
	aloha_parameter0.persistance = 0.25;
	suite.protocols[0].parameter = &aloha_parameter0;
	suite.protocols[0].name = "Aloha (.25)";

	suite.protocols[1].emulator = aloha_emulate;
	struct aloha_param aloha_parameter1;
	aloha_parameter1.persistance = 0.50;
	suite.protocols[1].parameter = &aloha_parameter1;
	suite.protocols[1].name = "Aloha (.50)";

	suite.protocols[2].emulator = aloha_emulate;
	struct aloha_param aloha_parameter2;
	aloha_parameter2.persistance = 0.75;
	suite.protocols[2].parameter = &aloha_parameter2;
	suite.protocols[2].name = "Aloha (.75)";

	struct debugfs_file df;
	init_file(&df);

	metamac_loop(&df, &suite);
}