#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <time.h>

static char current_text[64];
static time_t current_minute = (time_t)-1;

static void update_text(void)
{
    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) == NULL ||
        strftime(current_text,
                 sizeof(current_text),
                 AWM_CLOCK_FORMAT,
                 &local_time) == 0U) {
        (void)snprintf(current_text, sizeof(current_text), "--:--");
    }
    current_minute = now / 60;
}

void clock_initialize(void)
{
    update_text();
}

const char *clock_text(void)
{
    if (current_minute == (time_t)-1) {
        update_text();
    }
    return current_text;
}

int clock_next_timeout_ms(void)
{
    time_t now = time(NULL);
    int seconds = 60 - (int)(now % 60);

    if (seconds < 1 || seconds > 60) {
        seconds = 60;
    }
    return seconds * 1000 + 50;
}

void clock_tick(void)
{
    time_t now = time(NULL);

    if (now / 60 != current_minute) {
        update_text();
        taskbar_update();
    }
}
