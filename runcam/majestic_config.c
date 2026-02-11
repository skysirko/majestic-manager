#include "majestic_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct majestic_config_data g_majestic_config = {0};

static char *g_config_path = NULL;

static void config_store_reset(struct majestic_config_data *store) {
    for (size_t i = 0; i < store->count; ++i) {
        free(store->rows[i]);
    }
    free(store->rows);
    memset(store, 0, sizeof(*store));
    store->crop_row = -1;
    store->insert_row = -1;
}

static bool config_store_push_line(struct majestic_config_data *store, const char *line, size_t len) {
    if (store->count == store->capacity) {
        size_t new_capacity = store->capacity ? store->capacity * 2 : 32;
        char **new_rows = realloc(store->rows, new_capacity * sizeof(char *));
        if (!new_rows) {
            return false;
        }
        store->rows = new_rows;
        store->capacity = new_capacity;
    }
    store->rows[store->count] = strndup(line, len);
    if (!store->rows[store->count]) {
        return false;
    }
    store->count += 1;
    return true;
}

static void config_store_refresh_metadata(struct majestic_config_data *store) {
    store->crop_row = -1;
    store->insert_row = -1;
    store->section_indent = 0;

    bool in_video1 = false;
    size_t section_indent = 0;
    for (size_t i = 0; i < store->count; ++i) {
        char *current = store->rows[i];
        size_t len = strlen(current);
        size_t indent = 0;
        while (indent < len && (current[indent] == ' ' || current[indent] == '\t')) {
            indent++;
        }
        const char *trimmed = current + indent;

        if (strncmp(trimmed, "video1:", 7) == 0 &&
            (trimmed[7] == '\0' || trimmed[7] == '\n' || trimmed[7] == '\r' ||
             isspace((unsigned char)trimmed[7]))) {
            in_video1 = true;
            section_indent = indent;
            store->insert_row = (ssize_t)i + 1;
            continue;
        }

        if (in_video1 && indent <= section_indent && trimmed[0] != '\0') {
            in_video1 = false;
        }

        if (in_video1 && strncmp(trimmed, "crop:", 5) == 0) {
            store->crop_row = (ssize_t)i;
            store->section_indent = section_indent;
            return;
        }
    }
    if (store->insert_row != -1) {
        store->section_indent = section_indent;
    }
}

static bool config_store_save(const struct majestic_config_data *store) {
    if (!g_config_path) {
        fprintf(stderr, "Majestic config path is not set; cannot save.\n");
        return false;
    }

    FILE *out = fopen(g_config_path, "w");
    if (!out) {
        perror("fopen");
        return false;
    }
    for (size_t i = 0; i < store->count; ++i) {
        fputs(store->rows[i], out);
    }
    fclose(out);
    return true;
}

static bool config_store_load(void) {
    if (!g_config_path) {
        fprintf(stderr, "Majestic config path is not set; cannot load.\n");
        return false;
    }

    FILE *file = fopen(g_config_path, "r");
    if (!file) {
        fprintf(stderr, "Majestic config not found at %s; skipping crop update.\n", g_config_path);
        return false;
    }

    config_store_reset(&g_majestic_config);

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    while ((line_len = getline(&line, &line_cap, file)) != -1) {
        if (!config_store_push_line(&g_majestic_config, line, (size_t)line_len)) {
            fprintf(stderr, "Out of memory while reading config.\n");
            free(line);
            fclose(file);
            config_store_reset(&g_majestic_config);
            return false;
        }
    }
    free(line);
    fclose(file);

    config_store_refresh_metadata(&g_majestic_config);
    return true;
}

bool majestic_config_init(const char *path) {
    if (!path) {
        return false;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return false;
    }
    free(g_config_path);
    g_config_path = path_copy;

    return config_store_load();
}

void majestic_config_free(void) {
    config_store_reset(&g_majestic_config);
    free(g_config_path);
    g_config_path = NULL;
}

bool majestic_config_set_crop(const char *crop, bool ensure_exists) {
    if (g_majestic_config.count == 0) {
        fprintf(stderr, "Majestic config not loaded; skipping crop update.\n");
        return false;
    }

    config_store_refresh_metadata(&g_majestic_config);

    bool updated = false;

    if (g_majestic_config.crop_row >= 0) {
        char *current = g_majestic_config.rows[g_majestic_config.crop_row];
        size_t len = strlen(current);
        size_t indent = 0;
        while (indent < len && (current[indent] == ' ' || current[indent] == '\t')) {
            indent++;
        }
        size_t new_len = indent + strlen("crop: ") + strlen(crop) + 2;
        char *replacement = malloc(new_len);
        if (!replacement) {
            return false;
        }
        memcpy(replacement, current, indent);
        snprintf(replacement + indent, new_len - indent, "crop: %s\n", crop);
        free(g_majestic_config.rows[g_majestic_config.crop_row]);
        g_majestic_config.rows[g_majestic_config.crop_row] = replacement;
        updated = true;
    } else if (ensure_exists && g_majestic_config.insert_row >= 0) {
        size_t indent_len = g_majestic_config.section_indent + 2;
        size_t new_len = indent_len + strlen("crop: ") + strlen(crop) + 2;
        char *insert_line = malloc(new_len);
        if (!insert_line) {
            return false;
        }
        memset(insert_line, ' ', indent_len);
        snprintf(insert_line + indent_len, new_len - indent_len, "crop: %s\n", crop);

        if (g_majestic_config.count == g_majestic_config.capacity) {
            size_t new_capacity = g_majestic_config.capacity ? g_majestic_config.capacity * 2 : 32;
            char **new_rows = realloc(g_majestic_config.rows, new_capacity * sizeof(char *));
            if (!new_rows) {
                free(insert_line);
                return false;
            }
            g_majestic_config.rows = new_rows;
            g_majestic_config.capacity = new_capacity;
        }
        size_t insert_pos = (size_t)g_majestic_config.insert_row;
        size_t move_count = g_majestic_config.count - insert_pos;
        memmove(&g_majestic_config.rows[insert_pos + 1], &g_majestic_config.rows[insert_pos],
                move_count * sizeof(char *));
        g_majestic_config.rows[insert_pos] = insert_line;
        g_majestic_config.count += 1;
        updated = true;
    } else if (!ensure_exists) {
        fprintf(stderr, "crop entry inside video1 not found; no changes written.\n");
    } else {
        fprintf(stderr, "video1 section not found; crop entry cannot be created.\n");
    }

    if (!updated) {
        return false;
    }

    if (!config_store_save(&g_majestic_config)) {
        return false;
    }
    return true;
}
