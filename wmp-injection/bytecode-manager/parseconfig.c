#include "parseconfig.h"
#include "protocols.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <err.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

char *dup_and_free(xmlChar *s)
{
	if (s == NULL) {
		return NULL;
	}

	int len = strlen((char*)s);
	char *t = (char*)malloc(len + 1);
	memcpy(t, s, len + 1);
	xmlFree(s);
	return t;
}

int xml_count_children(xmlNode *node, char *tag)
{
	xmlNode *current = node->children;
	int count = 0;

	while (current != 0) {
		if (strcmp((char*)current->name, tag) == 0) {
			++count;
		}
		current = current->next;
	}

	return count;
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

int parse_int_param(char *key, char *value)
{
	int val;
	if (sscanf(value, "%d", &val) < 1) {
		errx(EXIT_FAILURE, "Invalid configuration file: Invalid %s value: %s.\n", key, value);
	}
	return val;
}

struct aloha_param *read_aloha_params(xmlNode *emulator_node)
{
	double persistence = 0.0;	

	xmlNode *param_node = emulator_node->children;
	while (param_node) {
		if (strcmp((char*)param_node->name, "param") != 0) {
			param_node = param_node->next;
			continue;
		}

		char *key = (char*)xmlGetProp(param_node, (xmlChar*)"key");
		char *value = (char*)xmlGetProp(param_node, (xmlChar*)"value");

		if (!key) {
			errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"key\" attribute on <param> node");
		}
		if (!value) {
			errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"value\" attribute on <param> node");
		}

		if (strcmp(key, "persistence") != 0) {
			errx(EXIT_FAILURE, "Invalid configuration file: Unexpected parameter: %s.\n", key);
		}

		if (sscanf(value, "%lf", &persistence) < 1) {
			errx(EXIT_FAILURE, "Invalid configuration file: Invalid persistence value: %s.\n", value);
		}
		if (persistence <= 0.0 || persistence > 1.0) {
			errx(EXIT_FAILURE, "Invalid configuration file: Invalid persistence value: %s.\n", value);
		}

		xmlFree(key);
		xmlFree(value);

		param_node = param_node->next;
	}

	if (persistence == 0.0) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing param \"persistence\"");
	}

	struct aloha_param *param = (struct aloha_param*)malloc(sizeof(struct aloha_param));
	if (param == NULL) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	param->persistence = persistence;
	return param;
}

struct tdma_param *read_tdma_params(xmlNode *emulator_node)
{
	int frame_offset = -1;
	int frame_length = -1;
	int slot_assignment = -1;

	xmlNode *param_node = emulator_node->children;
	while (param_node) {
		if (strcmp((char*)param_node->name, "param") != 0) {
			param_node = param_node->next;
			continue;
		}

		char *key = (char*)xmlGetProp(param_node, (xmlChar*)"key");
		char *value = (char*)xmlGetProp(param_node, (xmlChar*)"value");

		if (!key) {
			errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"key\" attribute on <param> node");
		}
		if (!value) {
			errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"value\" attribute on <param> node");
		}

		if (strcmp(key, "frame_offset") == 0) {
			frame_offset = parse_int_param("frame_offset", value);
			if (frame_offset < 0) {
				errx(EXIT_FAILURE, "Invalid configuration file: Invalid %s value: %s.\n", "frame_offset", value);
			}

		} else if (strcmp(key, "frame_length") == 0) {
			frame_length = parse_int_param("frame_length", value);
			if (frame_length <= 0) {
				errx(EXIT_FAILURE, "Invalid configuration file: Invalid %s value: %s.\n", "frame_length", value);
			}

		} else if (strcmp(key, "slot_assignment") == 0) {
			slot_assignment = parse_int_param("slot_assignment", value);
			if (slot_assignment < 0) {
				errx(EXIT_FAILURE, "Invalid configuration file: Invalid %s value: %s.\n", "slot_assignment", value);
			}

		} else {
			errx(EXIT_FAILURE, "Invalid configuration file: Unexpected parameter: %s.\n", key);
		}

		xmlFree(key);
		xmlFree(value);

		param_node = param_node->next;
	}

	if (frame_offset < 0) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing parameter \"frame_offset\"");
	}
	if (frame_length < 0) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing parameter \"frame_length\"");
	}
	if (slot_assignment < 0) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing parameter \"slot_assignment\"");
	}
	if (slot_assignment >= frame_length) {
		errx(EXIT_FAILURE, "Invalid configuration file: Invalid slot_assignment value: \"%d\".\n", slot_assignment);
	}

	struct tdma_param *param = (struct tdma_param*)malloc(sizeof(struct tdma_param));
	if (param == NULL) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	param->frame_offset = frame_offset;
	param->frame_length = frame_length;
	param->slot_assignment = slot_assignment;
	return param;
}

