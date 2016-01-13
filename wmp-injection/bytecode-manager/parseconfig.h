#ifndef PARSE_CONFIG_H
#define PARSE_CONFIG_H

#include "metamac.h"

struct arguments {
  char *config;
  char *fsm_basepath;
  char *logpath;
  double eta;
  metamac_flag_t metamac_flags;
};

struct protocol_suite *read_config(const char *program_name, struct arguments *arguments);

#endif // PARSE_CONFIG_H