CC ?= cc

CPPFLAGS ?=
XFT_CFLAGS ?= $(shell pkg-config --cflags xft 2>/dev/null || \
	printf '%s' '-I/usr/include/freetype2')
XFT_LIBS ?= $(shell pkg-config --libs xft 2>/dev/null || printf '%s' '-lXft')
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude -I. $(XFT_CFLAGS)

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
          -Wundef -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes

LDFLAGS ?=
LDLIBS += -lX11 -lXrandr -lXinerama $(XFT_LIBS) -lz

TARGET := antoniowm
SOURCES := src/main.c src/events.c src/client.c src/keyboard.c src/mouse.c \
           src/desktop.c src/ewmh.c src/hints.c src/stacking.c src/draw.c \
           src/decor.c src/clock.c src/taskbar.c src/menu.c \
           src/desktop_entries.c src/runtime_config.c src/icon.c src/monitor.c
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
DEPENDENCIES := $(OBJECTS:.o=.d)
TEST_CLIENT := tests/awm-test-client
TEST_EWMH := tests/awm-ewmh-request
TEST_PIXEL := tests/awm-pixel-probe
TEST_CLIPBOARD := tests/awm-clipboard-helper
TEST_DESKTOP_ENTRIES := tests/awm-desktop-entries-test

.PHONY: all clean debug check test-client smoke audit-smoke multimonitor-smoke \
	screenshot-smoke desktop-entries-smoke panel-menu-smoke

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

build/%.o: src/%.c include/wm.h config.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O0 -g3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes" all

check:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Werror" all

test-client: $(TEST_CLIENT) $(TEST_EWMH) $(TEST_PIXEL) $(TEST_CLIPBOARD) \
	$(TEST_DESKTOP_ENTRIES)

$(TEST_CLIENT): tests/test_client.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(TEST_EWMH): tests/ewmh_request.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(TEST_PIXEL): tests/pixel_probe.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(TEST_CLIPBOARD): tests/clipboard_helper.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< -lX11

$(TEST_DESKTOP_ENTRIES): tests/desktop_entries_test.c src/desktop_entries.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

smoke: all test-client
	./tests/test_desktop_entries.sh
	./tests/smoke_xephyr.sh
	./tests/smoke_phase4_xephyr.sh
	./tests/smoke_taskbar_xephyr.sh
	./tests/smoke_multimonitor_resize_xephyr.sh
	./tests/smoke_screenshot_xephyr.sh
	./tests/smoke_panel_menu_xephyr.sh

desktop-entries-smoke: $(TEST_DESKTOP_ENTRIES)
	./tests/test_desktop_entries.sh

panel-menu-smoke: all test-client
	./tests/smoke_panel_menu_xephyr.sh

multimonitor-smoke: all test-client
	./tests/smoke_multimonitor_resize_xephyr.sh

screenshot-smoke: all test-client
	./tests/smoke_screenshot_xephyr.sh

audit-smoke: all test-client
	./tests/audit_stress_xephyr.sh

clean:
	rm -rf build $(TARGET) $(TEST_CLIENT) $(TEST_EWMH) $(TEST_PIXEL) \
		$(TEST_CLIPBOARD) $(TEST_DESKTOP_ENTRIES)

-include $(DEPENDENCIES)
