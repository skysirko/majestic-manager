#include "majestic_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct majestic_config g_majestic_config = {0};

static void trim_trailing_spaces(char *str) {
    size_t len = strlen(str);
    while (len > 0 && str[len - 1] == ' ') {
        str[--len] = '\0';
    }
}

static bool ensure_section_capacity(struct majestic_config *config) {
    if (config->section_count < config->section_capacity) {
        return true;
    }

    // 8 -> 16 -> 31 -> ...
    size_t new_capacity = config->section_capacity ? config->section_capacity * 2 : 8;
    struct majestic_section *tmp = realloc(config->sections, new_capacity * sizeof(struct majestic_section));
    if (!tmp) {
        perror("realloc");
        return false;
    }

    config->sections = tmp;
    config->section_capacity = new_capacity;
    return true;
}

static bool ensure_entry_capacity(struct majestic_section *section) {
    if (section->entry_count < section->entry_capacity) {
        return true;
    }

    // 8 -> 16 -> 31 -> ...
    size_t new_capacity = section->entry_capacity ? section->entry_capacity * 2 : 8;
    struct majectic_section_entry *tmp = realloc(section->entries, new_capacity * sizeof(struct majectic_section_entry));
    if (!tmp) {
        perror("realloc");
        return false;
    }

    section->entries = tmp;
    section->entry_capacity = new_capacity;
    return true;
}

static struct majestic_section *create_section(struct majestic_config *config, const char *name) {
    if (!ensure_section_capacity(config)) {
        return NULL;
    }
    struct majestic_section *section = &config->sections[config->section_count];
    memset(section, 0, sizeof(*section));
    section->section = strdup(name);
    if (!section->section) {
        perror("strdup");
        return NULL;
    }
    config->section_count++;
    return section;
}

static bool add_entry(struct majestic_section *section, const char *field, const char *value) {
    if (!ensure_entry_capacity(section)) {
        return false;
    }
    struct majectic_section_entry *entry = &section->entries[section->entry_count];
    entry->field = strdup(field);
    if (!entry->field) {
        perror("strdup");
        return false;
    }
    entry->value = strdup(value);
    if (!entry->value) {
        perror("strdup");
        free(entry->field);
        entry->field = NULL;
        return false;
    }
    section->entry_count++;
    return true;
}

static bool write_config_file(void) {
    FILE *fp = fopen(MAJESTIC_CONFIG_FILE, "w");
    if (!fp) {
        perror("fopen");
        return false;
    }
    for (size_t i = 0; i < g_majestic_config.section_count; i++) {
        struct majestic_section *section = &g_majestic_config.sections[i];
        if (fprintf(fp, "%s:\n", section->section) < 0) {
            perror("fprintf");
            fclose(fp);
            return false;
        }
        for (size_t j = 0; j < section->entry_count; j++) {
            struct majectic_section_entry *entry = &section->entries[j];
            if (fprintf(fp, "  %s: %s\n", entry->field, entry->value ? entry->value : "") < 0) {
                perror("fprintf");
                fclose(fp);
                return false;
            }
        }
    }
    if (fclose(fp) != 0) {
        perror("fclose");
        return false;
    }
    return true;
}

bool majestic_config_init(void) {
    FILE *fp = fopen(MAJESTIC_CONFIG_FILE, "r");
    if (!fp) {
        perror("fopen");
        return false;
    }

    char *line = NULL;
    ssize_t linelen = 0;
    size_t linecap = 0;

    // read until EOF
    while ((linelen = getline(&line, &linecap, fp)) != -1) {

        // removing \n and \r
        while (linelen > 0) {
            char tail = line[linelen - 1];
            if (tail != '\n' && tail != '\r') {
                break;
            }
            line[--linelen] = '\0';
        }

        if (linelen == 0) {
            fprintf(stderr, "Empty lines not allowed\n");
            free(line);
            fclose(fp);
            return false;
        }

        // section
        if (line[0] != ' ') {

        } else { // field inside section

        }
    }

    free(line);
    fclose(fp);
    return true;
}

bool majestic_config_set_crop(const char *crop) {
    if (!crop) {
        return false;
    }

    // searching video1 section
    struct majestic_section *video1 = NULL;
    for (size_t i = 0; i < g_majestic_config.section_count; i++) {
        if (strcmp(g_majestic_config.sections[i].section, "video1") == 0) {
            video1 = &g_majestic_config.sections[i];
            break;
        }
    }
    if (!video1) {
        fprintf(stderr, "video1 section not found in %s\n", MAJESTIC_CONFIG_FILE);
        return false;
    }

    // searching crop entry
    struct majectic_section_entry *crop_entry = NULL;
    for (size_t i = 0; i < video1->entry_count; i++) {
        if (strcmp(video1->entries[i].field, "crop") == 0) {
            crop_entry = &video1->entries[i];
            break;
        }
    }
    if (!crop_entry) {
        fprintf(stderr, "crop field not found in video1 section\n");
        return false;
    }

    // update crop value
    char *new_value = strdup(crop);
    if (!new_value) {
        perror("strdup");
        return false;
    }
    char *old_value = crop_entry->value;
    crop_entry->value = new_value;
    if (!write_config_file()) {
        free(crop_entry->value);
        crop_entry->value = old_value;
        return false;
    }
    free(old_value);
    return true;
}
