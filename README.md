# AntonioWM 0.4.10 — captura PNG al portapapeles

AntonioWM es un gestor de ventanas flotante para X11, escrito desde cero en C
con Xlib. La versión 0.4.10 conserva íntegramente la compatibilidad
ICCCM/EWMH, la interfaz y el soporte multimonitor de 0.4.9, y añade una captura
global, silenciosa y asíncrona del escritorio al portapapeles X11 en formato
PNG.

El proyecto sigue priorizando código directo y comprensible. No usa un toolkit
gráfico, un compositor ni código procedente de otros gestores de ventanas.

## Funciones principales

### Gestión, decoraciones y estados

- Detección clara de otro gestor que ya posea `SubstructureRedirectMask`.
- Gestión de ventanas nuevas y existentes, reparenting, foco, elevación,
  movimiento, resize y cierre mediante `WM_DELETE_WINDOW`.
- Frame clásico compacto con borde, título UTF-8 truncado mediante elipsis,
  botones de minimizar, maximizar/restaurar y cerrar, y colores
  activo/inactivo claramente diferenciados.
- Movimiento por la barra de título o `Alt + botón izquierdo`; resize desde
  zonas invisibles de 7 px en los bordes y esquinas inferiores o con
  `Alt + botón derecho`, sin engrosar visualmente el marco.
- Cuatro escritorios virtuales, memoria de foco por escritorio y envío de
  familias de ventanas entre escritorios.
- Colocación en cascada para ventanas que no indican una posición explícita.
- Maximización limitada al monitor que contiene la mayor parte de la ventana,
  respetando su workarea, ambas barras y restaurando la geometría exacta.
- Fullscreen EWMH limitado al monitor actual, cubriendo sus barras y sin
  frame.
- Minimización ICCCM/EWMH mediante `IconicState` y
  `_NET_WM_STATE_HIDDEN`, con restauración desde la barra.
- Reinicio en caliente sin cerrar aplicaciones ni perder sus estados.
- Barra superior integrada con escritorios, lista de ventanas, urgencia,
  ventana activa y reloj `HH:MM` sin sondeo intensivo.
- Barra inferior integrada con lanzadores configurables —icono PNG o texto— y
  botones para las ventanas del escritorio visible.
- `Ctrl + Mayús + Impr Pant` ejecuta una captura completa y la ofrece como
  `image/png` en `CLIPBOARD`, sin abrir ventanas ni bloquear el bucle X11.

Las ventanas minimizadas se excluyen deliberadamente de `Alt + Tab`: siguen
visibles en las listas de ambas barras, desde donde se restauran
explícitamente.

## Barra inferior 0.4.8

- Ocupa todo el ancho disponible y reserva 28 px por defecto junto con la
  barra superior y los struts de docks externos. Las ventanas maximizadas
  respetan ese espacio; el fullscreen real usa la pantalla completa y queda
  por encima de las barras.
- Incluye de fábrica los lanzadores `Chrome`, `Terminal`, `Archivos` y `GIMP`,
  que ejecutan respectivamente `google-chrome`, `xterm`, `pcmanfm` y `gimp`.
  Se muestran como texto hasta que se configure un icono PNG para cada uno.
- Los iconos se escalan dentro de botones cuadrados conservando la barra
  compacta. Una ruta ausente, un PNG no válido o un visual X11 no compatible
  activa automáticamente la etiqueta de texto; nunca impide arrancar el WM.
- Los lanzadores usan el mismo `fork()` + `exec` no bloqueante del atajo de
  terminal. AntonioWM ignora/recolecta `SIGCHLD`, no espera a las aplicaciones
  y no deja procesos zombie.
- La zona restante muestra sólo clientes `NORMAL`, `DIALOG` y `UTILITY` del
  escritorio actual. Respeta `_NET_WM_STATE_SKIP_TASKBAR`; menús, tooltips,
  docks y demás ventanas especiales no crean botones.
- Una tarea activa, minimizada o urgente tiene un estado visual distinto. Los
  botones usan hover y pulsado, la misma fuente Xft y la misma paleta que la
  barra superior, y truncan títulos largos mediante elipsis.
- Un clic enfoca y eleva una ventana, restaura una minimizada o minimiza la
  tarea que ya estaba activa. Al cambiar de escritorio la lista se reconstruye
  inmediatamente.

