#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define MAJESTIC_CONFIG_FILE "runcam/majestic.yaml"

struct majectic_section_entry {
    char *field;
    char *value;
};

struct majestic_section {
    char *section;
    struct majectic_section_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
};

struct majestic_config {
    struct majestic_section *sections;
    size_t section_count;
    size_t section_capacity;
};

extern struct majestic_config g_majestic_config;

bool majestic_config_init(void);
bool majestic_config_set_crop(const char *crop);
