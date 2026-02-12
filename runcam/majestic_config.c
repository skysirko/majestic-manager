#include "majestic_config.h"

#include <stdio.h>
#include <stdlib.h>

struct majestic_config_data g_majestic_config = {0};

bool majestic_config_init(void) {
    FILE *fp = fopen(MAJESTIC_DEFAULT_CONFIG_PATH, "r");
    if (!fp) {
        perror("fopen");
        return false;
    }

    char *line = NULL;
    size_t linecap = 0;

    while (getline(&line, &linecap, fp) != -1) {
        /* no-op */
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