`config.h` continúa proporcionando todos los valores de fábrica. La activación,
altura, separación, márgenes, lanzadores, etiquetas, comandos e iconos también
pueden cambiarse sin recompilar mediante el archivo descrito más abajo.

## Pulido visual 0.4.5

- Texto de títulos y barra dibujado con Xft, antialiasing y hinting. Si la
  familia configurada no existe se intenta `sans`; si Xft no puede abrir una
  fuente, queda disponible el respaldo X11 `fixed`.
- Barra de título de 22 px y barra superior de 26 px por defecto, con texto
  centrado verticalmente y márgenes coherentes.
- Tres botones uniformes de 16 × 16 px dibujados sólo con primitivas X11, sin
  imágenes externas. Tienen estados normal, hover y pulsado; la acción se
  ejecuta al soltar el botón dentro del control.
- La barra de título y sus botones usan el cursor estándar `left_ptr`; el
  cursor de resize queda reservado para los bordes y para la operación de
  redimensionado, sin sustituir el cursor definido por las aplicaciones.
- Paleta oscura y poco saturada con colores independientes para bordes,
  títulos, botones, barra, selección, minimización y urgencia.
- Entradas de ventana separadas y con anchura máxima; los títulos demasiado
  largos se acortan con elipsis sin invadir la sección de ventana activa ni el
  reloj alineado a la derecha.

No se ha cambiado la semántica de foco, reparenting, geometría, estados,
escritorios, transients, stacking ni mensajes ICCCM/EWMH de la versión 0.4.0.

## Revisión de calidad 0.4.6

La revisión recorrió todos los módulos, cabeceras, configuración, Makefile,
scripts y documentación. Se reprodujeron y corrigieron estos defectos reales
con cambios localizados:

- la minimización ocultaba el frame, pero no desmapeaba el cliente como exige
  la iconificación ICCCM; ahora un `XMapWindow` posterior genera `MapRequest`
  y restaura la ventana;
- retirar el maximizado mientras una ventana estaba en fullscreen podía dejar
  una geometría maximizada sin los estados EWMH correspondientes;
- se ignoraba `WM_HINTS` con `StateHint = IconicState` en el primer mapeo;
- valores extremos o rejillas incompatibles de `WM_NORMAL_HINTS` podían causar
  overflow y tamaños que no respetaban simultáneamente mínimo, base e
  incrementos; las dimensiones quedan además limitadas al rango del protocolo
  X11 incluyendo las decoraciones;
- cambios dinámicos de `WM_HINTS`, `WM_PROTOCOLS`, `WM_NORMAL_HINTS` o tipo de
  ventana podían dejar foco o geometría internos incoherentes;
- se usaba de forma incompleta el timestamp de `_NET_ACTIVE_WINDOW` y un cambio
  sticky mediante `_NET_WM_DESKTOP` no recalculaba todas las propiedades y
  capas afectadas;
- `SIGINT`/`SIGTERM` no recorrían la limpieza normal y los comandos lanzados
  heredaban una disposición de `SIGCHLD` inadecuada;
- coordenadas extremas de `_NET_MOVERESIZE_WINDOW` y `_NET_WM_MOVERESIZE`
  podían desbordar las conversiones o superar el rango de coordenadas X11.

La terminación normal devuelve cada cliente al root, restaura su geometría,
destruye sus frames y elimina las propiedades EWMH que ya no tienen un gestor
que las mantenga. El reinicio en caliente conserva, en cambio, los estados
necesarios para reconstruir la sesión.

## Compatibilidad ICCCM de FASE 4

- `WM_TRANSIENT_FOR`: el diálogo se asocia con su padre, hereda el escritorio,
  se centra sobre él cuando no tiene posición explícita y permanece por encima.
- `WM_NORMAL_HINTS`: tamaño mínimo y máximo, base, incrementos, relación de
  aspecto, `USPosition`, `PPosition`, `USSize` y `PSize`.
- `WM_HINTS`: `InputHint`, estado inicial y `XUrgencyHint`.
- Modelos de foco pasivo y activo mediante `XSetInputFocus` y
  `WM_TAKE_FOCUS`, sin enfocar automáticamente ventanas auxiliares que no lo
  aceptan.
