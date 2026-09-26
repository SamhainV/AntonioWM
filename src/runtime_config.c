#include "wm.h"
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const LauncherConfig default_launchers[] = AWM_TASKBAR_LAUNCHERS;
static const char *const default_desktop_names[] = AWM_DESKTOP_NAMES;

static bool copy_text(char *destination,
                      size_t capacity,
                      const char *value,
                      const char *key,
                      unsigned long line_number)
{
    size_t length;

    if (destination == NULL || capacity == 0U || value == NULL) {
        return false;
    }
    length = strlen(value);
    if (length >= capacity) {
        fprintf(stderr,
                "%s: valor demasiado largo para '%s' en la linea %lu\n",
                AWM_NAME,
                key,
                line_number);
        return false;
    }
    memcpy(destination, value, length + 1U);
    return true;
}

static void load_defaults(void)
{
    size_t index;
    size_t count = sizeof(default_launchers) / sizeof(default_launchers[0]);
    size_t desktop_count = sizeof(default_desktop_names) /
                           sizeof(default_desktop_names[0]);

    memset(&wm.config, 0, sizeof(wm.config));
    (void)copy_text(wm.config.terminal,
                    sizeof(wm.config.terminal),
                    AWM_TERMINAL,
                    "terminal",
                    0U);
    (void)copy_text(wm.config.screenshot_command,
                    sizeof(wm.config.screenshot_command),
                    AWM_SCREENSHOT_COMMAND,
                    "screenshot.command",
                    0U);
    (void)copy_text(wm.config.system_logout_command,
                    sizeof(wm.config.system_logout_command),
                    AWM_SYSTEM_LOGOUT_COMMAND,
                    "system.logout.command",
                    0U);
    (void)copy_text(wm.config.system_switch_user_command,
                    sizeof(wm.config.system_switch_user_command),
                    AWM_SYSTEM_SWITCH_USER_COMMAND,
                    "system.switch_user.command",
                    0U);
    (void)copy_text(wm.config.system_reboot_command,
                    sizeof(wm.config.system_reboot_command),
                    AWM_SYSTEM_REBOOT_COMMAND,
                    "system.reboot.command",
                    0U);
    (void)copy_text(wm.config.system_poweroff_command,
                    sizeof(wm.config.system_poweroff_command),
                    AWM_SYSTEM_POWEROFF_COMMAND,
                    "system.poweroff.command",
                    0U);
    wm.config.taskbar_enabled = AWM_TASKBAR_ENABLED != 0;
    wm.config.taskbar_height = AWM_TASKBAR_HEIGHT;
    wm.config.taskbar_gap = AWM_TASKBAR_GAP;
    wm.config.taskbar_padding = AWM_TASKBAR_PADDING;
    wm.config.focus_follows_mouse = AWM_FOCUS_FOLLOWS_MOUSE != 0;

    if (count > AWM_RUNTIME_MAX_LAUNCHERS) {
        count = AWM_RUNTIME_MAX_LAUNCHERS;
    }
    wm.config.launcher_count = count;
    for (index = 0U; index < count; ++index) {
        wm.config.launchers[index] = default_launchers[index];
    }

    if (desktop_count > AWM_RUNTIME_MAX_DESKTOPS) {
        desktop_count = AWM_RUNTIME_MAX_DESKTOPS;
    }
    for (index = 0U; index < desktop_count; ++index) {
        (void)copy_text(wm.config.desktop_names[index],
                        sizeof(wm.config.desktop_names[index]),
                        default_desktop_names[index],
                        "desktop",
                        0U);
    }
}

static char *trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text) != 0) {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]) != 0) {
        --end;
    }
    *end = '\0';
    return text;
}

static char *strip_quotes(char *value)
{
    size_t length = strlen(value);

    if (length >= 2U &&
        ((value[0] == '"' && value[length - 1U] == '"') ||
         (value[0] == '\'' && value[length - 1U] == '\''))) {
        value[length - 1U] = '\0';
        return value + 1;
    }
    return value;
}

static bool parse_boolean(const char *value, bool *result)
{
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) {
        *result = true;
        return true;
    }
    if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) {
        *result = false;
        return true;
    }
    return false;
}

static bool parse_number(const char *value,
                         long minimum,
                         long maximum,
                         long *result)
{
    char *end = NULL;
    long number;

    errno = 0;
    number = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || number < minimum ||
        number > maximum) {
        return false;
    }
    *result = number;
    return true;
}

