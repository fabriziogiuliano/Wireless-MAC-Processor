#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <err.h>
#include <stdint.h>
#include <string.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define BUFFSIZE 255
void Die(char *mess) { perror(mess); exit(1); }

#include "metamac.h"
#include "protocols.h"
#include "vars.h"
#include "dataParser.h"
#include "bytecode-work.h"


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
	/* Accounting for the fact that the slots that TDMA variants transmit on are
	not necessarily aligned to the slot indices provided by the board. For instance,
	one would expect that TDMA-4 slot 1 would transmit on slot indexes 1 and 5, but 
	this is not necessarily true. Offset between transmissions will be 4, but not
	necessarily aligned to the slot indexes. */
	if (suite->protocols[suite->active_protocol].emulator == tdma_emulate &&
		current_slot.transmitted) {
		/* Update slot_offset. */

		struct tdma_param *params = suite->protocols[suite->active_protocol].parameter;
		int neg_offset = (current_slot.slot_num - params->frame_offset - params->slot_assignment) %
			params->frame_length;
		suite->slot_offset = (params->frame_length - neg_offset) % params->frame_length;
	}
	/* If there is no packet queued for this slot, consider all protocols to be correct
	and thus the weights will not change. */
	
	double z = 0;
	double uu=0;
	double d=0;
	double p_curr = ((struct aloha_param*)(suite->protocols[suite->active_protocol].parameter))->persistence;
	if (current_slot.packet_queued) {
		/* z represents the correct decision for this slot - transmit if the channel
		is idle (1.0) or defer if it is busy (0.0) */
		//z = (!current_slot.channel_busy) ? 1 : 0;

		if (suite->protocols[suite->active_protocol].emulator == tdma_emulate) {

			z = (!current_slot.channel_busy) ? 1 : 0;
		}
		
		if (suite->protocols[suite->active_protocol].emulator == aloha_emulate) {

			
			// transmission AND success: GOOD
			// no trasmission AND slot busy - GOOD
			// trasmission AND NOT success - WRONG 
			// trasmission  AND slot empty - WRONG
			//if GOOD
			//	z = p_current
			//else
			//	z = 1 - p_current


			if (	(current_slot.transmitted & current_slot.transmit_success) |	
				(~current_slot.transmitted & (current_slot.transmit_other | current_slot.bad_reception)) ) {	
					z = p_curr;
					uu=1;
				}
			
			else 
				if (	(current_slot.transmitted & ~current_slot.transmit_success) | 
					(~current_slot.transmitted & ~(current_slot.transmit_other | current_slot.bad_reception)) ){
					z = 1.0 - p_curr;
					uu=0;
				}

			z = (!current_slot.channel_busy) ? p_curr : 1-p_curr;
			
		}

		for (int p = 0; p < suite->num_protocols; ++p) {
			/* d is the decision of this component protocol - between 0 and 1 */
			d = suite->protocols[p].emulator(suite->protocols[p].parameter, 
				current_slot.slot_num, suite->slot_offset, suite->last_slot);
			
			//fprintf(stderr,"[%d] uu=%e,d=%e,z=%e,p_curr=%e\n",p,uu,d,z,p_curr);
			suite->weights[p] *= exp(-(suite->eta) * fabs(d - z));
			suite->weights[p]=suite->weights[p]<0.01?0.01:suite->weights[p];
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

void init_protocol_suite(struct protocol_suite *suite, int num_protocols, double eta, metamac_flag_t metamac_flags)
{
	suite->num_protocols = num_protocols;
	suite->active_protocol = -1;
	suite->slots[0] = -1;
	suite->slots[1] = -1;
	suite->active_slot = -1;
	suite->slot_offset = 0;

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
	suite->cycle = (metamac_flags & FLAG_CYCLE) != 0;
}

void free_protocol_suite(struct protocol_suite *suite)
{
	free(suite->protocols);
	free(suite->weights);
	free(suite);
}

void configure_params(struct debugfs_file *df, int slot, struct fsm_param *param)
{
	while (param != NULL) {
		//printf("Set parameters num : %d - value %d - with slot %d\n", param->num, param->value, slot);
		set_parameter(df, slot, param->num, param->value);
		param = param->next;
	}
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

		putInWaitMode(df);
		bytecodeSharedWrite(df, &opt);
		returnFromWaitMode(df);	

		configure_params(df, 0, suite->protocols[suite->active_protocol].fsm_params);
		opt.active = opt.load;

		writeAddressBytecode(df, &opt);

		suite->slots[0] = suite->active_protocol;
		suite->slots[1] = -1;
		suite->active_slot = 0;
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &suite->last_update);
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

static void load_protocol(struct debugfs_file *df, struct protocol_suite *suite, int protocol)
{
	struct options opt;
	int active = suite->active_slot; // Always 0 or 1 since metamac_init will already have run.
	int inactive = 1 - active;
	
	//printf(" before load -- active_protocol : %d - next protocol : %d\n", suite->active_protocol, protocol);

	if (protocol == suite->slots[active]) {
		/* This protocol is already running. */

	} else if (protocol == suite->slots[inactive]) {
		/* Switch to other slot. */
		opt.active = (inactive == 0) ? "1" : "2";
		writeAddressBytecode(df, &opt);
		suite->active_slot = inactive;

	} else if (suite->slots[active] >= 0 &&
			strcmp(suite->protocols[protocol].fsm_path,
			suite->protocols[suite->slots[active]].fsm_path) == 0) {
	  
		/* Protocol in active slot shares same FSM, but is not the same protocol
		(already checked). Write the parameters for this protocol. */
		configure_params(df, active, suite->protocols[protocol].fsm_params);
		suite->slots[active] = protocol;

	} else if (suite->slots[inactive] >= 0 &&
			strcmp(suite->protocols[protocol].fsm_path,
			suite->protocols[suite->slots[inactive]].fsm_path) == 0) {
		/* Protocol in inactive slot shares same FSM, but is not the same protocol,
		so write the parameters for this protocol and activate it. */
		configure_params(df, inactive, suite->protocols[protocol].fsm_params);
		opt.active = (inactive == 0) ? "1" : "2";
		writeAddressBytecode(df, &opt);
		suite->slots[inactive] = protocol;
		suite->active_slot = inactive;

	} else {
		/* Load into inactive slot. */
		opt.load = (inactive == 0) ? "1" : "2";
		opt.name_file = suite->protocols[protocol].fsm_path;
		bytecodeSharedWrite(df, &opt);
		configure_params(df, inactive, suite->protocols[protocol].fsm_params);
		opt.active = opt.load;
		writeAddressBytecode(df, &opt);

		suite->slots[inactive] = protocol;
		suite->active_slot = inactive;

	}

	suite->active_protocol = protocol;
	clock_gettime(CLOCK_MONOTONIC_RAW, &suite->last_update);
	
	//printf(" after load -- active_protocol : %d - next protocol : %d\n", suite->active_protocol, protocol);

}

static void metamac_evaluate(struct debugfs_file *df, struct protocol_suite *suite)
{
	/* Identify the best protocol. */
	int best = 0;
	for (int i = 0; i < suite->num_protocols; i++) {
		if (suite->weights[i] > suite->weights[best]) {
			best = i;
		}
	}

	if (suite->cycle) {
		struct timespec current_time;
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		uint64_t timediff = (current_time.tv_sec - suite->last_update.tv_sec) * 1000000L +
			(current_time.tv_nsec - suite->last_update.tv_nsec) / 1000L;

		if (timediff > 1000000L) {
			load_protocol(df, suite, (suite->active_protocol + 1) % suite->num_protocols);
		}
	} else {
		load_protocol(df, suite, best);
	}
}

#define BAD_RECEPTION       0x00FA
#define BUSY_SLOT           0x00FC

volatile int metamac_loop_break = 0;

int metamac_read_loop(struct metamac_queue *queue, struct debugfs_file *df,
	metamac_flag_t flags, int slot_time, int read_interval)
{
	unsigned long slot_num = 0L, read_num = 0L;
	int slot_index, last_slot_index;
	uint64_t tsf, last_tsf, initial_tsf;

	struct timespec start_time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
	uint64_t loop_end = 0L;

	getTSFRegs(df, &initial_tsf);
	tsf = initial_tsf;
	slot_index = shmRead16(df, B43_SHM_REGS, COUNT_SLOT) & 0x7;
	slot_num = (slot_index + 1) % 8;

	while (metamac_loop_break == 0) {

		struct timespec current_time;
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		uint64_t loop_start = (current_time.tv_sec - start_time.tv_sec) * 1000000L +
			(current_time.tv_nsec - start_time.tv_nsec) / 1000L;
		
		last_tsf = tsf;
		getTSFRegs(df, &tsf);
		last_slot_index = slot_index;
		slot_index = shmRead16(df, B43_SHM_REGS, COUNT_SLOT) & 0x7;

		uint packet_queued = shmRead16(df, B43_SHM_SHARED, PACKET_TO_TRANSMIT);
		uint transmitted = shmRead16(df, B43_SHM_SHARED, MY_TRANSMISSION);
		uint transmit_success = shmRead16(df, B43_SHM_SHARED, SUCCES_TRANSMISSION);
		uint transmit_other = shmRead16(df, B43_SHM_SHARED, OTHER_TRANSMISSION);
		uint bad_reception = shmRead16(df, B43_SHM_SHARED, BAD_RECEPTION);
		uint busy_slot = shmRead16(df, B43_SHM_SHARED, BUSY_SLOT);
		int end_slot_index = shmRead16(df, B43_SHM_REGS, COUNT_SLOT) & 0x7;

		uint channel_busy;
		if (flags & FLAG_USE_BUSY) {
			channel_busy = (transmitted & ~transmit_success) |
				((transmit_other | bad_reception | busy_slot) & ~(transmitted & transmit_success));
		} else {
			
			channel_busy = (transmitted & ~transmit_success) |
				((transmit_other | bad_reception) & ~(transmitted & transmit_success));
		}

		int slots_passed = slot_index - last_slot_index;
		slots_passed = slots_passed < 0 ? slots_passed + 8 : slots_passed;
		int64_t actual = ((int64_t)tsf) - ((int64_t)last_tsf);

		if (actual < 0 || actual > 200000) {
			fprintf(stderr, "Received TSF difference of %lld between consecutive reads.\n", (long long)actual);
			/* Unresolved bug with hardware/firmware/kernel driver causes occasional large jumps
			in the TSF counter value. In this situation use time from the OS timer instead. */
			actual = ((int64_t)loop_start) - ((int64_t)loop_end);
		}

		int64_t min_diff = abs(actual - slots_passed * slot_time);
		int64_t diff;

		/* Suppose last_slot_index is 7 and slot_index is 5. Then, since the slot
		is a value mod 8 we know the actual number of slots which have passed is
		>= 6 and congruent to 6 mod 8. Using the TSF counter from the network card,
		we find the most likely number of slots which have passed. */
		while ((diff = abs(actual - (slots_passed + 8) * slot_time)) < min_diff) {
			slots_passed += 8;
			min_diff = diff;
		}

		/* Because the reads are not atomic, the values for the slot
		indicated by slot_index are effectively unstable and could change between
		the reads for the different feedback variables. Thus, only the last 7 slots
		can be considered valid. If more than 7 slots have passed, we have to inject
		empty slots to maintain the synchronization. Note that the 7th most recent
		slot is at an offset of -6 relative to the current slot, hence the -1. */
		int slot_offset = slots_passed;
		int max_read_offset = (slot_index <= end_slot_index) ? slot_index - end_slot_index + 7 : slot_index - end_slot_index - 1;
		for (; slot_offset > max_read_offset; slot_offset--) {
			/* Empty filler slot. */
			slot_num++;
		}

		struct metamac_slot slots[8];
		int ai = 0;

		for (; slot_offset > 0; slot_offset--) {
			int si = slot_index - slot_offset;
			si = si < 0 ? si + 8 : si;

			slots[ai].slot_num = slot_num++;
			slots[ai].read_num = read_num;
			slots[ai].host_time = loop_start;
			slots[ai].tsf_time = tsf;
			slots[ai].slot_index = slot_index;
			slots[ai].slots_passed = slots_passed;
			slots[ai].filler = 0;
			slots[ai].packet_queued = (packet_queued >> si) & 1;
			slots[ai].transmitted = (transmitted >> si) & 1;
			slots[ai].transmit_success = (transmit_success >> si) & 1;
			slots[ai].transmit_other = (transmit_other >> si) & 1;
			slots[ai].bad_reception = (bad_reception >> si) & 1;
			slots[ai].busy_slot = (busy_slot >> si) & 1;
			slots[ai].channel_busy = (channel_busy >> si) & 1;
			ai++;
		}

		queue_multipush(queue, slots, ai);

		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		loop_end = (current_time.tv_sec - start_time.tv_sec) * 1000000L +
			(current_time.tv_nsec - start_time.tv_nsec) / 1000L;

		int64_t delay = ((int64_t)loop_start) + read_interval - ((int64_t)loop_end);
		if (delay > 0) {
			usleep(delay);
		}

		read_num++;
	}

	usleep(10000);
	queue_signal(queue);

	return metamac_loop_break;
}



int socket_commands_receive(struct metamac_queue *queue, struct debugfs_file *df, metamac_flag_t flags, int *fixed_protocol, struct protocol_suite *suite)
{

    char command[BUFFSIZE];
  
    int sock;
    struct sockaddr_in echoserver;
    struct sockaddr_in echoclient;
    char buffer[BUFFSIZE];
    unsigned int echolen, clientlen, serverlen;
    int received = 0;
	/* Create the UDP socket */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    	Die("Failed to create socket");
    }

    /* Construct the server sockaddr_in structure */
    memset(&echoserver, 0, sizeof(echoserver));       /* Clear struct */
    echoserver.sin_family = AF_INET;                  /* Internet/IP */
    echoserver.sin_addr.s_addr = htonl(INADDR_ANY);   /* Any IP address */
    echoserver.sin_port = htons(atoi("8400"));       /* server port */

    /* Bind the socket */
    serverlen = sizeof(echoserver);
    if (bind(sock, (struct sockaddr *) &echoserver, serverlen) < 0) {
    	Die("Failed to bind server socket");
    }

    printf("start command server\n");
    while (metamac_loop_break == 0) {

	  /* Receive a message from the client */
	  clientlen = sizeof(echoclient);
	  if ((received = recvfrom(sock, buffer, BUFFSIZE, 0,
                                       (struct sockaddr *) &echoclient,
                                       &clientlen)) < 0) {
                Die("Failed to receive message");
	  }
	  printf("Client connected: %s - message : %s\n", inet_ntoa(echoclient.sin_addr), buffer);

	  //{"username":"ece","says":"hello"}
	  //sscanf(data, "{\"username\":\"%[^\"]\",\"says\":\"%s\"}", name, msg);
	  //[{"command":"slot1"}]
	  sscanf(buffer, "[{\"command\":\"%[^\"]\"}]", command);
	  /* Send the message back to client */
	  /*if (sendto(sock, buffer, received, 0,
			  (struct sockaddr *) &echoclient,
			  sizeof(echoclient)) != received) {
		  Die("Mismatch in number of echo'd bytes");
	  }*/

	  /* fixed_protocol = 0:metamac 1:protocol0 2:protocol1 3:protocol2 4:protocol3
	   * active_protocol = 0:protocol0 1:protocol1 2:protocol2 3:protocol3
	   */
	  
	  if ( strcmp(command, "METAMAC") == 0 ) {
	    printf("metamac\n");
	    *fixed_protocol = 0;
	  }
	  if ( strcmp(command, "Protocol 1") == 0 || strcmp(command, "TDMA 1") == 0) {
	    printf("1\n");
	    *fixed_protocol = 1;
	    load_protocol(df, suite, 0);
	  }
	  if ( strcmp(command, "Protocol 2") == 0 || strcmp(command, "TDMA 2") == 0) {
	    printf("2\n");
	    *fixed_protocol = 2;
	    load_protocol(df, suite, 1);
	  }
	  if ( strcmp(command, "Protocol 3") == 0 || strcmp(command, "TDMA 3") == 0) {
	    printf("3\n");
	    *fixed_protocol = 3;
	    load_protocol(df, suite, 2);
	  }
	  if ( strcmp(command, "Protocol 4") == 0 || strcmp(command, "TDMA 4") == 0) {
	    printf("4\n");
	    *fixed_protocol = 4;
	    load_protocol(df, suite, 3);
	  }
	  if ( strcmp(command, "Protocol 5") == 0 || strcmp(command, "ALOHA") == 0) {
	    printf("4\n");
	    *fixed_protocol = 5;
	    load_protocol(df, suite, 4);
	  }
	  


    }
    return metamac_loop_break;
}


int metamac_process_loop(struct metamac_queue *queue, struct debugfs_file *df,
	struct protocol_suite *suite, metamac_flag_t flags, const char *logpath, int *fixed_protocol, const char * logging_server)
{
	FILE * logfile;
	if (flags & FLAG_LOGGING) {
		logfile = fopen(logpath, "w+");
		if (logfile == NULL) {
			err(EXIT_FAILURE, "Unable to open log file");
		}

		fprintf(logfile, "slot_num,offset,read_num,host_time,tsf_time,slot_index,slots_passed,filler,packet_queued,transmitted,transmit_success,transmit_other,bad_reception,busy_slot,channel_busy,pcoll,protocol");
		for (int i = 0; i < suite->num_protocols; i++) {
			fprintf(logfile, ",%s", suite->protocols[i].name);
		}
		fprintf(logfile, "\n");
		printf("Logging to %s\n", logpath);
	} else {
		logfile = NULL;
	}

	
	
	
	
	
	//c socket variables for client
	int sock;
	struct sockaddr_in echoserver;
        struct sockaddr_in echoclient;
        char buffer[BUFFSIZE];
        unsigned int echolen, clientlen;
        int received = 0;
	/* Create the UDP socket */
        if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
	  Die("Failed to create socket");
        }
        /* Construct the server sockaddr_in structure */
        memset(&echoserver, 0, sizeof(echoserver));       /* Clear struct */
        echoserver.sin_family = AF_INET;                  /* Internet/IP */
 
          echoserver.sin_addr.s_addr = inet_addr(logging_server);  /* IP address */
 //         echoserver.sin_port = htons(atoi(argv[3]));       /* server port */
         
	  //echoserver.sin_addr.s_addr = inet_addr("10.8.8.6");  /* IP address */
         //echoserver.sin_addr.s_addr = inet_addr("10.8.19.1");  /* IP address */
         echoserver.sin_port = htons(atoi("4321"));       /* server port */

	
	
	
	
	unsigned long loop = 0;
	struct timespec last_update_time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &last_update_time);

	metamac_loop_break = 0;
	unsigned long long count_transmitted=0;
	unsigned long long count_transmit_success=0;
	unsigned long long count_transmitted_=0;
	unsigned long long count_transmit_success_=0;
	double pcoll = 0;
	while (metamac_loop_break == 0) {

		struct metamac_slot slots[16];
		size_t count = queue_multipop(queue, slots, ARRAY_SIZE(slots));
		for (int i = 0; i < count; i++) {
			//evaluate the best protocol
			update_weights(suite, slots[i]);

			count_transmitted+=slots[i].transmitted;
			count_transmit_success+=slots[i].transmit_success;
			if (logfile != NULL) {
				fprintf(logfile, "%llu,%d,%llu,%llu,%llu,%d,%d,%01x,%01x,%01x,%01x,%01x,%01x,%01x,%01x,%e,%llu,%llu,%llu,%llu,%s",
					(unsigned long long) slots[i].slot_num,
					suite->slot_offset,
					(unsigned long long) slots[i].read_num,
					(unsigned long long) slots[i].host_time,
					(unsigned long long) slots[i].tsf_time,
					slots[i].slot_index,
					slots[i].slots_passed,
					slots[i].filler,
					slots[i].packet_queued,
					slots[i].transmitted,
					slots[i].transmit_success,
					slots[i].transmit_other,
					slots[i].bad_reception,
					slots[i].busy_slot,
					slots[i].channel_busy,
					pcoll,
					count_transmitted,
					count_transmit_success,
					count_transmitted_,
					count_transmit_success_,
					suite->protocols[suite->active_protocol].name);

				for (int i = 0; i < suite->num_protocols; i++) {
					fprintf(logfile, ",%e", suite->weights[i]);
				}

				fprintf(logfile, "\n");
			}

		}

		struct timespec current_time;
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);

		unsigned long timediff = (current_time.tv_sec - last_update_time.tv_sec) * 1000000L
			+ (current_time.tv_nsec - last_update_time.tv_nsec) / 1000L;

		/* Update running protocol. */
		if (!(flags & FLAG_READONLY) && (*fixed_protocol == 0)) {
			metamac_evaluate(df, suite);
		}

		/* Update display every 1 second. */
		if (timediff > 1000000L) {
			//compute pcoll every timediff
			if ((count_transmit_success-count_transmit_success_)>0){
				pcoll = 1 -  (double)(count_transmit_success-count_transmit_success_)/(double)(count_transmitted-count_transmitted_) ;
			}
			count_transmitted_ = count_transmitted; 
			count_transmit_success_ = count_transmit_success; 
			if (flags & FLAG_VERBOSE) {
				metamac_display(loop++, suite);
			}
			if (flags & FLAG_REMOTE_LOGGING) {
				/* Send the word to the server */
 				int ii;
				sprintf(buffer, "{\"active\" : \"%d\"", suite->active_protocol); 
				for (ii = 0; ii < suite->num_protocols; ii++) {
				      sprintf(buffer, "%s, \"%d\":[\"%5.3f\", \"%s\"]", buffer, ii, suite->weights[ii], suite->protocols[ii].name);
				}
				sprintf(buffer, "%s}", buffer); 
				//printf("%s\n", buffer);
				echolen = strlen(buffer);
 				if (sendto(sock, buffer, echolen, 0, (struct sockaddr *) &echoserver, sizeof(echoserver)) != echolen) {
 				    Die("Mismatch in number of sent bytes");	
 				}
				
			}

			last_update_time = current_time;
		}
	}

	if (logfile != NULL) {
		fclose(logfile);
	}

	return metamac_loop_break;
}