- Adquisición de la selección ICCCM `WM_Sn` y anuncio `MANAGER` con timestamp
  real del servidor.
- `WM_PROTOCOLS`, `WM_DELETE_WINDOW`, `WM_STATE` y `WM_CHANGE_STATE`.

## Tipos de ventana y política de decoración

Se interpretan `_NET_WM_WINDOW_TYPE_NORMAL`, `DIALOG`, `UTILITY`, `TOOLBAR`,
`MENU`, `DROPDOWN_MENU`, `POPUP_MENU`, `TOOLTIP`, `NOTIFICATION`, `SPLASH`,
`DOCK` y `DESKTOP`.

| Tipo | Decoración predeterminada |
| --- | --- |
| `NORMAL`, `DIALOG`, `UTILITY` | frame, borde y barra de título |
| `TOOLBAR` | sólo borde |
| menús, tooltip, notification, splash, dock y desktop | ninguna |

Las solicitudes `_MOTIF_WM_HINTS` pueden retirar toda la decoración o dejar
sólo el borde. Las ventanas `override_redirect` no se gestionan ni reparentan.

## EWMH de FASE 4

AntonioWM publica `_NET_SUPPORTING_WM_CHECK` con el nombre `AntonioWM` y
mantiene actualizadas estas propiedades del root:

- `_NET_SUPPORTED`;
- `_NET_CLIENT_LIST` y `_NET_CLIENT_LIST_STACKING`;
- `_NET_ACTIVE_WINDOW`;
- `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP` y
  `_NET_DESKTOP_NAMES`;
- `_NET_DESKTOP_GEOMETRY`, `_NET_DESKTOP_VIEWPORT` y `_NET_WORKAREA`.

Además de fullscreen, maximización y minimización, implementa estados
`ABOVE`, `BELOW`, `STICKY`, `SKIP_TASKBAR`, `SKIP_PAGER`,
`DEMANDS_ATTENTION` y `MODAL`. El stacking mantiene las capas desktop, below,
normal, above, diálogos/transients, elementos especiales y fullscreen.

Se atienden `_NET_CLOSE_WINDOW`, `_NET_MOVERESIZE_WINDOW`,
`_NET_WM_MOVERESIZE`, `_NET_RESTACK_WINDOW`, `_NET_CURRENT_DESKTOP`,
`_NET_WM_DESKTOP`, `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE` y
`_NET_REQUEST_FRAME_EXTENTS`. `_NET_WM_STRUT` y `_NET_WM_STRUT_PARTIAL`
permiten que docks externos reserven pantalla; la barra propia se desplaza y
el área maximizada combina ambos espacios reservados.

## Compatibilidad GIMP 3 y RandR

Xephyr puede diferir la materialización de sus monitores RandR hasta la primera
consulta RandR 1.5. Si una aplicación GTK es el primer cliente que la realiza,
las notificaciones resultantes pueden invalidar el `GdkMonitor` obtenido
durante su inicio. AntonioWM consulta una vez la topología RandR antes de
gestionar aplicaciones. Si RandR 1.5 no está disponible, continúa normalmente.

Esta corrección se validó con GIMP 3.0.4 y su splash normal: abrió la interfaz,
mantuvo un monitor GDK válido, fue reparentado, recibió foco y cerró con
`Alt + F4`, sin los errores `GDK_IS_MONITOR`, resolución `0.0`,
`GDK_IS_DISPLAY` ni violación de segmento.

## Multimonitor y resize 0.4.9

- La topología se obtiene primero mediante RandR 1.5 y, si no está disponible,
  mediante Xinerama. Como último fallback se conserva una sola pantalla raíz.
- El monitor de una ventana se elige por la mayor superficie de intersección;
  si no existe intersección se usa el que contiene su centro y finalmente el
  monitor más cercano.
- El workarea se calcula por monitor, combinando `_NET_WM_STRUT_PARTIAL`,
  `_NET_WM_STRUT` y las barras superior e inferior de AntonioWM que realmente
  intersectan ese monitor.
- Maximizar y fullscreen permanecen dentro del monitor seleccionado. Mover una
  ventana a otro monitor antes de maximizar cambia correctamente el destino;
  restaurar recupera exactamente posición y tamaño anteriores.
