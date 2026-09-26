#include "wm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message)
{
    fprintf(stderr, "desktop_entries_test: %s\n", message);
    desktop_entries_cleanup();
    exit(EXIT_FAILURE);
}

static const ApplicationEntry *find_identifier(const char *identifier)
{
    size_t index;

    for (index = 0U; index < desktop_entries_count(); ++index) {
        const ApplicationEntry *entry = desktop_entries_get(index);

        if (entry != NULL && strcmp(entry->identifier, identifier) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void require_absent(const char *identifier)
{
    if (find_identifier(identifier) != NULL) {
        fail(identifier);
    }
}

int main(int argc, char **argv)
{
    const ApplicationEntry *utility;
    const ApplicationEntry *terminal;
    const ApplicationEntry *override;
    char command[AWM_DESKTOP_ENTRY_COMMAND_LENGTH];

    if (argc != 4) {
        fprintf(stderr,
                "uso: %s SYSTEM_DIR USER_DIR EXPECTED_ICON\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    if (!desktop_entries_load_paths(argv[1], argv[2])) {
        fail("no se pudieron cargar los directorios de prueba");
    }
    if (desktop_entries_count() != 10U) {
        fail("el numero de aplicaciones visibles no es 10");
    }
    require_absent("hidden.desktop");
    require_absent("nodisplay.desktop");
    require_absent("missing-tryexec.desktop");
    require_absent("suppressed.desktop");

    if (desktop_entries_category_count(APPLICATION_CATEGORY_ACCESSORIES) != 3U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_DEVELOPMENT) != 1U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_GRAPHICS) != 1U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_INTERNET) != 1U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_MULTIMEDIA) != 1U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_OFFICE) != 1U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_SYSTEM) != 1U ||
        desktop_entries_category_count(APPLICATION_CATEGORY_OTHER) != 1U) {
        fail("la clasificacion por categorias es incorrecta");
    }

    utility = find_identifier("utility.desktop");
    if (utility == NULL || strcmp(utility->name, "Utilidad visible") != 0 ||
        strcmp(utility->icon_path, argv[3]) != 0) {
        fail("Name/Icon de la utilidad no se analizaron correctamente");
    }
    if (strstr(utility->command, "%f") != NULL ||
        strstr(utility->command, "%F") != NULL ||
        strstr(utility->command, "%u") != NULL ||
        strstr(utility->command, "%U") != NULL ||
        strstr(utility->command, "%i") != NULL ||
        strstr(utility->command, "%c") != NULL ||
        strstr(utility->command, "%k") != NULL ||
        strstr(utility->command, "'%'") == NULL) {
        fail("los codigos de campo Exec no se limpiaron correctamente");
    }

    terminal = find_identifier("terminal.desktop");
    if (terminal == NULL || !terminal->terminal ||
        !desktop_entry_launch_command(terminal,
                                      "xterm",
                                      command,
                                      sizeof(command)) ||
        strstr(command, "xterm -e /bin/sh -c") == NULL) {
        fail("Terminal=true no genero un comando para xterm");
    }
    if (!desktop_entry_launch_command(terminal,
                                      "gnome-terminal",
                                      command,
                                      sizeof(command)) ||
        strstr(command, "gnome-terminal -- /bin/sh -c") == NULL) {
        fail("Terminal=true no genero un comando para gnome-terminal");
    }

    override = find_identifier("override.desktop");
    if (override == NULL || strcmp(override->name, "Version de usuario") != 0 ||
        strstr(override->command, "user-tool") == NULL) {
        fail("la entrada de usuario no sustituyo a la entrada del sistema");
    }

    desktop_entries_cleanup();
    puts("Parser .desktop superado: filtros, override, categorias, Exec, icono y Terminal=true.");
    return EXIT_SUCCESS;
}
