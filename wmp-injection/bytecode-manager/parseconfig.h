#ifndef PARSE_CONFIG_H
#define PARSE_CONFIG_H

#include "metamac.h"

struct protocol_suite *read_config(const char *program_name, const char *file_name, const char *fsm_basepath);

#endif // PARSE_CONFIG_H