#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_TMP=$(mktemp -d)

cleanup()
{
    rm -rf "$TEST_TMP"
}
trap cleanup EXIT INT TERM

mkdir -p "$TEST_TMP/system" "$TEST_TMP/user" "$TEST_TMP/bin"
printf '#!/bin/sh\nexit 0\n' >"$TEST_TMP/bin/present-tool"
chmod +x "$TEST_TMP/bin/present-tool"
printf '%s' \
    'iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAEklEQVR4nGO4o6HxHx9mGBkKAIMRisGi/0sOAAAAAElFTkSuQmCC' |
    base64 -d >"$TEST_TMP/icon.png"

cat >"$TEST_TMP/system/utility.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Utilidad visible
Exec=present-tool --one %f %F %u %U %i %c %k %%
Icon=$TEST_TMP/icon.png
Categories=Utility;
TryExec=present-tool
EOF
cat >"$TEST_TMP/system/terminal.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Consola de prueba
Exec=present-tool --terminal
Categories=Utility;
Terminal=true
EOF
cat >"$TEST_TMP/system/development.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Desarrollo
Exec=present-tool
Categories=Development;
EOF
cat >"$TEST_TMP/system/graphics.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Graficos
Exec=present-tool
Categories=Graphics;
EOF
cat >"$TEST_TMP/system/internet.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Internet
Exec=present-tool
Categories=Network;
EOF
cat >"$TEST_TMP/system/multimedia.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Multimedia
Exec=present-tool
Categories=AudioVideo;
EOF
cat >"$TEST_TMP/system/office.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Oficina
Exec=present-tool
Categories=Office;
EOF
cat >"$TEST_TMP/system/system.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Sistema
Exec=present-tool
Categories=System;
EOF
cat >"$TEST_TMP/system/other.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Otro
Exec=present-tool
Categories=Education;
EOF
cat >"$TEST_TMP/system/override.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Version del sistema
Exec=system-tool
Categories=Utility;
EOF
cat >"$TEST_TMP/user/override.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Version de usuario
Exec=user-tool
Categories=Utility;
EOF
cat >"$TEST_TMP/system/suppressed.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Debe ocultarse
Exec=present-tool
Categories=Utility;
EOF
cat >"$TEST_TMP/user/suppressed.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Oculta por usuario
Exec=present-tool
Hidden=true
EOF
cat >"$TEST_TMP/system/hidden.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Oculta
Exec=present-tool
Hidden=true
EOF
cat >"$TEST_TMP/system/nodisplay.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=No mostrar
Exec=present-tool
NoDisplay=true
EOF
cat >"$TEST_TMP/system/missing-tryexec.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=TryExec ausente
Exec=present-tool
TryExec=definitely-not-installed-awm-test
EOF

PATH="$TEST_TMP/bin:$PATH" "$PROJECT_DIR/tests/awm-desktop-entries-test" \
    "$TEST_TMP/system" "$TEST_TMP/user" "$TEST_TMP/icon.png"
