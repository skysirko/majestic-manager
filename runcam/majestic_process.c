#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "majestic_process.h"

static const char *const MAJESTIC_INIT_SCRIPT = "/etc/init.d/S95majestic";

// Spawn a short-lived child to run the command and return its status.
static int run_command(const char *const *argv) {
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "Failed to fork for %s: %s\n", argv[0], strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        const int exit_code = errno == ENOENT ? 127 : 126;
        _exit(exit_code);
    }

    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "Failed to wait for %s: %s\n", argv[0], strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);

        if (exit_code == 0) {
            return 0;
        }

        if (exit_code == 127) {
            return -2;
        }
    }

    return -1;
}

int reload_majestic_process(void) {
    static const char *const restart_command[] = { MAJESTIC_INIT_SCRIPT, "reload", NULL };

    const int status = run_command(restart_command);

    if (status == -2) {
        fprintf(stderr, "Init script %s not available; cannot restart Majestic.\n", MAJESTIC_INIT_SCRIPT);
        return -1;
    }

    if (status != 0) {
        fprintf(stderr, "Init script %s failed to restart Majestic.\n", MAJESTIC_INIT_SCRIPT);
        return -1;
    }

    return 0;
}
