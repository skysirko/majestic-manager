#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct majestic_config_entry {
    char *section;
    char *field;
    char *value;
};

struct majestic_config_data {
    struct majestic_config_entry *rows;
};

extern struct majestic_config_data g_majestic_config;
#define MAJESTIC_DEFAULT_CONFIG_PATH "runcam/majestic.yaml"

bool majestic_config_init(void);
void majestic_config_free(void);
bool majestic_config_set_crop(const char *crop);
