#include "wm.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct {
    char *name;
    char *exec;
    char *icon;
    char *categories;
    char *try_exec;
    char *type;
    bool hidden;
    bool no_display;
    bool terminal;
} RawDesktopEntry;

static ApplicationEntry *entries;
static size_t entry_count;
static size_t entry_capacity;

static char *duplicate_text(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    length = strlen(text);
    copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
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

static bool parse_boolean(const char *value)
{
    return strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0;
}

static char *unescape_value(const char *value)
{
    size_t length = strlen(value);
    char *result = malloc(length + 1U);
    size_t source = 0U;
    size_t destination = 0U;

    if (result == NULL) {
        return NULL;
    }
    while (source < length) {
        if (value[source] == '\\' && source + 1U < length) {
            char escaped = value[++source];

            if (escaped == 'n') {
                result[destination++] = '\n';
            } else if (escaped == 't') {
                result[destination++] = '\t';
            } else if (escaped == 'r') {
                result[destination++] = '\r';
            } else if (escaped == 's') {
                result[destination++] = ' ';
            } else {
                result[destination++] = escaped;
            }
            ++source;
        } else {
            result[destination++] = value[source++];
        }
    }
    result[destination] = '\0';
    return result;
}

static bool replace_text(char **destination, const char *value)
{
    char *copy = unescape_value(value);

    if (copy == NULL) {
        return false;
    }
    free(*destination);
    *destination = copy;
    return true;
}

static void free_raw_entry(RawDesktopEntry *entry)
{
    free(entry->name);
    free(entry->exec);
    free(entry->icon);
    free(entry->categories);
    free(entry->try_exec);
    free(entry->type);
    memset(entry, 0, sizeof(*entry));
}

static bool executable_available(const char *executable)
{
    const char *path;
    const char *start;

    if (executable == NULL || executable[0] == '\0') {
        return true;
    }
    if (strchr(executable, '/') != NULL) {
        return access(executable, X_OK) == 0;
    }

    path = getenv("PATH");
    if (path == NULL || path[0] == '\0') {
        path = "/usr/local/bin:/usr/bin:/bin";
    }
    start = path;
    for (;;) {
        const char *end = strchr(start, ':');
        size_t directory_length = end != NULL ? (size_t)(end - start)
                                              : strlen(start);
        char candidate[AWM_RUNTIME_PATH_LENGTH];
        int written;

        if (directory_length == 0U) {
            written = snprintf(candidate, sizeof(candidate), "./%s", executable);
        } else {
            written = snprintf(candidate,
                               sizeof(candidate),
                               "%.*s/%s",
                               (int)directory_length,
                               start,
                               executable);
        }
        if (written >= 0 && (size_t)written < sizeof(candidate) &&
            access(candidate, X_OK) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        start = end + 1;
    }
    return false;
}

static bool has_category(const char *categories, const char *category)
{
    size_t wanted_length;
    const char *start;

    if (categories == NULL || category == NULL) {
        return false;
    }
    wanted_length = strlen(category);
    start = categories;
    while (*start != '\0') {
        const char *end = strchr(start, ';');
        size_t length = end != NULL ? (size_t)(end - start) : strlen(start);

        if (length == wanted_length && strncmp(start, category, length) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        start = end + 1;
    }
    return false;
}

static ApplicationCategory choose_category(const char *categories)
{
    if (has_category(categories, "Development")) {
        return APPLICATION_CATEGORY_DEVELOPMENT;
    }
    if (has_category(categories, "Graphics")) {
        return APPLICATION_CATEGORY_GRAPHICS;
    }
    if (has_category(categories, "Network") ||
        has_category(categories, "WebBrowser") ||
        has_category(categories, "Email") ||
        has_category(categories, "InstantMessaging")) {
        return APPLICATION_CATEGORY_INTERNET;
    }
    if (has_category(categories, "AudioVideo") ||
        has_category(categories, "Audio") ||
        has_category(categories, "Video")) {
        return APPLICATION_CATEGORY_MULTIMEDIA;
    }
    if (has_category(categories, "Office")) {
        return APPLICATION_CATEGORY_OFFICE;
    }
    if (has_category(categories, "Settings") ||
        has_category(categories, "System")) {
        return APPLICATION_CATEGORY_SYSTEM;
    }
    if (has_category(categories, "Utility") ||
        has_category(categories, "Accessories")) {
        return APPLICATION_CATEGORY_ACCESSORIES;
    }
    return APPLICATION_CATEGORY_OTHER;
}

static bool append_character(char **text,
                             size_t *length,
                             size_t *capacity,
                             char character)
{
    char *resized;
    size_t next_capacity;

    if (*length + 2U <= *capacity) {
        (*text)[(*length)++] = character;
        (*text)[*length] = '\0';
        return true;
    }
    next_capacity = *capacity == 0U ? 64U : *capacity * 2U;
    if (next_capacity < *capacity) {
        return false;
    }
    resized = realloc(*text, next_capacity);
    if (resized == NULL) {
        return false;
    }
    *text = resized;
    *capacity = next_capacity;
    (*text)[(*length)++] = character;
    (*text)[*length] = '\0';
    return true;
}

static bool append_text(char **text,
                        size_t *length,
                        size_t *capacity,
                        const char *value)
{
    while (*value != '\0') {
        if (!append_character(text, length, capacity, *value++)) {
            return false;
        }
    }
    return true;
}

static char *remove_field_codes(const char *token)
{
    char *clean = NULL;
    size_t length = 0U;
    size_t capacity = 0U;
    size_t index;

    for (index = 0U; token[index] != '\0'; ++index) {
        if (token[index] != '%') {
            if (!append_character(&clean, &length, &capacity, token[index])) {
                free(clean);
                return NULL;
            }
            continue;
        }
        if (token[index + 1U] == '%') {
            if (!append_character(&clean, &length, &capacity, '%')) {
                free(clean);
                return NULL;
            }
            ++index;
        } else if (token[index + 1U] != '\0') {
            ++index;
        }
    }
    if (clean == NULL) {
        clean = duplicate_text("");
    }
    return clean;
}

static bool append_shell_quoted(char **command,
                                size_t *length,
                                size_t *capacity,
                                const char *token)
{
    const char *cursor;

    if (!append_character(command, length, capacity, '\'')) {
        return false;
    }
    for (cursor = token; *cursor != '\0'; ++cursor) {
        if (*cursor == '\'') {
            if (!append_text(command, length, capacity, "'\\''")) {
                return false;
            }
        } else if (!append_character(command, length, capacity, *cursor)) {
            return false;
        }
    }
    return append_character(command, length, capacity, '\'');
}

static char *build_exec_command(const char *exec)
{
    const char *cursor = exec;
    char *command = NULL;
    size_t command_length = 0U;
    size_t command_capacity = 0U;
    bool first = true;

    while (*cursor != '\0') {
        char *token = NULL;
        size_t token_length = 0U;
        size_t token_capacity = 0U;
        char quote = '\0';
        char *clean;

        while (isspace((unsigned char)*cursor) != 0) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        while (*cursor != '\0' &&
               (quote != '\0' || isspace((unsigned char)*cursor) == 0)) {
            if (*cursor == '\\' && cursor[1] != '\0') {
                ++cursor;
                if (!append_character(&token,
                                      &token_length,
                                      &token_capacity,
                                      *cursor++)) {
                    free(token);
                    free(command);
                    return NULL;
                }
            } else if (quote == '\0' && (*cursor == '\'' || *cursor == '"')) {
                quote = *cursor++;
            } else if (quote != '\0' && *cursor == quote) {
                quote = '\0';
                ++cursor;
            } else if (!append_character(&token,
                                         &token_length,
                                         &token_capacity,
                                         *cursor++)) {
                free(token);
                free(command);
                return NULL;
            }
        }
        if (token == NULL) {
            token = duplicate_text("");
        }
        clean = token != NULL ? remove_field_codes(token) : NULL;
        free(token);
        if (clean == NULL) {
            free(command);
            return NULL;
        }
        if (clean[0] != '\0') {
            if ((!first && !append_character(&command,
                                             &command_length,
                                             &command_capacity,
                                             ' ')) ||
                !append_shell_quoted(&command,
                                     &command_length,
                                     &command_capacity,
                                     clean)) {
                free(clean);
                free(command);
                return NULL;
            }
            first = false;
        }
        free(clean);
    }
    if (command == NULL) {
        command = duplicate_text("");
    }
    return command;
}

static bool png_name(const char *path)
{
    size_t length = strlen(path);

    return length >= 4U && strcasecmp(path + length - 4U, ".png") == 0;
}

static char *readable_png(const char *path)
{
    return png_name(path) && access(path, R_OK) == 0 ? duplicate_text(path)
                                                     : NULL;
}

static char *icon_from_directory(const char *directory, const char *name)
{
    char path[AWM_RUNTIME_PATH_LENGTH];
    int written;

    if (directory == NULL || directory[0] == '\0') {
        return NULL;
    }
    written = snprintf(path,
                       sizeof(path),
                       png_name(name) ? "%s/%s" : "%s/%s.png",
                       directory,
                       name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return NULL;
    }
    return readable_png(path);
}

static char *resolve_icon_path(const char *icon, const char *desktop_directory)
{
    static const char *const system_directories[] = {
        "/usr/share/pixmaps",
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/hicolor/32x32/apps",
        "/usr/share/icons/hicolor/24x24/apps",
        "/usr/share/icons/hicolor/22x22/apps",
        "/usr/share/icons/hicolor/16x16/apps",
        "/usr/share/icons/Adwaita/48x48/apps",
        "/usr/share/icons/Adwaita/32x32/apps",
        "/usr/share/icons/Adwaita/24x24/apps",
        "/usr/share/icons/Adwaita/22x22/apps",
        "/usr/share/icons/Adwaita/16x16/apps"
    };
    const char *home;
    size_t index;
    char *resolved;

    if (icon == NULL || icon[0] == '\0') {
        return duplicate_text("");
    }
    if (icon[0] == '/') {
        resolved = readable_png(icon);
        return resolved != NULL ? resolved : duplicate_text("");
    }
    if (strchr(icon, '/') != NULL) {
        resolved = icon_from_directory(desktop_directory, icon);
        return resolved != NULL ? resolved : duplicate_text("");
    }

    home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        static const char *const suffixes[] = {
            ".local/share/icons/hicolor/48x48/apps",
            ".local/share/icons/hicolor/32x32/apps",
            ".local/share/icons/hicolor/24x24/apps",
            ".local/share/icons/hicolor/16x16/apps",
            ".local/share/pixmaps",
            ".icons/hicolor/48x48/apps",
            ".icons/hicolor/32x32/apps"
        };

        for (index = 0U; index < sizeof(suffixes) / sizeof(suffixes[0]); ++index) {
            char directory[AWM_RUNTIME_PATH_LENGTH];
            int written = snprintf(directory,
                                   sizeof(directory),
                                   "%s/%s",
                                   home,
                                   suffixes[index]);

            if (written >= 0 && (size_t)written < sizeof(directory)) {
                resolved = icon_from_directory(directory, icon);
                if (resolved != NULL) {
                    return resolved;
                }
            }
        }
    }
    for (index = 0U;
         index < sizeof(system_directories) / sizeof(system_directories[0]);
         ++index) {
        resolved = icon_from_directory(system_directories[index], icon);
        if (resolved != NULL) {
            return resolved;
        }
    }
    return duplicate_text("");
}

static bool set_raw_value(RawDesktopEntry *entry,
                          const char *key,
                          const char *value)
{
    if (strcmp(key, "Name") == 0) {
        return replace_text(&entry->name, value);
    }
    if (strcmp(key, "Exec") == 0) {
        return replace_text(&entry->exec, value);
    }
    if (strcmp(key, "Icon") == 0) {
        return replace_text(&entry->icon, value);
    }
    if (strcmp(key, "Categories") == 0) {
        return replace_text(&entry->categories, value);
    }
    if (strcmp(key, "TryExec") == 0) {
        return replace_text(&entry->try_exec, value);
    }
    if (strcmp(key, "Type") == 0) {
        return replace_text(&entry->type, value);
    }
    if (strcmp(key, "Hidden") == 0) {
        entry->hidden = parse_boolean(value);
    } else if (strcmp(key, "NoDisplay") == 0) {
        entry->no_display = parse_boolean(value);
    } else if (strcmp(key, "Terminal") == 0) {
        entry->terminal = parse_boolean(value);
    }
    return true;
}

static bool parse_desktop_file(const char *path,
                               const char *identifier,
                               const char *directory,
                               ApplicationEntry *result,
                               bool *visible)
{
    FILE *file;
    RawDesktopEntry raw;
    char *line = NULL;
    size_t line_capacity = 0U;
    bool in_desktop_entry = false;
    bool valid = true;

    memset(&raw, 0, sizeof(raw));
    memset(result, 0, sizeof(*result));
    *visible = false;
    file = fopen(path, "r");
    if (file == NULL) {
        return errno == ENOENT;
    }
    while (getline(&line, &line_capacity, file) >= 0) {
        char *key;
        char *value;
        char *separator;

        key = trim(line);
        if (key[0] == '\0' || key[0] == '#') {
            continue;
        }
        if (key[0] == '[') {
            in_desktop_entry = strcmp(key, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_desktop_entry) {
            continue;
        }
        separator = strchr(key, '=');
        if (separator == NULL) {
            continue;
        }
        *separator = '\0';
        value = separator + 1;
        if (!set_raw_value(&raw, trim(key), trim(value))) {
            valid = false;
            break;
        }
    }
    if (ferror(file) != 0) {
        valid = false;
    }
    free(line);
    (void)fclose(file);

    if (valid && raw.type != NULL && strcmp(raw.type, "Application") == 0 &&
        !raw.hidden && !raw.no_display && raw.name != NULL &&
        raw.name[0] != '\0' && raw.exec != NULL && raw.exec[0] != '\0' &&
        executable_available(raw.try_exec)) {
        result->identifier = duplicate_text(identifier);
        result->name = duplicate_text(raw.name);
        result->command = build_exec_command(raw.exec);
        result->icon_path = resolve_icon_path(raw.icon, directory);
        result->category = choose_category(raw.categories);
        result->terminal = raw.terminal;
        if (result->identifier == NULL || result->name == NULL ||
            result->command == NULL || result->command[0] == '\0' ||
            result->icon_path == NULL) {
            free(result->identifier);
            free(result->name);
            free(result->command);
            free(result->icon_path);
            memset(result, 0, sizeof(*result));
            valid = false;
        } else {
            *visible = true;
        }
    }
    free_raw_entry(&raw);
    return valid;
}

static void free_entry(ApplicationEntry *entry)
{
    free(entry->identifier);
    free(entry->name);
    free(entry->command);
    free(entry->icon_path);
    memset(entry, 0, sizeof(*entry));
}

static void remove_identifier(const char *identifier)
{
    size_t index;

    for (index = 0U; index < entry_count; ++index) {
        if (strcmp(entries[index].identifier, identifier) == 0) {
            free_entry(&entries[index]);
            if (index + 1U < entry_count) {
                memmove(&entries[index],
                        &entries[index + 1U],
                        (entry_count - index - 1U) * sizeof(*entries));
            }
            --entry_count;
            return;
        }
    }
}

static bool append_entry(ApplicationEntry *entry)
{
    ApplicationEntry *resized;
    size_t next_capacity;

    if (entry_count == entry_capacity) {
        next_capacity = entry_capacity == 0U ? 32U : entry_capacity * 2U;
        if (next_capacity < entry_capacity) {
            return false;
        }
        resized = realloc(entries, next_capacity * sizeof(*entries));
        if (resized == NULL) {
            return false;
        }
        entries = resized;
        entry_capacity = next_capacity;
    }
    entries[entry_count++] = *entry;
    memset(entry, 0, sizeof(*entry));
    return true;
}

static bool desktop_filename(const char *name)
{
    size_t length = strlen(name);

    return length > 8U && strcmp(name + length - 8U, ".desktop") == 0;
}

static bool load_directory(const char *directory, bool overrides)
{
    DIR *stream;
    struct dirent *item;

    if (directory == NULL || directory[0] == '\0') {
        return true;
    }
    stream = opendir(directory);
    if (stream == NULL) {
        return errno == ENOENT || errno == ENOTDIR;
    }
    while ((item = readdir(stream)) != NULL) {
        char path[AWM_RUNTIME_PATH_LENGTH];
        ApplicationEntry entry;
        bool visible;
        int written;

        if (!desktop_filename(item->d_name)) {
            continue;
        }
        if (overrides) {
            remove_identifier(item->d_name);
        }
        written = snprintf(path, sizeof(path), "%s/%s", directory, item->d_name);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            continue;
        }
        if (!parse_desktop_file(path,
                                item->d_name,
                                directory,
                                &entry,
                                &visible)) {
            (void)closedir(stream);
            return false;
        }
        if (visible && !append_entry(&entry)) {
            free_entry(&entry);
            (void)closedir(stream);
            return false;
        }
    }
    if (closedir(stream) != 0) {
        return false;
    }
    return true;
}

static int compare_entries(const void *left_pointer, const void *right_pointer)
{
    const ApplicationEntry *left = left_pointer;
    const ApplicationEntry *right = right_pointer;

    if (left->category != right->category) {
        return left->category < right->category ? -1 : 1;
    }
    return strcasecmp(left->name, right->name);
}

void desktop_entries_cleanup(void)
{
    size_t index;

    for (index = 0U; index < entry_count; ++index) {
        free_entry(&entries[index]);
    }
    free(entries);
    entries = NULL;
    entry_count = 0U;
    entry_capacity = 0U;
}

bool desktop_entries_load_paths(const char *system_directory,
                                const char *user_directory)
{
    desktop_entries_cleanup();
    if (!load_directory(system_directory, false) ||
        !load_directory(user_directory, true)) {
        desktop_entries_cleanup();
        return false;
    }
    if (entry_count > 1U) {
        qsort(entries, entry_count, sizeof(*entries), compare_entries);
    }
    return true;
}

bool desktop_entries_initialize(void)
{
    const char *system_directory = getenv("ANTONIOWM_SYSTEM_APPLICATIONS_DIR");
    const char *user_directory = getenv("ANTONIOWM_USER_APPLICATIONS_DIR");
    char user_path[AWM_RUNTIME_PATH_LENGTH];

    if (system_directory == NULL || system_directory[0] == '\0') {
        system_directory = "/usr/share/applications";
    }
    if (user_directory == NULL || user_directory[0] == '\0') {
        const char *data_home = getenv("XDG_DATA_HOME");
        const char *home = getenv("HOME");
        int written;

        if (data_home != NULL && data_home[0] != '\0') {
            written = snprintf(user_path,
                               sizeof(user_path),
                               "%s/applications",
                               data_home);
        } else if (home != NULL && home[0] != '\0') {
            written = snprintf(user_path,
                               sizeof(user_path),
                               "%s/.local/share/applications",
                               home);
        } else {
            written = snprintf(user_path, sizeof(user_path), "/nonexistent");
        }
        if (written < 0 || (size_t)written >= sizeof(user_path)) {
            return false;
        }
        user_directory = user_path;
    }
    return desktop_entries_load_paths(system_directory, user_directory);
}

size_t desktop_entries_count(void)
{
    return entry_count;
}

const ApplicationEntry *desktop_entries_get(size_t index)
{
    return index < entry_count ? &entries[index] : NULL;
}

size_t desktop_entries_category_count(ApplicationCategory category)
{
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < entry_count; ++index) {
        if (entries[index].category == category) {
            ++count;
        }
    }
    return count;
}

const ApplicationEntry *desktop_entries_category_get(ApplicationCategory category,
                                                     size_t index)
{
    size_t entry_index;

    for (entry_index = 0U; entry_index < entry_count; ++entry_index) {
        if (entries[entry_index].category == category) {
            if (index == 0U) {
                return &entries[entry_index];
            }
            --index;
        }
    }
    return NULL;
}

const char *desktop_entries_category_name(ApplicationCategory category)
{
    static const char *const names[] = {
        "Accesorios",
        "Desarrollo",
        "Gráficos",
        "Internet",
        "Multimedia",
        "Oficina",
        "Sistema",
        "Otros"
    };

    return category < APPLICATION_CATEGORY_COUNT ? names[category] : "Otros";
}

static bool write_launch_command(char *output,
                                 size_t capacity,
                                 const char *terminal,
                                 const char *separator,
                                 const char *command)
{
    char *quoted = NULL;
    size_t quoted_length = 0U;
    size_t quoted_capacity = 0U;
    int written;

    if (!append_shell_quoted(&quoted,
                             &quoted_length,
                             &quoted_capacity,
                             command)) {
        free(quoted);
        return false;
    }
    written = snprintf(output,
                       capacity,
                       "%s %s /bin/sh -c %s",
                       terminal,
                       separator,
                       quoted);
    free(quoted);
    return written >= 0 && (size_t)written < capacity;
}

bool desktop_entry_launch_command(const ApplicationEntry *entry,
                                  const char *terminal,
                                  char *output,
                                  size_t capacity)
{
    int written;

    if (entry == NULL || entry->command == NULL || output == NULL ||
        capacity == 0U) {
        return false;
    }
    if (!entry->terminal) {
        written = snprintf(output, capacity, "%s", entry->command);
        return written >= 0 && (size_t)written < capacity;
    }
    if (terminal == NULL || terminal[0] == '\0') {
        return false;
    }
    return write_launch_command(output,
                                capacity,
                                terminal,
                                strstr(terminal, "gnome-terminal") != NULL
                                    ? "--"
                                    : "-e",
                                entry->command);
}