- AntonioWM escucha cambios RandR de pantalla, CRTC, salidas y recursos para
  volver a aplicar la geometría de ventanas maximizadas o fullscreen.
- Cinco ventanas `InputOnly` transparentes proporcionan agarres cómodos en
  borde izquierdo, derecho, inferior y ambas esquinas inferiores. Cada zona
  muestra sólo su cursor direccional; el borde visible continúa midiendo 2 px
  y el área cliente restante conserva el cursor de la aplicación.

## Captura al portapapeles 0.4.10

El atajo global `Ctrl + Mayús + Impr Pant` ejecuta de fábrica:

```sh
maim | xclip -selection clipboard -t image/png -i
```

El comando se lanza con `/bin/sh -c` en un proceso hijo desacoplado: AntonioWM
no espera su terminación y continúa atendiendo eventos. La disposición de
`SIGCHLD` evita zombis, mientras que `xclip` puede permanecer vivo el tiempo
necesario como propietario de la selección `CLIPBOARD`. Una captura posterior
reemplaza de forma normal al propietario anterior.

El grab incluye las variantes de CapsLock y NumLock usadas por los demás
atajos globales. Si falta `maim`, `xclip` o ambos, sólo falla ese comando; el
gestor sigue funcionando. No se crea ningún selector, diálogo, terminal,
notificación ni ventana gestionada.

## Dependencias en Debian 13

```sh
sudo apt install build-essential libx11-dev libxft-dev libxinerama-dev \
    libxrandr-dev zlib1g-dev pkg-config xserver-xephyr xterm x11-utils \
    x11-xserver-utils xdotool
```

La captura al portapapeles tiene dos dependencias opcionales adicionales:

```sh
sudo apt install maim xclip
```

Sólo son necesarias para `Ctrl + Mayús + Impr Pant`; AntonioWM puede arrancar
y utilizarse normalmente sin ellas.

El binario enlaza con Xlib, XRandR, Xinerama, Xft y zlib; esta última se usa
únicamente para descomprimir PNG. Xinerama proporciona el fallback de
topología cuando RandR 1.5 no está disponible. `xdotool`, `x11-utils`,
`x11-xserver-utils` —que contiene `xrandr`—, `xterm` y Xephyr sólo son
necesarios para pruebas. Si
`pkg-config` no está disponible, el Makefile incluye un fallback adecuado para
las rutas estándar de Debian 13.

## Compilación

Desde la carpeta `AntonioWM`:

```sh
make
```

La versión puede consultarse sin conectarse al servidor X:

```sh
./antoniowm --version
./antoniowm -v
```

Ambas órdenes imprimen `AntonioWM 0.4.10-screenshot` y terminan correctamente
aunque no exista `DISPLAY` o ya haya otro gestor controlando la pantalla.

Objetivos disponibles:

```sh
make check       # recompila con todos los avisos tratados como errores
make debug       # símbolos de depuración y -O0
make test-client # clientes X11 usados por las pruebas
make smoke       # regresiones FASES 1–4, barras y multimonitor en Xephyr
make multimonitor-smoke # sólo geometría RandR y resize por frame
make screenshot-smoke  # sólo captura, CLIPBOARD, locks y procesos hijos
make audit-smoke # carga, cierres concurrentes y limpieza/reinicio del WM
make clean
```

## Prueba manual segura con Xephyr

```sh
Xephyr :1 -screen 1280x720 &
cd AntonioWM
DISPLAY=:1 ./antoniowm
```

Desde otra terminal:

```sh
DISPLAY=:1 xterm &
DISPLAY=:1 xfce4-terminal &
DISPLAY=:1 thunar &
DISPLAY=:1 mousepad &
DISPLAY=:1 firefox &
DISPLAY=:1 gimp &
```

Todo proceso con `DISPLAY=:1` queda dentro del servidor anidado y no interfiere
con el gestor del escritorio anfitrión.

## Controles