static bool build_config_path(char *path, size_t capacity)
{
    const char *override = getenv("ANTONIOWM_CONFIG");
    const char *base;
    int written;

    if (override != NULL && override[0] != '\0') {
        return copy_text(path, capacity, override, "ANTONIOWM_CONFIG", 0U);
    }
    base = getenv("XDG_CONFIG_HOME");
    if (base != NULL && base[0] != '\0') {
        written = snprintf(path, capacity, "%s/antoniowm/config", base);
    } else {
        base = getenv("HOME");
        if (base == NULL || base[0] == '\0') {
            return false;
        }
        written = snprintf(path, capacity, "%s/.config/antoniowm/config", base);
    }
    return written >= 0 && (size_t)written < capacity;
}

static void config_directory(const char *path, char *directory, size_t capacity)
{
    const char *slash = strrchr(path, '/');
    size_t length;

    if (slash == NULL) {
        (void)copy_text(directory, capacity, ".", "config", 0U);
        return;
    }
    if (slash == path) {
        (void)copy_text(directory, capacity, "/", "config", 0U);
        return;
    }
    length = (size_t)(slash - path);
    if (length >= capacity) {
        directory[0] = '\0';
        return;
    }
    memcpy(directory, path, length);
    directory[length] = '\0';
}

static bool resolve_icon_path(char *destination,
                              size_t capacity,
                              const char *value,
                              const char *directory,
                              unsigned long line_number)
{
    const char *home;
    int written;

    if (value[0] == '\0') {
        destination[0] = '\0';
        return true;
    }
    if (value[0] == '/') {
        return copy_text(destination, capacity, value, "launcher.icon", line_number);
    }
    if (value[0] == '~' && value[1] == '/') {
        home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            fprintf(stderr,
                    "%s: no se puede expandir '~' en la linea %lu sin HOME\n",
                    AWM_NAME,
                    line_number);
            return false;
        }
        written = snprintf(destination, capacity, "%s/%s", home, value + 2);
    } else {
        written = snprintf(destination, capacity, "%s/%s", directory, value);
    }
    if (written < 0 || (size_t)written >= capacity) {
        fprintf(stderr,
                "%s: ruta de icono demasiado larga en la linea %lu\n",
                AWM_NAME,
                line_number);
        return false;
    }
    return true;
}

static void invalid_value(const char *key, unsigned long line_number)
{
    fprintf(stderr,
            "%s: valor no valido para '%s' en la linea %lu; se conserva el anterior\n",
            AWM_NAME,
            key,
            line_number);
}

static bool parse_indexed_key(const char *key,
                              const char *prefix,
                              size_t maximum,
                              size_t *index,
                              const char **field)
{
    const char *number_text;
    char *end = NULL;
    unsigned long number;
    size_t prefix_length = strlen(prefix);

    if (strncmp(key, prefix, prefix_length) != 0) {
        return false;
    }
    number_text = key + prefix_length;
    errno = 0;
    number = strtoul(number_text, &end, 10);
    if (errno != 0 || end == number_text || number == 0U || number > maximum ||
        end[0] != '.' || end[1] == '\0') {
        return false;
    }
    *index = (size_t)(number - 1U);
    *field = end + 1;
    return true;
}

static bool apply_launcher_value(const char *key,
                                 const char *value,
                                 const char *directory,
                                 unsigned long line_number)
{
    size_t index;
    const char *field;
    LauncherConfig *launcher;

    if (!parse_indexed_key(key,
                           "launcher.",
                           AWM_RUNTIME_MAX_LAUNCHERS,
                           &index,
                           &field)) {
        return false;
    }
    launcher = &wm.config.launchers[index];
    if (strcmp(field, "label") == 0) {
        (void)copy_text(launcher->label,
                        sizeof(launcher->label),
                        value,
                        key,
                        line_number);
    } else if (strcmp(field, "command") == 0) {
        (void)copy_text(launcher->command,
                        sizeof(launcher->command),
                        value,
                        key,
                        line_number);
    } else if (strcmp(field, "icon") == 0) {
        (void)resolve_icon_path(launcher->icon,
                                sizeof(launcher->icon),
                                value,
                                directory,
                                line_number);
    } else {
        return false;
    }
    if (index >= wm.config.launcher_count) {
        wm.config.launcher_count = index + 1U;
    }
    return true;
}

static bool apply_desktop_value(const char *key,
                                const char *value,
                                unsigned long line_number)
{
    const char *number_text;
    char *end = NULL;
    unsigned long number;
    size_t index;

    if (strncmp(key, "desktop.", 8U) != 0) {
        return false;
    }
    number_text = key + 8U;
    errno = 0;
    number = strtoul(number_text, &end, 10);
    if (errno != 0 || end == number_text || number == 0U ||
        number > AWM_RUNTIME_MAX_DESKTOPS ||
        (*end != '\0' && strcmp(end, ".name") != 0)) {
        return false;
    }
    index = (size_t)(number - 1U);
    (void)copy_text(wm.config.desktop_names[index],
                    sizeof(wm.config.desktop_names[index]),
                    value,
                    key,
                    line_number);
    return true;
}

