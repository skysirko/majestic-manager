#pragma once

/**
 * Update (or create) the `video1.crop` entry inside the Majestic YAML config.
 *
 * @param config_path Absolute path to /etc/majestic.yaml (or override).
 * @param crop_value  New crop string (e.g., "0x0x1920x1080").
 *
 * @return 0 on success, -1 on error (details logged to stderr).
 */
int majestic_config_set_crop(const char *config_path, const char *crop_value);
