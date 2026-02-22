#pragma once

/**
 * Reload the Majestic process so configuration changes take effect.
 *
 * @return 0 on success, -1 on failure (details logged to stderr).
 */
int reload_majestic_process(void);
