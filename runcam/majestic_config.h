#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct majestic_config_data {
    char **rows;
    size_t count;
    size_t capacity;
    ssize_t crop_row;
    ssize_t insert_row;
    size_t section_indent;
};

extern struct majestic_config_data g_majestic_config;
extern const char *const MAJESTIC_DEFAULT_CONFIG_PATH;

bool majestic_config_init(void);
void majestic_config_free(void);
bool majestic_config_set_crop(const char *crop, bool ensure_exists);