| Acción | Teclado o ratón |
| --- | --- |
| Siguiente ventana no minimizada | `Alt + Tab` |
| Ventana anterior no minimizada | `Alt + Mayús + Tab` |
| Cerrar ventana activa | `Alt + F4` |
| Abrir terminal | `Super + Enter` |
| Capturar escritorio a `CLIPBOARD` | `Ctrl + Mayús + Impr Pant` |
| Maximizar/restaurar | `Super + M` o botón de maximizar |
| Activar/desactivar fullscreen | `Super + F` |
| Cambiar al escritorio 1–4 | `Super + 1` … `Super + 4` |
| Enviar ventana al escritorio 1–4 | `Super + Mayús + 1` … `Super + Mayús + 4` |
| Reiniciar AntonioWM | `Super + Mayús + R` |
| Enfocar y elevar | clic en cliente, título o entrada de la barra |
| Mover | arrastrar el título o `Alt + botón izquierdo` |
| Redimensionar | arrastrar un borde o `Alt + botón derecho` |
| Minimizar | botón de minimizar |
| Restaurar minimizada | clic en su entrada de la barra |
| Cambiar escritorio | clic en 1–4 en la barra |
| Lanzar aplicación | clic en Chrome, Terminal, Archivos o GIMP abajo |
| Cambiar a una tarea | clic en su botón de la barra inferior |
| Minimizar tarea activa | segundo clic en su botón de la barra inferior |

`AWM_TERMINAL` en `config.h` define la terminal de fábrica. La variable de
entorno conserva precedencia sobre el archivo de usuario:

```sh
ANTONIOWM_TERMINAL='xfce4-terminal' DISPLAY=:1 ./antoniowm
```

## Configuración de usuario

Al arrancar, AntonioWM busca primero la ruta indicada por
`ANTONIOWM_CONFIG`. Si no existe esa variable, usa
`$XDG_CONFIG_HOME/antoniowm/config` o, como fallback,
`~/.config/antoniowm/config`. Que el archivo no exista es normal: en ese caso
se usan íntegramente los valores de `config.h`.

El formato es deliberadamente sencillo: una asignación `clave=valor` por
línea, espacios exteriores ignorados y líneas de comentario iniciadas por
`#`. Se aceptan valores entre comillas simples o dobles, pero no se realiza
expansión de shell. Los comandos pueden contener espacios y se ejecutan con el
mismo mecanismo no bloqueante que los lanzadores anteriores. Una clave o un
valor incorrectos producen un aviso y AntonioWM conserva el valor válido
anterior.

Ejemplo completo:

```ini
# ~/.config/antoniowm/config
terminal=xfce4-terminal
screenshot.command=maim | xclip -selection clipboard -t image/png -i
focus_follows_mouse=false

taskbar.enabled=true
taskbar.height=30
taskbar.gap=4
taskbar.padding=4
taskbar.launcher_count=4

launcher.1.label=Chrome
launcher.1.command=google-chrome
launcher.1.icon=/usr/share/icons/hicolor/32x32/apps/google-chrome.png

launcher.2.label=Terminal
launcher.2.command=xfce4-terminal
launcher.2.icon=icons/terminal.png

launcher.3.label=Archivos
launcher.3.command=pcmanfm
launcher.3.icon=/usr/share/icons/hicolor/32x32/apps/pcmanfm.png

launcher.4.label=GIMP
launcher.4.command=gimp
launcher.4.icon=~/Iconos/gimp.png

desktop.1=Trabajo
desktop.2=Web
desktop.3=Multimedia
desktop.4=Otros
```

El mismo ejemplo listo para copiar se incluye en `examples/config`.

Las rutas de icono absolutas se usan tal cual; `~/` se expande con `HOME` y
las rutas relativas se resuelven respecto al directorio del archivo de
configuración. Se admiten PNG no entrelazados habituales —paleta, escala de
grises, RGB y RGBA— de hasta 2048 × 2048, aunque conviene usar imágenes
pequeñas. Si el icono falta o no puede decodificarse, el botón muestra
`launcher.N.label`.

Los booleanos aceptan `true/false`, `yes/no`, `on/off` y `1/0`. Se permiten
como máximo 16 lanzadores. `taskbar.height` está acotado entre 0 y 256 px, y
gap/padding entre 0 y 64 px; una altura cero equivale a no crear la barra. Los
nombres también pueden escribirse como `desktop.N.name`. `Super + Mayús + R`
reinicia AntonioWM y vuelve a leer el archivo sin cerrar las aplicaciones.
`screenshot.command` puede contener una tubería u otro comando shell; si se
omite, se utiliza el comando `maim | xclip ...` mostrado anteriormente.

