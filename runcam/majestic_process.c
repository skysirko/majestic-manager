#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "majestic_process.h"

static const char *const MAJESTIC_PROCESS_NAME = "majestic";

static uint64_t monotonic_now_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
}

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

// Scan /proc to see if a process with the given comm name exists.
static bool is_majestic_running(void) {
    DIR *proc_dir = opendir("/proc");

    if (!proc_dir) {
        fprintf(stderr, "Unable to open /proc: %s\n", strerror(errno));
        return false;
    }

    struct dirent *entry = NULL;

    while ((entry = readdir(proc_dir)) != NULL) {
        if (!isdigit((unsigned char)entry->d_name[0])) {
            continue;
        }

        char comm_path[PATH_MAX];
        const int written = snprintf(
            comm_path,
            sizeof(comm_path),
            "/proc/%s/comm",
            entry->d_name);

        if (written <= 0 || (size_t)written >= sizeof(comm_path)) {
            continue;
        }

        FILE *comm = fopen(comm_path, "r");

        if (!comm) {
            continue;
        }

        char buffer[256];

        if (fgets(buffer, sizeof(buffer), comm) != NULL) {
            buffer[strcspn(buffer, "\n")] = '\0';

            if (strcmp(buffer, MAJESTIC_PROCESS_NAME) == 0) {
                fclose(comm);
                closedir(proc_dir);
                return true;
            }
        }

        fclose(comm);
    }

    closedir(proc_dir);
    return false;
}

static bool wait_for_majestic_running(uint64_t timeout_ms) {
    const uint64_t start_ms = monotonic_now_ms();
    while (1) {
        if (is_majestic_running()) {
            return true;
        }

        if (timeout_ms > 0) {
            const uint64_t now_ms = monotonic_now_ms();

            if (now_ms >= start_ms && now_ms - start_ms >= timeout_ms) {
                break;
            }
        }

        const struct timespec sleep_time = {
            .tv_sec = 0,
            .tv_nsec = 100 * 1000 * 1000
        };

        nanosleep(&sleep_time, NULL);
    }

    return false;
}

int reload_majestic_process(void) {
    // Ask Majestic to reload its configuration via SIGHUP.
    static const char *const hup_command[] = { "killall", "-1", MAJESTIC_PROCESS_NAME, NULL };

    const int status = run_command(hup_command);

    if (status == -2) {
        fprintf(stderr, "killall command not available; cannot reload Majestic.\n");
        return -1;
    }

    if (status != 0) {
        fprintf(stderr, "Unable to signal Majestic.\n");
        return -1;
    }

    fprintf(stderr, "Majestic reload (SIGHUP) succeeded.\n");

    if (!wait_for_majestic_running(5000)) {
        fprintf(stderr, "Majestic did not start after configuration update.\n");
        return -1;
    }

    return 0;
}
