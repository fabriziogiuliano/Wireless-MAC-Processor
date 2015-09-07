#include "metamac.h"
#include "parseconfig.h"

#include <stdio.h>
#include <argp.h>
#include <signal.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

// ARG PARSING
const char *argp_program_version = "MetaMAC Host Daemon 0.0.1";
static char doc[] = "Switches MAC protocols in response to network activity.";
static char args_doc[] = "CONFIG";

static struct argp_option options[] = {
	{ "verbose", 'v', 0, 0, "Verbose output." },
	{ "logging", 'v', 0, 0, "Log MAC feedback for each slot." },
	{ "readonly", 'r', 0, 0, "Do not update running protocol." },
	{ 0 }
};

struct arguments {
	char *config;
	metamac_flag_t metamac_flags;
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
		break;

	case 'r':
		arguments->metamac_flags |= FLAG_READONLY;
		break;

	case ARGP_KEY_ARG:
		if (state->arg_num >= 1)
			argp_usage(state);

		switch (state->arg_num) {
		case 0:
			arguments->config = arg;
			break;
		default:
			break;
		}

		break;
	case ARGP_KEY_END:
		if (state->arg_num < 1)
			argp_usage(state);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

static void inthandler(int signum, siginfo_t *info, void *ptr)
{
	if (signum == SIGINT) {
		printf("Received SIGINT, exiting...");
		metamac_loop_break = SIGINT;
	}
}

static struct sigaction act;

int main(int argc, char *argv[])
{
	/* Set signal handler for interrupt signal. */
	act.sa_sigaction = inthandler;
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &act, NULL);

	struct arguments arguments;
	memset(&arguments, 0, sizeof(arguments));
	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	struct protocol_suite *suite = read_config(argv[0], arguments.config);

	struct debugfs_file df;
	init_file(&df);
	metamac_init(&df, suite, arguments.metamac_flags);

	metamac_loop(&df, suite, arguments.metamac_flags);

	return 0;
}