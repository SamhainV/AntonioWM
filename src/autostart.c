#include "wm.h"
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AWM_RESTART_MARKER "_ANTONIOWM_RESTART_PID"

bool autostart_consume_restart_marker(void)
{
    const char *value = getenv(AWM_RESTART_MARKER);
    char *end = NULL;
    unsigned long marker_pid;
    bool matches = false;

    if (value != NULL && value[0] != '\0') {
        errno = 0;
        marker_pid = strtoul(value, &end, 10);
        if (errno == 0 && end != value && *end == '\0' &&
            marker_pid == (unsigned long)getpid()) {
            matches = true;
        }
    }
    if (unsetenv(AWM_RESTART_MARKER) != 0) {
        fprintf(stderr,
                "%s: no se pudo limpiar el marcador de reinicio: %s\n",
                AWM_NAME,
                strerror(errno));
    }
    return matches;
}

bool autostart_prepare_restart(void)
{
    char value[32];
    int written = snprintf(value, sizeof(value), "%lu", (unsigned long)getpid());

    if (written < 0 || (size_t)written >= sizeof(value)) {
        return false;
    }
    if (setenv(AWM_RESTART_MARKER, value, 1) != 0) {
        fprintf(stderr,
                "%s: no se pudo crear el marcador de reinicio: %s\n",
                AWM_NAME,
                strerror(errno));
        return false;
    }
    return true;
}

void autostart_run(void)
{
    size_t index;

    for (index = 0U; index < wm.config.autostart_count; ++index) {
        const char *command = wm.config.autostart_commands[index];

        if (command[0] == '\0') {
            fprintf(stderr,
                    "%s: autostart.%zu.command esta vacio o ausente; se omite\n",
                    AWM_NAME,
                    index + 1U);
            continue;
        }
        command_spawn(command);
    }
}
