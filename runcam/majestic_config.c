#include "majestic_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct majestic_config_data g_majestic_config = {0};

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
