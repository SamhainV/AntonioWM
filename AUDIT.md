# Auditoría y validación X11 de AntonioWM

Revisión del 13 de noviembre de 2026, iniciada sobre `v0.4.14`. Este informe
publicable resume los cambios y las pruebas realmente realizadas. Las capturas,
logs, hashes de configuración personal, rutas de usuario y copias de instalación
se conservan únicamente como evidencias locales excluidas de Git.

## Hallazgos y correcciones

| Gravedad | Ubicación | Reproducción / evidencia | Corrección |
|---|---|---|---|
| Media, funcional | `src/taskbar.c`, `layout_clients` | Abrir una ventana: el botón mostraba solo el título | Icono a la izquierda y texto truncado dentro del mismo botón |
| Media, funcional | `src/desktop.c`, `desktop_initialize`; `src/runtime_config.c`, `apply_value` | `desktop.count=6` era desconocida y se mantenía el número compilado | Recuento configurable entre 1 y 16 |
| Media, recuperación | `src/ewmh.c`, `ewmh_initialize`, `ewmh_get_window_desktop` | Reducir escritorios y reiniciar con ventanas abiertas | Escritorio actual y clientes fuera de rango pasan al último válido |
| Media, XID obsoleto | `src/tray.c`, `tray_handle_event` | Matar applets deja PropertyNotify/UnmapNotify antes de DestroyNotify; se observaron BadWindow, petición 20 | Consumir destrucción pendiente antes de consultar `_XEMBED_INFO` |
| Baja, observabilidad | `src/main.c`, `runtime_xerror` | Se silenciaban errores tolerados por el manejador | `ANTONIOWM_LOG_XERRORS=1` permite registrarlos |
| Baja, memoria al salir | `src/main.c`, `main` | LSan detectó 8.839 bytes en 128 asignaciones de Fontconfig | `FcFini()` después del cierre de Xft y X11; repetición sin informe |
| Pruebas | `smoke_task_preview_xephyr.sh`, `hover_task` | `mousemove --sync` podía bloquear al repetir destino | Movimiento sin esa espera; se sigue comprobando el estado de preview |
| Pruebas | `smoke_systray_xephyr.sh`, `start_wm` y conflicto de selección | EWMH se publica antes de terminar panel/tray | Esperas acotadas para panel y diagnóstico |
| Pruebas | `smoke_autostart_xephyr.sh`, `wait_for_count` | Contador vacío cuando el archivo aún no existe | Interpretarlo como cero |

Se revisaron gestión y destrucción de clientes, reparenting, foco, stacking,
eventos, preview, escritorios, EWMH, monitor, configuración y rutas de limpieza.
La compilación inicial estricta y el parser `.desktop` pasaron antes de los
cambios. La revisión no demuestra ausencia de errores en todos los caminos.

## Política de iconos

1. Se prioriza `_NET_WM_ICON`, CARDINAL/32. La propiedad está limitada a
   1.048.576 unidades; cada dimensión debe estar entre 1 y 2048. Se comprueba
   el número de píxeles antes de multiplicar o leer. Xlib representa esos
   elementos mediante long nativo también en LP64.
2. Se elige una resolución suficiente, o una pequeña disponible si ninguna
   alcanza el tamaño solicitado. Se convierte ARGB a RGBA, conserva la
   proporción y mezcla alfa con el fondo del botón.
3. Fallback `WM_HINTS`: `IconPixmapHint` monocromo o TrueColor de profundidad
   predeterminada, y máscara monocroma opcional que cubra la imagen. Se limitan
   dimensiones y memoria. Un manejador temporal captura errores de pixmaps
   obsoletos durante la lectura síncrona, tras drenar los errores anteriores.
4. Se dibuja un símbolo genérico si no hay imagen válida o la visual no
   permite pintarla. No se deduce la aplicación a partir del título.

La copia RGBA pertenece al cliente. Se invalida al cambiar o borrar
`_NET_WM_ICON` y al cambiar `WM_HINTS`; se libera al retirar o destruir el
cliente, fallar la decoración y cerrar/reiniciar. Los slots de clic y la
preview conservan su identidad. No se retienen pixmaps prestados del cliente.

## Escritorios y configuración

```ini
desktop.count=6
desktop.1.name=Trabajo
desktop.2.name=Web
desktop.6.name=Correo
```

- Rango 1–16. El valor predeterminado sigue siendo **cuatro**, con etiquetas
  1, 2, 3 y 4. También se acepta `desktop.1=Trabajo`.
