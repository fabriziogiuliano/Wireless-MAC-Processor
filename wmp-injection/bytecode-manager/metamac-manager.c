#include "metamac.h"
#include "parseconfig.h"

#include <stdio.h>
#include <libxml/tree.h>
#include <libxml/parser.h>

int main(int argc, char *argv[])
{
	LIBXML_TEST_VERSION
	xmlInitParser();

	if (argc != 2) {
		fprintf(stderr, "Usage: %s CONFIG\n", argv[0]);
		return -1;
	}

	struct protocol_suite *suite = read_config("metamac", argv[1]);

	return 0;
}

/*int main(int argc, char *argv[])
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
}*/