static void apply_value(const char *key,
                        const char *value,
                        const char *directory,
                        unsigned long line_number)
{
    bool boolean;
    long number;

    if (strcmp(key, "terminal") == 0) {
        (void)copy_text(wm.config.terminal,
                        sizeof(wm.config.terminal),
                        value,
                        key,
                        line_number);
    } else if (strcmp(key, "screenshot.command") == 0) {
        (void)copy_text(wm.config.screenshot_command,
                        sizeof(wm.config.screenshot_command),
                        value,
                        key,
                        line_number);
    } else if (strcmp(key, "system.logout.command") == 0) {
        (void)copy_text(wm.config.system_logout_command,
                        sizeof(wm.config.system_logout_command),
                        value,
                        key,
                        line_number);
    } else if (strcmp(key, "system.switch_user.command") == 0) {
        (void)copy_text(wm.config.system_switch_user_command,
                        sizeof(wm.config.system_switch_user_command),
                        value,
                        key,
                        line_number);
    } else if (strcmp(key, "system.reboot.command") == 0) {
        (void)copy_text(wm.config.system_reboot_command,
                        sizeof(wm.config.system_reboot_command),
                        value,
                        key,
                        line_number);
    } else if (strcmp(key, "system.poweroff.command") == 0) {
        (void)copy_text(wm.config.system_poweroff_command,
                        sizeof(wm.config.system_poweroff_command),
                        value,
                        key,
                        line_number);
    } else if (strcmp(key, "taskbar.enabled") == 0) {
        if (parse_boolean(value, &boolean)) {
            wm.config.taskbar_enabled = boolean;
        } else {
            invalid_value(key, line_number);
        }
    } else if (strcmp(key, "taskbar.height") == 0) {
        if (parse_number(value, 0L, 256L, &number)) {
            wm.config.taskbar_height = (unsigned int)number;
        } else {
            invalid_value(key, line_number);
        }
    } else if (strcmp(key, "taskbar.gap") == 0) {
        if (parse_number(value, 0L, 64L, &number)) {
            wm.config.taskbar_gap = (int)number;
        } else {
            invalid_value(key, line_number);
        }
    } else if (strcmp(key, "taskbar.padding") == 0) {
        if (parse_number(value, 0L, 64L, &number)) {
            wm.config.taskbar_padding = (int)number;
        } else {
            invalid_value(key, line_number);
        }
    } else if (strcmp(key, "taskbar.launcher_count") == 0) {
        if (parse_number(value,
                         0L,
                         (long)AWM_RUNTIME_MAX_LAUNCHERS,
                         &number)) {
            wm.config.launcher_count = (size_t)number;
        } else {
            invalid_value(key, line_number);
        }
    } else if (strcmp(key, "focus_follows_mouse") == 0) {
        if (parse_boolean(value, &boolean)) {
            wm.config.focus_follows_mouse = boolean;
        } else {
            invalid_value(key, line_number);
        }
    } else if (apply_launcher_value(key, value, directory, line_number) ||
               apply_desktop_value(key, value, line_number)) {
        return;
    } else {
        fprintf(stderr,
                "%s: clave desconocida '%s' en la linea %lu\n",
                AWM_NAME,
                key,
                line_number);
    }
}

void runtime_config_load(void)
{
    char path[AWM_RUNTIME_PATH_LENGTH];
    char directory[AWM_RUNTIME_PATH_LENGTH];
    char *line = NULL;
    size_t line_capacity = 0U;
    unsigned long line_number = 0U;
    FILE *file;

    load_defaults();
    if (!build_config_path(path, sizeof(path))) {
        return;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr,
                    "%s: no se pudo leer la configuracion '%s': %s\n",
                    AWM_NAME,
                    path,
                    strerror(errno));
        }
        return;
    }
    config_directory(path, directory, sizeof(directory));

    while (getline(&line, &line_capacity, file) >= 0) {
        char *key;
        char *value;
        char *separator;

        ++line_number;
        key = trim(line);
        if (key[0] == '\0' || key[0] == '#') {
            continue;
        }
        separator = strchr(key, '=');
        if (separator == NULL) {
            fprintf(stderr,
                    "%s: se esperaba 'clave=valor' en la linea %lu\n",
                    AWM_NAME,
                    line_number);
            continue;
        }
        *separator = '\0';
        value = strip_quotes(trim(separator + 1));
        key = trim(key);
        if (key[0] == '\0') {
            invalid_value("(clave vacia)", line_number);
            continue;
        }
        apply_value(key, value, directory, line_number);
    }
    if (ferror(file) != 0) {
        fprintf(stderr,
                "%s: error al leer la configuracion '%s'\n",
                AWM_NAME,
                path);
    }
    free(line);
    (void)fclose(file);
    fprintf(stdout, "%s: configuracion cargada desde %s\n", AWM_NAME, path);
}