- Las etiquetas omitidas son numéricas; cada etiqueta admite hasta 63 bytes.
  Los valores inválidos de count conservan el valor previo de esa carga.
- La carga respeta `ANTONIOWM_CONFIG`, después
  `$XDG_CONFIG_HOME/antoniowm/config`, después `~/.config/antoniowm/config`.
- `Super+Shift+r` reinicia internamente y recarga. No hay recarga por SIGHUP.
  Al reducir el número se ajustan clientes y escritorio actual al último
  válido. Se conserva sticky y se redimensionan las propiedades EWMH.
- Los atajos numéricos cubren hasta 9; el panel permite los restantes sujeto
  al espacio disponible. Las etiquetas largas se truncan.
- El README incluye un ejemplo personal opcional con cinco launchers y
  `desktop.count=1`. No cambia los valores predeterminados ni instala el
  fichero personal. `examples/config` conserva cuatro escritorios.

## Entorno y aislamiento

La conexión al X11 anfitrión estaba restringida dentro del sandbox; la consulta
con el mismo DISPLAY y autorización funcionó fuera de él. No había Xvfb, pero
sí Xvnc/TightVNC y Xephyr. Se usó Xvnc como anfitrión de servidores Xephyr;
los WMs de regresión se ejecutaron exclusivamente en pantallas anidadas.

Xephyr sobre esa versión de TightVNC fallaba en AllocARGBCursor. Se resolvió
con `-sw-cursor -no-host-grab`. Los servidores de prueba ofrecían Composite,
Render, XTest, RandR y XRes. xdotool y libXRes se extrajeron en un directorio
temporal sin instalar paquetes. Las ejecuciones fallidas por cursor, locks o
interferencia de recompilación no se cuentan como superadas. El runner final
usa una copia privada para que un reinicio no pierda su ejecutable.

## Pruebas ejecutadas

| Prueba | Resultado |
|---|---|
| `make check` inicial y final | Pasa con advertencias estrictas y `-Werror` |
| `make config-icons-test` | Pasa: 1/4/6 escritorios, etiquetas, entradas inválidas, ARGB/alfa, tamaños y datos truncados; propiedad simulada |
| Fases 1–3 y fase 4 | Pasan: foco, Alt+Tab, ICCCM, transients/tipos, decoraciones, size hints, EWMH, terminal y reinicio |
| Taskbar | Pasa: PNG/fallback, launchers, clics, foco, minimizar/restaurar y desactivación |
| Panel y menú | Pasa: ratón/teclado, Escape, clic exterior, catálogo .desktop, acciones y reloj/layout |
| Tray | Pasa: XEmbed, clic/rueda, popup, SIGKILL, altas/bajas, reinicios y conflicto de selección |
| Autostart | Pasa: comandos independientes, fallos tolerados, reinicios sin duplicados ni zombies |
| RandR y dos monitores | Pasa con monitores lógicos: maximizado/fullscreen, restauración y resize de bordes/esquinas |
| Captura | Pasa: PNG, locks, configuración runtime y procesos hijos |
| Preview | Pasa: retardo, contenido visible/Iconic, foco, invalidación, cierre, multimonitor, reinicio, desactivación y sin Composite |
| Stress | Pasa: 24 clientes, cierres durante move/resize, estados, escritorios, reinicio y SIGTERM |
| Integración Xlib/XTest nueva | Pasa: píxeles ARGB/alfa, iconos 512×512, propiedades malformadas/de tipo incorrecto, cambios y borrado, WM_HINTS/máscara/pixmap obsoleto, fallback, clic/restauración, hover rápido y cierres pendiente/visible |
| Escritorios con WM real | Pasan 1/4/6/16, etiquetas EWMH, entradas inválidas, nueve reinicios y reducción 16→4 con ventanas abiertas |
| XRes | 118 recursos / 0 bytes de pixmaps atribuidos al WM antes y después de 40 ciclos tras cinco de calentamiento |
| Aplicaciones reales | Chrome con perfil temporal/página local, xterm y xcalc: minimizar/restaurar, Alt+Tab, maximizar y reiniciar |
| Profundidades | 16 bits TrueColor: dibujo y píxel comprobado; 8 bits PseudoColor: genérico; WM_HINTS monocromo en ambas |

En la integración se comprueba que no queda una preview visible huérfana.
Las capturas inspeccionadas muestran iconos antes del título, elipsis sin
invadir otros botones, contenido de previews, selectores para 6/16 escritorios,
aplicaciones reales y dos monitores lógicos. Los registros examinados de
integración, aplicaciones y profundidades no contienen errores X11 con el
diagnóstico habilitado; esto no descarta cualquier carrera futura.

