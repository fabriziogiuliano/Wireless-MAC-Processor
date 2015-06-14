#include "parseconfig.h"
#include "protocols.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

int xml_count_children(xmlNode *node);
int read_protocol(struct protocol_suite *suite, int index, xmlNode *protocol_node, int initial, char **message);
xmlNode *xml_child_by_name(xmlNode *node, char* name);

int xml_count_children(xmlNode *node)
{
	xmlNode *current = node->children;
	int count = 0;

	while (current != 0) {
		++count;
		current = current->next;
	}

	return count;
}

int read_protocol(struct protocol_suite *suite, int index, xmlNode *protocol_node, int initial, char **message)
{
	char *id_str = (char*)xmlGetProp(protocol_node, (xmlChar*)"id");
	if (!id_str) {
		*message = "Missing \"id\" attribute on <protocol> node.";
		return -1;
	}
	int id = atoi(id_str);
	xmlFree(id_str);

	char *name = (char*)xmlGetProp(protocol_node, (xmlChar*)"name");
	if (!name) {
		*message = "Missing \"name\" attribute on <protocol> node.";
		return -1;
	}

	xmlNode *fsm_node = xml_child_by_name(protocol_node, "fsm");
	if (!fsm_node) {
		*message = "Missing \"fsm\" child of <protocol> node.";
		return -1;
	}

	char *fsm_path = (char*)xmlGetProp(fsm_node, (xmlChar*)"path");
	if (!fsm_path) {
		*message = "Missing \"path\" attribute on <fsm> node.";
	}

	xmlNode *emulator_node = xml_child_by_name(protocol_node, "emulator");
	if (!emulator_node) {
		*message = "Missing \"emulator\" child of <protocol> node.";
		return -1;
	}

	char *type = (char*)xmlGetProp(emulator_node, (xmlChar*)"type");
	if (!type) {
		*message = "Missing \"type\" attribute on <emulator> node.";
		return -1;
	}

	protocol_emulator emulator = 0;
	if (strcmp(type, "aloha") == 0) {
		protocol_emulator = aloha_emulate;

	} else if (strcmp(type, "tdma") == 0) {

	} else {

	}

	return 0;
}

void *read_aloha_params(xmlNode *emulator_node, char **message)
{
	double persistence = 1.0;

	xmlNode *current = emulator_node->children;
	while (current != 0) {
		if (strcmp((char*)current->name, "param") != 0) {
			*message = "Unexpected child of <emulator> node.";
			return 0
		}

		char *key = (char*)xmlGetProp(current, (xmlChar*)"key");
		char *value = (char*)xmlGetProp(current, (xmlChar*)"value");

		if (!key) {
			*message = "Missing \"key\" attribute on <param> node.";
			return 0;
		}
		if (!value) {
			*message = "Missing \"value\" attribute on <param> node.";
			return 0;
		}
	}

	struct aloha_param *param = (struct aloha_param*)malloc(sizeof(struct aloha_param));
}

xmlNode *xml_child_by_name(xmlNode *node, char* name)
{
	xmlNode *current = node->children;

	while (current != 0) {
		if (strcmp((char*)current->name, name) == 0) {
			return current;
		}

		current = current->next;
	}

	return 0;
}

struct protocol_suite *read_config(char *program_name, char *file_name)
{
	char *message = 0;

	do {
		xmlDoc *doc = xmlParseFile(file_name);
		if (!doc)
			break;

		xmlNode *metamac_node = xmlDocGetRootElement(doc);
		if (!metamac_node || strcmp((char*)metamac_node->name, "metamac") != 0) {
			message = "Root node should be <metamac>.";
			break;
		}

		/* eta is a required attribute */
		char* eta_str = (char*)xmlGetProp(metamac_node, (xmlChar*)"eta");
		if (!eta_str) {
			message = "Missing \"eta\" attribute on <metamac> node.";
			break;
		}
		double eta = atof(eta_str);
		xmlFree(eta_str);

		/* initial-protocol is optional */
		char *initial_str = (char*)xmlGetProp(metamac_node, (xmlChar*)"initial-protocol");
		int initial = -1;
		if (initial_str) {
			initial = atoi(initial_str);
			xmlFree(initial_str);
		}

		int num_protocols = xml_count_children(metamac_node);
		if (num_protocols == 0) {
			message = "There must be at least one component protocol.";
			break;
		}

		struct protocol_suite *suite = (struct protocol_suite*)malloc(sizeof(struct protocol_suite));
		init_protocol_suite(suite, num_protocols, eta);
		
		xmlFreeDoc(doc);
		return suite;

	} while (0);

	/* Error handling */
	if (message) {
		fprintf(stderr, "%s: Invalid configuration file: %s\n", program_name, message);
	} else {
		fprintf(stderr, "%s: Invalid configuration file.\n", program_name);
	}
	return 0;
}
