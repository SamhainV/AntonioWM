CC ?= cc

CPPFLAGS ?=
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude -I.

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
          -Wundef -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes

LDFLAGS ?=
LDLIBS += -lX11 -lXrandr

TARGET := antoniowm
SOURCES := src/main.c src/events.c src/client.c src/keyboard.c src/mouse.c \
           src/desktop.c src/ewmh.c src/hints.c src/stacking.c src/draw.c \
           src/decor.c src/bar.c
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
DEPENDENCIES := $(OBJECTS:.o=.d)
TEST_CLIENT := tests/awm-test-client
TEST_EWMH := tests/awm-ewmh-request

.PHONY: all clean debug check test-client smoke

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

test-client: $(TEST_CLIENT) $(TEST_EWMH)

$(TEST_CLIENT): tests/test_client.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(TEST_EWMH): tests/ewmh_request.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

smoke: all test-client
	./tests/smoke_xephyr.sh
	./tests/smoke_phase4_xephyr.sh

clean:
	rm -rf build $(TARGET) $(TEST_CLIENT) $(TEST_EWMH)

-include $(DEPENDENCIES)