Comandos reproducibles desde la raíz del repositorio:

```sh
make check
make config-icons-test
# Con DISPLAY anfitrión y dependencias de pruebas disponibles:
./tests/run_isolated_regressions.sh
./tests/run_x11_integration.sh
./tests/run_icon_depths.sh
./tests/run_real_apps.sh
```

La integración requiere libXtst y libXRes, además de las herramientas de los
scripts. Admite `WM_BINARY`, `EVIDENCE_DIR`, `TEST_SANITIZERS` y
`AWM_XRES_LIBRARY`. Las regresiones admiten `WM_BINARY` y `TEST_SCREEN_BASE`.
Las evidencias se generan en `tests/evidence/`, excluido de Git: no se publican
capturas del escritorio, logs de aplicaciones, cookies ni copias instaladas.

## ASan, UBSan y LeakSanitizer

El WM completo se compiló con `-fsanitize=address,undefined`,
`-fno-omit-frame-pointer`, depuración y advertencias estrictas con `-Werror`.

**Detección de fugas desactivada:** suite completa, stress, integración y
unitario a `-O0` pasan con `ASAN_OPTIONS=detect_leaks=0`. Estas ejecuciones no
son comprobaciones de fugas. La espera de autostart produjo avisos de contador
vacío antes de su corrección; la repetición corregida también pasa.

**LeakSanitizer activado:** la primera integración con WM real señaló
8.839 bytes / 128 asignaciones de Fontconfig al salir. Tras añadir `FcFini()`
al cierre normal, la misma integración, nueve reinicios y la repetición con
iconos grandes/malformados pasan sin informes ASan/UBSan/LSan ni supresiones.
No se desactivó detect_leaks en esas ejecuciones. La limitación de LSan bajo
ptrace observada dentro del sandbox no se extrapola a estas pruebas fuera de él.

LSan comprueba la terminación final, no la imagen descartada por exec. XRes
aporta una medición independiente de estabilidad de los recursos vivos.

## Instalación local y validación posterior

Después de aceptar las pruebas aisladas, el usuario canceló el plan de VT3 y
hotplug físico y autorizó actualizar su instalación. Se guardó el ejecutable
anterior, se reemplazó únicamente el binario instalado de forma atómica y se
reinició internamente sin cerrar Xorg ni el gestor de sesiones. El hash del
proceso activo coincidió con el nuevo binario y la lista de clientes se mantuvo.
La configuración personal no se modificó, comprobado mediante hash antes y
después; no se publican su contenido, su hash ni los datos del escritorio.

El cliente `tests/installed_smoke.c` comprobó una ventana propia: icono verde
renderizado, contenido real de previews normal y minimizada y restauración por
clic. Pasó; se retiró su ventana y se restauraron foco/puntero. Se inspeccionó
el icono de Chrome junto a su título y los cuatro escritorios predeterminados
vigentes durante esa comprobación. No fue necesario rollback. Las copias y
las instrucciones locales de restauración se mantienen fuera del commit.

`installed_smoke.c` opera sobre una sesión habitual y no pertenece al runner
aislado: solo debe ejecutarse con autorización explícita. Los scripts de
sesión física quedaron preparados, pero no se ejecutaron y el usuario canceló
ese plan. Su inclusión como herramientas manuales no significa validación física.

## Límites y publicación

- Dos monitores lógicos y presencia de salidas reales comprobados; hotplug
  físico no ejecutado. El usuario ha considerado suficiente Xephyr/Xvnc.
- PseudoColor y pixmaps de color cuya profundidad no corresponde a la visual
  predeterminada usan fallback. No se adivina un colormap desconocido.
- Cambiar píxeles de WM_HINTS sin PropertyNotify no invalida la caché.
- No hay paginación del panel: muchos escritorios/launchers en una pantalla
  estrecha pueden dejar slots fuera del espacio disponible.
- No se han recorrido todas las aplicaciones, fallos de asignación ni carreras
  con clientes hostiles. La suite completa ASan/UBSan se ejecutó con LSan
  desactivado; la comprobación de fugas fue la integración instrumentada.
- El commit reúne código, correcciones, fuentes de pruebas, README,
  `examples/config` y este informe. Se excluyen binarios, backups, temporales,
  configuración personal, secretos y logs/capturas locales. No se cambia el
  predeterminado de cuatro escritorios ni se modifica `v0.4.14` o crea otro tag.
