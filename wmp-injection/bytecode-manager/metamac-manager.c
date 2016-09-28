#include "metamac.h"
#include "parseconfig.h"

#include <stdio.h>
#include <argp.h>
#include <signal.h>
#include <sched.h>
#include <string.h>
#include <pthread.h>
#include <err.h>
#include <sys/signal.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

// ARG PARSING
const char *argp_program_version = "MetaMAC Host Daemon 0.0.1";
static char doc[] = "Switches MAC protocols in response to network activity.";
static char args_doc[] = "CONFIG";

static struct argp_option options[] = {
	{ "verbose",  'v', 0,      0, "Verbose output." },
	{ "logfile",  'l', "FILE", 0, "File to log MAC feedback for each slot." },
	{ "readonly", 'r', 0,      0, "Do not update running protocol." },
	{ "cycle",    'c', 0,      0, "Force cycling of protocols."},
	{ "eta",      'e', "ETA",  0, "Learning constant eta (>= 0)."},
	{ "usebusy",  'b', 0,      0, "Use the busy slot feedback."},
	{ "remotelogging",  'g', "SERVR-IP-ADDRESS",      0, "Send logging to remote server."},
	{ 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *arguments = state->input;

	switch (key) {
	case 'v':
		arguments->metamac_flags |= FLAG_VERBOSE;
		break;

	case 'l':
		arguments->metamac_flags |= FLAG_LOGGING;
		arguments->logpath = arg;
		break;

	case 'g':
		arguments->metamac_flags |= FLAG_REMOTE_LOGGING;
		arguments->logging_server = arg;
		break;

	case 'r':
		arguments->metamac_flags |= FLAG_READONLY;
		break;

	case 'c':
		arguments->metamac_flags |= FLAG_CYCLE;
		break;

	case 'e':
		if (sscanf(arg, "%lf", &arguments->eta) < 1 || arguments->eta <= 0.0) {
			argp_usage(state);
		}
		arguments->metamac_flags |= FLAG_ETA_OVERRIDE;
		break;

	case 'b':
		arguments->metamac_flags |= FLAG_USE_BUSY;
		break;

	case ARGP_KEY_ARG:
		if (state->arg_num >= 1) {
			argp_usage(state);
		}

		switch (state->arg_num) {
		case 0:
			arguments->config = arg;
			break;
		default:
			break;
		}

		break;
	case ARGP_KEY_END:
		if (state->arg_num < 1) {
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

static void sigint_handler(int signum, siginfo_t *info, void *ptr)
{
	if (signum == SIGINT) {
		printf("Received SIGINT, exiting...");
		metamac_loop_break = SIGINT;
	}
}

static struct sigaction sigact;

struct thread_params {
	struct protocol_suite suite;
	struct metamac_queue queue;
	struct debugfs_file df;
	metamac_flag_t flags;
	int fixed_protocol;
	
};

static void *run_read_loop(void *arg)
{
	struct thread_params *params = arg;

	/* Set the scheduling policy for this thread to SCHED_FIFO
	and the priority to 98 (second highest). */
	struct sched_param param = { .sched_priority = 98 };
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	metamac_read_loop(&params->queue, &params->df, params->flags, 2200, 8000);

	return (void*)NULL;
}


static void *run_socket_commands_receive(void *arg)
{
	struct thread_params *params = arg;

	socket_commands_receive(&params->queue, &params->df, params->flags, &params->fixed_protocol, &params->suite);

	return (void*)NULL;
}



int main(int argc, char *argv[])
{
	/* Set signal handler for interrupt signal. */
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_sigaction = sigint_handler;
	sigact.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sigact, NULL);

	/* Parse command line arguments. */
	struct arguments arguments;
	memset(&arguments, 0, sizeof(arguments));
	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	struct thread_params *params = malloc(sizeof(struct thread_params));
	if (!params) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	//struct protocol_suite *suite = read_config(argv[0], &arguments);
	read_config(&params->suite, argv[0], &arguments);

	queue_init(&params->queue, 256);
	init_file(&params->df);
	params->flags = arguments.metamac_flags;
	params->fixed_protocol = 0;
	
	metamac_init(&params->df, &params->suite, arguments.metamac_flags);

	pthread_t reader;
	pthread_create(&reader, NULL, run_read_loop, params);

	pthread_t commands_receive;
	pthread_create(&commands_receive, NULL, run_socket_commands_receive, params);


	metamac_process_loop(&params->queue, &params->df, &params->suite,
		arguments.metamac_flags, arguments.logpath, &params->fixed_protocol, arguments.logging_server);

	pthread_join(reader, NULL);

	//pthread_join(commands_receive, NULL);
	//kill thread sending SIG_TERM 
	pthread_kill(commands_receive, SIGTERM);

	queue_destroy(&params->queue);
	close_file(&params->df);
	free(params);

	free_protocol_suite(&params->suite);

	return 0;
}