void read_protocol(struct protocol *proto, xmlNode *protocol_node)
{
	/* Parse id */
	char *id_str = (char*)xmlGetProp(protocol_node, (xmlChar*)"id");
	if (!id_str) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"id\" attribute on <protocol> node");
	}
	if (sscanf(id_str, "%d", &proto->id) < 1) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Invalid \"id\" attribute on <protocol> node");
	}
	xmlFree(id_str);

	proto->name = dup_and_free(xmlGetProp(protocol_node, (xmlChar*)"name"));
	if (!proto->name) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"name\" attribute on <protocol> node");
	}

	xmlNode *fsm_node = xml_child_by_name(protocol_node, "fsm");
	if (!fsm_node) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"fsm\" child of <protocol> node");
	}

	proto->fsm_path = dup_and_free(xmlGetProp(fsm_node, (xmlChar*)"path"));
	if (!proto->fsm_path) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"path\" attribute on <fsm> node");
	}

	xmlNode *emulator_node = xml_child_by_name(protocol_node, "emulator");
	if (!emulator_node) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"emulator\" child of <protocol> node");
	}

	char *type = (char*)xmlGetProp(emulator_node, (xmlChar*)"type");
	if (!type) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s.\n", "Missing \"type\" attribute on <emulator> node");
	}

	if (strcmp(type, "aloha") == 0) {
		proto->emulator = aloha_emulate;
		proto->parameter = read_aloha_params(emulator_node);
	} else if (strcmp(type, "tdma") == 0) {
		proto->emulator = tdma_emulate;
		proto->parameter = read_tdma_params(emulator_node);
	} else {
		errx(EXIT_FAILURE, "Invalid configuration file: Unknown emulator type %s.\n", type);
	}

	xmlFree(type);
}

struct protocol_suite *read_config(char *program_name, char *file_name)
{
	xmlDoc *doc = xmlParseFile(file_name);
	if (!doc) {
		errx(EXIT_FAILURE, "Invalid configuration file.\n");
	}

	xmlNode *metamac_node = xmlDocGetRootElement(doc);
	if (!metamac_node || strcmp((char*)metamac_node->name, "metamac") != 0) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s\n", "Root node should be <metamac>.");
	}

	/* eta is a required attribute */
	char* eta_str = (char*)xmlGetProp(metamac_node, (xmlChar*)"eta");
	if (!eta_str) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s\n", "Missing \"eta\" attribute on <metamac> node.");
	}
	double eta;
	if (sscanf(eta_str, "%lf", &eta) < 1) {
		errx(EXIT_FAILURE, "Invalid configuration file: Invalid eta value \"%s\".\n", eta_str);
	}
	xmlFree(eta_str);

	/* initial-protocol is optional */
	char *initial_str = (char*)xmlGetProp(metamac_node, (xmlChar*)"initial-protocol");
	int initial;
	if (initial_str) {
		if (sscanf(initial_str, "%d", &initial) < 1) {
			errx(EXIT_FAILURE, "Invalid configuration file: Invalid initial-protocol value \"%s\".\n", initial_str);
		}
		xmlFree(initial_str);
	} else {
		initial = 0;
	}

	int num_protocols = xml_count_children(metamac_node, "protocol");
	if (num_protocols == 0) {
		errx(EXIT_FAILURE, "Invalid configuration file: %s\n", "There must be at least one component protocol.");
	}

	struct protocol_suite *suite = (struct protocol_suite*)malloc(sizeof(struct protocol_suite));
	if (suite == NULL) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	init_protocol_suite(suite, num_protocols, eta);

	xmlNode *protocol_node = metamac_node->children;
	int index = 0;
	while (protocol_node) {
		if (strcmp((char*)protocol_node->name, "protocol") != 0) {
			protocol_node = protocol_node->next;
			continue;
		}

		read_protocol(&suite->protocols[index++], protocol_node);
		protocol_node = protocol_node->next;
	}

	for (index = 0; index < num_protocols; index++) {
		if (suite->protocols[index].id == initial) {
			suite->best_protocol = index;
		}
	}
	
	xmlFreeDoc(doc);
	return suite;
}