`config.h` sigue siendo la fuente de todos los valores de fábrica y contiene
además colores, tipografía, dimensiones de decoraciones y atajos, que en esta
versión aún requieren recompilar.

## Pruebas automáticas

`make smoke` ejecuta cinco suites dentro de Xephyr. La primera cubre las
regresiones de FASES 1–3: reparenting, decoraciones, cascada, foco, teclado,
ratón, resize, maximización/restauración, fullscreen, minimización,
escritorios, barra, cierre y reinicio. La segunda (`smoke_phase4_xephyr.sh`)
añade:

- transients modales, centrado, escritorio y stacking familiar;
- todos los tipos de ventana y la política de decoraciones/Motif;
- foco `InputHint`/`WM_TAKE_FOCUS` y urgencia sin robo de foco;
- mínimos, máximos, base, incrementos y aspecto;
- valores extremos de size hints sin overflow del protocolo;
- estados above, below, sticky, skip, hidden y modal;
- `_NET_MOVERESIZE_WINDOW`, movimiento interactivo y restacking;
- struts completos/parciales, barra propia y `_NET_WORKAREA` estable;
- persistencia de relaciones y estados tras reiniciar el WM.

La tercera (`smoke_taskbar_xephyr.sh`) comprueba:

- carga de un icono PNG real y fallback de texto para una ruta inexistente;
- lectura de terminal, altura, padding, separación, comandos, etiquetas,
  nombres de escritorios y política de foco desde un archivo externo;
- geometría de la barra inferior y reserva combinada en `_NET_WORKAREA`;
- ejecución de los cuatro comandos configurados y del atajo de terminal, sin
  procesos zombie;
- foco, elevación, minimización y restauración mediante los botones de tarea;
- filtrado de `SKIP_TASKBAR` y separación por escritorios;
- maximizado entre las dos barras y fullscreen sobre toda la pantalla;
- reconstrucción y recarga tras reiniciar, y desactivación completa de la
  taskbar sin reservar workarea.

La cuarta (`smoke_multimonitor_resize_xephyr.sh`) crea dos monitores lógicos
RandR en un Xephyr de 1200 × 700 y comprueba:

- maximizado independiente primero en el monitor izquierdo y después en el
  derecho, reservando 26 px arriba y 28 px abajo;
- restauración exacta antes y después de mover la misma ventana entre
  monitores;
- fullscreen únicamente en el monitor actual, cubriendo ambas barras;
- selección por mayor intersección cuando una ventana cruza la frontera;
- agarres invisibles de 7 px, crecimiento/reducción por el borde derecho,
  ambas esquinas inferiores y ventanas próximas al extremo de cada monitor;
- incrementos ICCCM, `Alt + botón derecho` y restauración exacta tras resize.

La quinta (`smoke_screenshot_xephyr.sh`) comprueba:

- ejecución del comando predeterminado y de un override
  `screenshot.command` leído del archivo de usuario;
- publicación de los bytes PNG en la selección `CLIPBOARD`, anuncio del target
  `image/png` y lectura posterior por otro cliente X11;
- activación con CapsLock y NumLock, varias capturas consecutivas y reemplazo
  limpio del propietario anterior;
- ausencia de ventanas visibles, bloqueo o procesos zombie;
- continuidad del WM y de sus atajos cuando `maim` y `xclip` no existen.

La suite usa sustitutos instrumentados de `maim`/`xclip`, porque esas dos
herramientas opcionales no están instaladas en el entorno de pruebas. El
sustituto de `xclip` implementa el protocolo de selección X11 y permanece vivo
como propietario, por lo que la prueba valida la transferencia `image/png`, no
sólo la creación de un proceso.

`audit_stress_xephyr.sh` añade 24 clientes concurrentes, destrucción durante
movimiento y resize, coordenadas EWMH extremas, iconificación y restauración,
estados, escritorios, reinicio bajo carga y terminación limpia con `SIGTERM`.
Puede ejecutarse con `make audit-smoke`.

Las pantallas predeterminadas son `:98`, `:97`, `:95`, `:93` y `:92`. Pueden
cambiarse así:

```sh
ANTONIOWM_TEST_DISPLAY=:77 ANTONIOWM_PHASE4_TEST_DISPLAY=:76 \
ANTONIOWM_TASKBAR_TEST_DISPLAY=:75 \
ANTONIOWM_MULTIMONITOR_TEST_DISPLAY=:74 \
ANTONIOWM_SCREENSHOT_TEST_DISPLAY=:73 make smoke
```

En un contenedor donde xterm no pueda crear un pseudo-terminal, la primera
suite puede ejecutar el resto de regresiones con
`ANTONIOWM_SKIP_XTERM=1`; este selector no se usa en una validación normal de
Debian.

### Sanitizadores

Ejemplo con AddressSanitizer:

```sh
make clean
make all test-client \
    CFLAGS='-O1 -g3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow \
    -Wformat=2 -Wundef -Wpointer-arith -Wstrict-prototypes \
    -Wmissing-prototypes -Werror -fsanitize=address \
    -fno-omit-frame-pointer' \
    LDFLAGS='-fsanitize=address'
ASAN_OPTIONS='detect_leaks=0:halt_on_error=1:abort_on_error=1' make smoke
```

Para UndefinedBehaviorSanitizer, sustituye `address` por `undefined` y usa
`UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'`. La revisión 0.4.6 superó
por separado las regresiones y la prueba de estrés con ASan y UBSan. En el
contenedor Work, LeakSanitizer no pudo inicializarse por la instrumentación del
propio entorno; ASan se ejecutó allí con `detect_leaks=0`.

## Aplicaciones reales validadas

| Aplicación | Resultado comprobado |
| --- | --- |
| xterm | frame, terminal por atajo, foco y cierre |
| xfce4-terminal 1.1.3 | frame, foco, maximizar/restaurar, minimizar/restaurar y cierre |
| Thunar 4.18.8 | frame, foco, maximizar/restaurar, minimizar/restaurar y cierre |
| Mousepad 0.6.1 (GTK) | frame, foco, estados y cierre |
| Firefox ESR 140.14.0 | reparenting, foco, maximización, minimización, fullscreen y cierre |
| QTerminal 1.4.0 (Qt) | ventana normal, frame, foco y menús emergentes |
| GIMP 3.0.4 (GTK) | splash, interfaz estable, monitor, geometría, foco y cierre |

Para 0.4.5 se repitieron las dos suites Xephyr afectadas y pruebas reales con
xfce4-terminal, Thunar, Mousepad y GIMP 3.0.4. GIMP volvió a abrir su interfaz
con un monitor válido y sin la cascada GDK. El runtime de Work no permitió
relanzar xterm (restricción de propietario/grupo del PTY al ejecutarlo como
root) ni Firefox (sus tres binarios terminaron con `SIGBUS` incluso usando
`--version`, antes de conectarse a X11). Ambos quedan como comprobación manual
final en Debian 13; ya habían superado la validación completa de 0.4.0 y las
rutas de reparenting/estado que usan están cubiertas por las regresiones
instrumentadas de 0.4.5.

El contenedor de validación tampoco proporcionó una aplicación GTK4
independiente. No permitió automatizar de extremo a extremo el diálogo
Preferencias de QTerminal; ambos puntos siguen siendo comprobaciones manuales
recomendadas. Los diálogos y transients GTK/Qt se cubren mediante las
aplicaciones disponibles y clientes X11 instrumentados.

Durante la revisión 0.4.6 se volvieron a ejecutar xfce4-terminal 1.1.3,
Thunar 4.18.8, Mousepad 0.6.1 y GIMP 3.0.4. Las cuatro aplicaciones superaron
las comprobaciones aplicables; GIMP abrió su interfaz con monitor GDK válido y
sin la cascada de errores de resolución. xterm continuó bloqueado por la
restricción de pseudo-terminales al ejecutar como root dentro de Work, por lo
que debe repetirse en Debian real.

Para 0.4.7 se repitieron las regresiones de geometría, estados, struts,
stacking y reinicio, además de la suite específica de la barra inferior. El
contenedor Work no contiene Google Chrome ni PCManFM, y su restricción de PTY
impide iniciar xterm; por ello los cuatro nombres de comando se sustituyeron
temporalmente por clientes X11 instrumentados para verificar cada clic,
`fork`/`exec`, gestión de ventanas y ausencia de zombies. Las aplicaciones
reales continúan como comprobación manual en Debian 13; GIMP 3.0.4 ya superó
la validación real completa de la versión anterior.

