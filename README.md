# AntonioWM 0.4.0 — FASE 4

AntonioWM es un gestor de ventanas flotante para X11, escrito desde cero en C
con Xlib. La versión 0.4.0 conserva el núcleo, los escritorios y estados de las
FASES 1 y 2, y las decoraciones, minimización y barra de la FASE 3. Esta fase
mejora la compatibilidad ICCCM/EWMH, los diálogos y transients, los tipos de
ventana, el modelo de foco, los size hints, el stacking y los struts.

El proyecto sigue priorizando código directo y comprensible. No usa un toolkit
gráfico, un compositor ni código procedente de otros gestores de ventanas.

## Funciones principales

### Gestión, decoraciones y estados

- Detección clara de otro gestor que ya posea `SubstructureRedirectMask`.
- Gestión de ventanas nuevas y existentes, reparenting, foco, elevación,
  movimiento, resize y cierre mediante `WM_DELETE_WINDOW`.
- Frame clásico con borde, título UTF-8, botones de minimizar,
  maximizar/restaurar y cerrar, y colores activo/inactivo.
- Movimiento por la barra de título o `Alt + botón izquierdo`; resize desde
  los bordes o con `Alt + botón derecho`.
- Cuatro escritorios virtuales, memoria de foco por escritorio y envío de
  familias de ventanas entre escritorios.
- Colocación en cascada para ventanas que no indican una posición explícita.
- Maximización que respeta el área de trabajo y restauración exacta.
- Fullscreen EWMH sobre toda la pantalla, por encima de la barra y sin frame.
- Minimización ICCCM/EWMH mediante `IconicState` y
  `_NET_WM_STATE_HIDDEN`, con restauración desde la barra.
- Reinicio en caliente sin cerrar aplicaciones ni perder sus estados.
- Barra superior integrada con escritorios, lista de ventanas, urgencia,
  ventana activa y reloj `HH:MM` sin sondeo intensivo.

Las ventanas minimizadas se excluyen deliberadamente de `Alt + Tab`: siguen
visibles en la lista de la barra, desde donde se restauran explícitamente.

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

## Dependencias en Debian 13

```sh
sudo apt install build-essential libx11-dev libxft-dev libxinerama-dev \
    libxrandr-dev xserver-xephyr xterm x11-utils xdotool
```

El binario enlaza con Xlib y XRandR. Xft y Xinerama siguen siendo dependencias
preparatorias para fuentes y distribución multimonitor posteriores. `xdotool`,
`x11-utils`, `xterm` y Xephyr sólo son necesarios para pruebas.

## Compilación

Desde la carpeta `AntonioWM`:

```sh
make
```

Objetivos disponibles:

```sh
make check       # recompila con todos los avisos tratados como errores
make debug       # símbolos de depuración y -O0
make test-client # clientes X11 usados por las pruebas
make smoke       # regresiones funcionales FASES 1–4 dentro de Xephyr
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

`AWM_TERMINAL` en `config.h` define la terminal. También puede sobrescribirse
sin recompilar:

```sh
ANTONIOWM_TERMINAL='xfce4-terminal' DISPLAY=:1 ./antoniowm
```

## Pruebas automáticas

`make smoke` ejecuta dos suites dentro de Xephyr. La primera cubre las
regresiones de FASES 1–3: reparenting, decoraciones, cascada, foco, teclado,
ratón, resize, maximización/restauración, fullscreen, minimización,
escritorios, barra, cierre y reinicio. La segunda (`smoke_phase4_xephyr.sh`)
añade:

- transients modales, centrado, escritorio y stacking familiar;
- todos los tipos de ventana y la política de decoraciones/Motif;
- foco `InputHint`/`WM_TAKE_FOCUS` y urgencia sin robo de foco;
- mínimos, máximos, base, incrementos y aspecto;
- estados above, below, sticky, skip, hidden y modal;
- `_NET_MOVERESIZE_WINDOW`, movimiento interactivo y restacking;
- struts completos/parciales, barra propia y `_NET_WORKAREA` estable;
- persistencia de relaciones y estados tras reiniciar el WM.

La pantalla predeterminada es `:98`. Puede cambiarse así:

```sh
ANTONIOWM_TEST_DISPLAY=:77 make smoke
```

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
`UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'`. La entrega 0.4.0 superó
las regresiones con ambos sanitizadores por separado.

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

El contenedor de validación no proporcionó una aplicación GTK4 independiente.
Tampoco permitió automatizar de extremo a extremo el diálogo Preferencias de
QTerminal; ambos puntos quedan como comprobaciones manuales recomendadas en el
Debian 13 de destino. Los diálogos y transients GTK/Qt se cubrieron mediante
las aplicaciones disponibles y clientes X11 instrumentados.

## Configuración

Los parámetros principales están en `config.h`: terminal; colores y medidas
de bordes, títulos y barra; fuente; formato del reloj; nombres y número de
escritorios; cascada; política de foco; tamaños interactivos; modificadores y
atajos. Después de cambiarlo, ejecuta de nuevo `make`.

## Estructura

```text
AntonioWM/
├── include/wm.h       Estructuras compartidas y API interna
├── src/main.c         Conexión X11, RandR, bucle de eventos y reinicio
├── src/events.c       Despacho central de eventos X11
├── src/client.c       Clientes, foco, geometría, estados y cierre
├── src/keyboard.c     Capturas de teclado y lanzamiento de comandos
├── src/mouse.c        Foco, movimiento y resize interactivo
├── src/desktop.c      Escritorios, visibilidad y cascada
├── src/ewmh.c         Átomos, propiedades y mensajes EWMH
├── src/hints.c        Hints ICCCM, tipos EWMH y decoraciones Motif
├── src/stacking.c     Capas, transients y orden de apilamiento
├── src/draw.c         Primitivas Xlib de texto y dibujo
├── src/decor.c        Frames, títulos, botones y geometría decorada
├── src/bar.c          Barra interactiva, lista, urgencia y reloj
├── tests/             Clientes instrumentados y suites Xephyr
├── config.h           Configuración recompilable
├── Makefile
└── README.md
```

## Límites deliberados de la FASE 4

- La topología RandR se consulta, pero AntonioWM todavía trata la pantalla X11
  como un único espacio de trabajo; la distribución multimonitor queda para una
  fase posterior.
- No hay compositor, transparencias ni motor complejo de temas.
- La compatibilidad ICCCM/EWMH es deliberadamente razonable, no una afirmación
  de cobertura total de todas las extensiones posibles.
- No se instala todavía como sesión de un display manager. La instalación en
  `/usr/local` y la integración de sesión continúan reservadas para la FASE 5.
