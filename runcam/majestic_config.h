#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define MAJESTIC_CONFIG_FILE "runcam/majestic.yaml"

struct majestic_config_entry {
    char *section;
    char *field;
    char *value;
};

struct majestic_config_data {
    struct majestic_config_entry *rows;
};

extern struct majestic_config_data g_majestic_config;

bool majestic_config_init(void);
bool majestic_config_set_crop(const char *crop);