Para 0.4.8 se repitieron las regresiones de FASES 1–3, la suite ICCCM/EWMH de
FASE 4 y la prueba específica de taskbar. Esta última usó configuración
externa, un PNG RGBA real, un icono deliberadamente ausente, cuatro comandos
instrumentados y el atajo de terminal. También verificó los nombres EWMH,
foco por ratón, maximizado, fullscreen, escritorios, reinicio y taskbar
desactivada. El intento de xterm real volvió a fallar antes de crear una
ventana por la restricción de PTY de Work; no se modificó AntonioWM para
acomodar esa limitación del contenedor.

Para 0.4.9 se ejecutaron la compilación estricta con `-Werror`, la nueva suite
RandR de dos monitores, la regresión de FASES 1–3 que cubre decoraciones,
maximización, fullscreen y resize, y la suite FASE 4 que cubre struts y size
hints. No se repitieron las pruebas de taskbar/iconos ni aplicaciones reales
porque estos cambios no modifican esas rutas.

Para 0.4.10 se ejecutaron la compilación estricta, la suite específica de
captura, la regresión de configuración/taskbar y la prueba moderna de resize y
multimonitor. La prueba de captura realizó varias adquisiciones —también con
CapsLock y NumLock—, leyó después el PNG desde `CLIPBOARD`, verificó el
comando runtime, el reemplazo de propietarios y la ausencia de zombis. El
contenedor no incluye los binarios reales `maim` y `xclip`, por lo que el
pegado final con ambos paquetes debe confirmarse en Debian; su protocolo X11
se comprobó con el cliente instrumentado descrito arriba.

## Configuración

Los valores de fábrica están en `config.h`: terminal; captura; barras; lanzadores;
título; borde; botones; márgenes; fuente; paleta; reloj; escritorios; cascada;
foco; tamaños interactivos; modificadores y atajos. El archivo de usuario
sobrescribe el subconjunto documentado sin compilar. Después de cambiar el
resto en `config.h`, ejecuta de nuevo `make`.

## Estructura

```text
AntonioWM/
├── include/wm.h       Estructuras compartidas y API interna
├── src/main.c         Conexión X11, RandR, bucle de eventos y reinicio
├── src/monitor.c      Topología RandR/Xinerama y selección de monitor
├── src/events.c       Despacho central de eventos X11
├── src/client.c       Clientes, foco, geometría, estados y cierre
├── src/keyboard.c     Capturas de teclado y lanzamiento de comandos
├── src/mouse.c        Foco, movimiento y resize interactivo
├── src/desktop.c      Escritorios, visibilidad y cascada
├── src/ewmh.c         Átomos, propiedades y mensajes EWMH
├── src/hints.c        Hints ICCCM, tipos EWMH y decoraciones Motif
├── src/stacking.c     Capas, transients y orden de apilamiento
├── src/draw.c         Texto Xft/fallback X11 y primitivas de dibujo
├── src/decor.c        Frames, títulos, botones y geometría decorada
├── src/bar.c          Barra interactiva, lista, urgencia y reloj
├── src/taskbar.c      Barra inferior, iconos, lanzadores y tareas
├── src/runtime_config.c Parser acotado del archivo de usuario
├── src/icon.c         Decodificación PNG y dibujo XImage
├── examples/config    Configuración externa de ejemplo
├── tests/             Clientes instrumentados y suites Xephyr
├── config.h           Configuración recompilable
├── Makefile
└── README.md
```

## Límites deliberados de la versión 0.4.10

- Fullscreen ocupa por defecto un único monitor. Aún no se implementa la
  solicitud opcional `_NET_WM_FULLSCREEN_MONITORS` para abarcar varios
  monitores de forma explícita.
- No hay compositor, transparencias, sombras complejas, animaciones, búsqueda
  automática en temas de iconos ni motor de temas. Los iconos de ventanas en
  la taskbar siguen siendo opcionales y esta versión conserva allí el título.
- La compatibilidad ICCCM/EWMH es deliberadamente razonable, no una afirmación
  de cobertura total de todas las extensiones posibles.
- No se instala todavía como sesión de un display manager. La instalación en
  `/usr/local` y la integración de sesión continúan reservadas para la FASE 5.
