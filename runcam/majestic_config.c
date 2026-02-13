#include "majestic_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct majestic_config_data g_majestic_config = {0};

bool majestic_config_init(void) {
    FILE *fp = fopen(MAJESTIC_DEFAULT_CONFIG_PATH, "r");
    if (!fp) {
        perror("fopen");
        return false;
    }

    char *line = NULL;
    size_t linecap = 0;
    char section[256] = {0};
    ssize_t linelen = 0;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        if (linelen == 0) {
            fprintf(stderr, "Empty lines in %s not allowed\n", MAJESTIC_DEFAULT_CONFIG_PATH);
            free(line);
            fclose(fp);
            return false;
        }

        while (linelen > 0) {
            char tail = line[linelen - 1];
            if (tail != '\n' && tail != '\r') {
                break;
            }
            line[--linelen] = '\0';
        }

        if (linelen == 0) {
            fprintf(stderr, "Empty lines in %s not allowed\n", MAJESTIC_DEFAULT_CONFIG_PATH);
            free(line);
            fclose(fp);
            return false;
        }

        if (linelen >= 2 && line[0] == ' ' && line[1] == ' ') {
            const char *value = line + 2;
            while (*value == ' ') {
                value++;
            }
            strncpy(section, value, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';
        }
    }

    free(line);
    fclose(fp);
    return true;
}

void majestic_config_free(void) {
}

bool majestic_config_set_crop(const char *crop) {
    (void)crop;
    return false;
}
