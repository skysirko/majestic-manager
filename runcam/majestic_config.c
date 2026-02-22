#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "third_party/libyaml-0.2.5/include/yaml.h"

#include "majestic_config.h"

static int node_to_id(const yaml_document_t *document, const yaml_node_t *node) {
    if (!document || !node || document->nodes.start == NULL) {
        return 0;
    }

    return (int)(node - document->nodes.start) + 1;
}

static bool scalar_matches(const yaml_node_t *node, const char *value) {
    if (!node || node->type != YAML_SCALAR_NODE) {
        return false;
    }

    return strcmp((const char *)node->data.scalar.value, value) == 0;
}

static int create_scalar(yaml_document_t *document, const char *value) {
    return yaml_document_add_scalar(
        document,
        (const yaml_char_t *)YAML_DEFAULT_SCALAR_TAG,
        (const yaml_char_t *)value,
        (int)strlen(value),
        YAML_PLAIN_SCALAR_STYLE);
}

static int ensure_child_mapping(
    yaml_document_t *document,
    yaml_node_t *parent_node,
    int parent_id,
    const char *key,
    yaml_node_t **out_node) {

    if (!document || !parent_node || parent_node->type != YAML_MAPPING_NODE) {
        return 0;
    }

    yaml_node_pair_t *pair = parent_node->data.mapping.pairs.start;

    while (pair && pair < parent_node->data.mapping.pairs.top) {
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);

        if (scalar_matches(key_node, key)) {
            yaml_node_t *value_node = yaml_document_get_node(document, pair->value);

            if (value_node && value_node->type == YAML_MAPPING_NODE) {
                *out_node = value_node;
                return pair->value;
            }

            const int new_mapping_id = yaml_document_add_mapping(
                document,
                (const yaml_char_t *)YAML_DEFAULT_MAPPING_TAG,
                YAML_BLOCK_MAPPING_STYLE);

            if (new_mapping_id == 0) {
                return 0;
            }

            pair->value = new_mapping_id;
            *out_node = yaml_document_get_node(document, new_mapping_id);
            return new_mapping_id;
        }

        pair++;
    }

    const int key_node_id = create_scalar(document, key);
    const int value_node_id = yaml_document_add_mapping(
        document,
        (const yaml_char_t *)YAML_DEFAULT_MAPPING_TAG,
        YAML_BLOCK_MAPPING_STYLE);

    if (key_node_id == 0 || value_node_id == 0) {
        return 0;
    }

    if (!yaml_document_append_mapping_pair(document, parent_id, key_node_id, value_node_id)) {
        return 0;
    }

    *out_node = yaml_document_get_node(document, value_node_id);
    return value_node_id;
}

static bool set_mapping_scalar(
    yaml_document_t *document,
    yaml_node_t *mapping_node,
    int mapping_id,
    const char *key,
    const char *value) {

    if (!document || !mapping_node || mapping_node->type != YAML_MAPPING_NODE) {
        return false;
    }

    const int new_scalar_id = create_scalar(document, value);

    if (new_scalar_id == 0) {
        return false;
    }

    yaml_node_pair_t *pair = mapping_node->data.mapping.pairs.start;

    while (pair && pair < mapping_node->data.mapping.pairs.top) {
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);

        if (scalar_matches(key_node, key)) {
            pair->value = new_scalar_id;
            return true;
        }

        pair++;
    }

    const int key_node_id = create_scalar(document, key);

    if (key_node_id == 0) {
        return false;
    }

    return yaml_document_append_mapping_pair(document, mapping_id, key_node_id, new_scalar_id);
}

static bool reload_document(
    const char *config_path,
    yaml_document_t *document,
    yaml_node_t **root_node,
    int *root_id) {

    FILE *input = fopen(config_path, "rb");

    if (!input) {
        if (errno == ENOENT) {
            fprintf(stderr, "%s does not exist; skipping update.\n", config_path);
        } else {
            fprintf(stderr, "Failed to open %s for reading: %s\n", config_path, strerror(errno));
        }
        return false;
    }

    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Failed to initialize YAML parser.\n");
        fclose(input);
        return false;
    }

    yaml_parser_set_input_file(&parser, input);

    if (!yaml_parser_load(&parser, document)) {
        fprintf(stderr, "YAML parser error while reading %s: %s (line %zu)\n",
                config_path,
                parser.problem ? parser.problem : "unknown",
                (size_t)parser.problem_mark.line + 1);
        yaml_parser_delete(&parser);
        fclose(input);
        return false;
    }

    yaml_parser_delete(&parser);
    fclose(input);

    *root_node = yaml_document_get_root_node(document);

    if (!*root_node || (*root_node)->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "Unexpected Majestic YAML structure. Expected mapping root.\n");
        yaml_document_delete(document);
        return false;
    }

    *root_id = node_to_id(document, *root_node);
    return true;
}

static bool persist_document(const char *config_path, yaml_document_t *document) {
    FILE *output = fopen(config_path, "wb");

    if (!output) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", config_path, strerror(errno));
        return false;
    }

    yaml_emitter_t emitter;

    if (!yaml_emitter_initialize(&emitter)) {
        fprintf(stderr, "Failed to initialize YAML emitter.\n");
        fclose(output);
        return false;
    }

    yaml_emitter_set_output_file(&emitter, output);

    if (!yaml_emitter_open(&emitter)) {
        fprintf(stderr, "Failed to open YAML stream for writing %s.\n", config_path);
        yaml_emitter_delete(&emitter);
        fclose(output);
        return false;
    }

    if (!yaml_emitter_dump(&emitter, document)) {
        fprintf(stderr, "Failed to emit YAML document to %s.\n", config_path);
        yaml_emitter_delete(&emitter);
        fclose(output);
        return false;
    }

    if (!yaml_emitter_close(&emitter)) {
        fprintf(stderr, "Failed to finalize YAML stream for %s.\n", config_path);
        yaml_emitter_delete(&emitter);
        fclose(output);
        return false;
    }

    yaml_emitter_delete(&emitter);
    fclose(output);
    return true;
}

int majestic_config_set_crop(const char *config_path, const char *crop_value) {
    yaml_document_t document;
    yaml_node_t *root_node = NULL;
    int root_id = 0;

    if (!reload_document(config_path, &document, &root_node, &root_id)) {
        return -1;
    }

    yaml_node_t *video_node = NULL;
    const int video_id = ensure_child_mapping(&document, root_node, root_id, "video1", &video_node);

    if (video_id == 0 || !video_node) {
        fprintf(stderr, "Failed to ensure video1 section inside %s.\n", config_path);
        yaml_document_delete(&document);
        return -1;
    }

    if (!set_mapping_scalar(&document, video_node, video_id, "crop", crop_value)) {
        fprintf(stderr, "Failed to set crop field inside %s.\n", config_path);
        yaml_document_delete(&document);
        return -1;
    }

    if (!persist_document(config_path, &document)) {
        return -1;
    }

    return 0;
}
