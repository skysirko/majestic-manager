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

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
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

        // section never starts with space
        if (line[0] != ' ') {

        } else { // section field

        }
    }

    free(line);
    fclose(fp);
    return true;
}

bool majestic_config_set_crop(const char *crop) {
    (void)crop;
    return false;
}
