#ifndef PARSE_CONFIG_H
#define PARSE_CONFIG_H

#include "metamac.h"

struct arguments {
  char *config;
  char *fsm_basepath;
  char *logpath;
  char *logging_server;
  double eta;
  metamac_flag_t metamac_flags;
};

void read_config(struct protocol_suite *suite, const char *program_name, struct arguments *arguments);

#endif // PARSE_CONFIG_H