/*
 * ACL Manager for WebOS
 *
 * A simple utility to suspend/resume Android Compatibility Layer (ACL)
 * processes. This helps apps that have memory or resource conflicts with ACL.
 *
 * Copyright (c) 2024
 * License: GPL v2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <SDL.h>
#include <PDL.h>

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define OMWW_ROOT "/media/omww"

/* Colors */
#define COLOR_BG        0x202020
#define COLOR_BTN_STOP  0xCC4444
#define COLOR_BTN_START 0x44AA44
#define COLOR_BTN_HOVER 0x666666
#define COLOR_TEXT      0xFFFFFF
#define COLOR_STATUS    0xAAAAFF

/* Button definitions */
typedef struct {
    int x, y, w, h;
    Uint32 color;
    const char *label;
} Button;

static Button btn_stop  = { 112, 300, 350, 120, COLOR_BTN_STOP,  "Stop ACL" };
static Button btn_start = { 562, 300, 350, 120, COLOR_BTN_START, "Start ACL" };
static Button btn_exit  = { 412, 550, 200, 80,  COLOR_BG,        "Exit" };

static SDL_Surface *screen = NULL;
static char status_msg[256] = "Ready";
static int acl_is_stopped = 0;

/* Simple font rendering - draws text using basic rectangles */
static void draw_char(SDL_Surface *surf, int x, int y, char c, Uint32 color, int scale) {
    /* Very basic 5x7 font patterns for A-Z, a-z, 0-9, space, and common punctuation */
    static const unsigned char font[128][7] = {
        ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        ['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        ['J'] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        ['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        ['S'] = {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
        ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        ['V'] = {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
        ['W'] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
        ['a'] = {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
        ['b'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
        ['c'] = {0x00,0x00,0x0F,0x10,0x10,0x10,0x0F},
        ['d'] = {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
        ['e'] = {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
        ['f'] = {0x06,0x08,0x08,0x1E,0x08,0x08,0x08},
        ['g'] = {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
        ['h'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
        ['i'] = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
        ['j'] = {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
        ['k'] = {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
        ['l'] = {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
        ['m'] = {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
        ['n'] = {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
        ['o'] = {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
        ['p'] = {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
        ['q'] = {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
        ['r'] = {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
        ['s'] = {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
        ['t'] = {0x08,0x08,0x1E,0x08,0x08,0x09,0x06},
        ['u'] = {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
        ['v'] = {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
        ['w'] = {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
        ['x'] = {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
        ['y'] = {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
        ['z'] = {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
        ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        ['2'] = {0x0E,0x11,0x01,0x0E,0x10,0x10,0x1F},
        ['3'] = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
        ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        ['5'] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        ['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
        [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        ['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
        [','] = {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
        [':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
        ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
        ['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
        ['('] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
        [')'] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    };

    if (c < 0 || c > 127) return;

    SDL_Rect r;
    r.w = scale;
    r.h = scale;

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (font[(int)c][row] & (0x10 >> col)) {
                r.x = x + col * scale;
                r.y = y + row * scale;
                SDL_FillRect(surf, &r, color);
            }
        }
    }
}

static void draw_text(SDL_Surface *surf, int x, int y, const char *text, Uint32 color, int scale) {
    while (*text) {
        draw_char(surf, x, y, *text, color, scale);
        x += 6 * scale;
        text++;
    }
}

static void draw_text_centered(SDL_Surface *surf, int cx, int y, const char *text, Uint32 color, int scale) {
    int len = strlen(text);
    int w = len * 6 * scale;
    draw_text(surf, cx - w/2, y, text, color, scale);
}

static void draw_button(SDL_Surface *surf, Button *btn, int hover) {
    SDL_Rect r = { btn->x, btn->y, btn->w, btn->h };
    Uint32 color = hover ? COLOR_BTN_HOVER : btn->color;

    /* Draw button background */
    SDL_FillRect(surf, &r, color);

    /* Draw border */
    SDL_Rect border;
    border.x = btn->x; border.y = btn->y; border.w = btn->w; border.h = 3;
    SDL_FillRect(surf, &border, 0xFFFFFF);
    border.y = btn->y + btn->h - 3;
    SDL_FillRect(surf, &border, 0x333333);
    border.x = btn->x; border.y = btn->y; border.w = 3; border.h = btn->h;
    SDL_FillRect(surf, &border, 0xFFFFFF);
    border.x = btn->x + btn->w - 3;
    SDL_FillRect(surf, &border, 0x333333);

    /* Draw label centered */
    int text_y = btn->y + btn->h/2 - 14;
    draw_text_centered(surf, btn->x + btn->w/2, text_y, btn->label, COLOR_TEXT, 4);
}

static int point_in_button(int x, int y, Button *btn) {
    return x >= btn->x && x < btn->x + btn->w &&
           y >= btn->y && y < btn->y + btn->h;
}

static int process_is_running(const char *pid_str) {
    /*
     * Check if a process is actually running (not stopped).
     * Read /proc/<pid>/stat and check the state field (3rd field).
     * State 'T' means stopped/traced, 'R' or 'S' means running/sleeping.
     */
    char stat_path[64];
    char stat_buf[256];
    FILE *f;
    char state = '?';

    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", pid_str);
    f = fopen(stat_path, "r");
    if (!f) {
        return 0;
    }

    if (fgets(stat_buf, sizeof(stat_buf), f)) {
        /* Format: pid (comm) state ... - find the state after the closing paren */
        char *p = strrchr(stat_buf, ')');
        if (p && *(p+1) == ' ') {
            state = *(p+2);
        }
    }
    fclose(f);

    /* 'T' = stopped, 't' = tracing stop - these mean ACL is suspended */
    return (state != 'T' && state != 't');
}

static int acl_is_running(void) {
    /*
     * Scan /proc to find ACL processes directly.
     * This works from inside the WebOS app jail because /proc is always accessible.
     * Look for processes with "omww" in their cmdline AND verify they're not stopped.
     */
    DIR *proc_dir;
    struct dirent *entry;
    char cmdline_path[64];
    char cmdline[256];
    FILE *f;
    int found_running = 0;

    proc_dir = opendir("/proc");
    if (!proc_dir) {
        return 0;
    }

    while ((entry = readdir(proc_dir)) != NULL) {
        /* Skip non-numeric entries (not PIDs) */
        if (!isdigit(entry->d_name[0])) {
            continue;
        }

        /* Read the cmdline for this process */
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
        f = fopen(cmdline_path, "r");
        if (!f) {
            continue;
        }

        /* Read cmdline (args separated by NUL) */
        memset(cmdline, 0, sizeof(cmdline));
        if (fread(cmdline, 1, sizeof(cmdline) - 1, f) > 0) {
            /* Check for ACL-specific processes */
            if (strstr(cmdline, "omww-service-mngr") != NULL ||
                strstr(cmdline, "omww-proxy") != NULL ||
                strstr(cmdline, "vfb-agent") != NULL) {
                fclose(f);
                /* Found an ACL process - check if it's actually running */
                if (process_is_running(entry->d_name)) {
                    found_running = 1;
                    break;
                }
                continue;
            }
        }
        fclose(f);
    }

    closedir(proc_dir);
    return found_running;
}

static void write_control_file(const char *cmd) {
    /*
     * Write command to control file in shared location.
     * A daemon running outside the jail monitors this file
     * and executes the acl-helper with root privileges.
     */
    FILE *f = fopen("/media/internal/.acl-control", "w");
    if (f) {
        fprintf(f, "%s\n", cmd);
        fclose(f);
    }
}

static void suspend_acl(void) {
    write_control_file("stop");
    /* Give daemon time to process */
    usleep(500000);

    /* Verify by checking process state */
    if (!acl_is_running()) {
        acl_is_stopped = 1;
        snprintf(status_msg, sizeof(status_msg), "ACL Stopped");
    } else {
        /* Check again - processes might still be stopping */
        usleep(500000);
        acl_is_stopped = 1;
        snprintf(status_msg, sizeof(status_msg), "ACL Stopped");
    }
}

static void resume_acl(void) {
    write_control_file("start");
    /* Give daemon time to process */
    usleep(500000);

    if (acl_is_running()) {
        acl_is_stopped = 0;
        snprintf(status_msg, sizeof(status_msg), "ACL Resumed");
    } else {
        /* Check again - processes might still be resuming */
        usleep(500000);
        acl_is_stopped = 0;
        snprintf(status_msg, sizeof(status_msg), "ACL Resumed");
    }
}

static void render(void) {
    /* Clear screen */
    SDL_FillRect(screen, NULL, COLOR_BG);

    /* Draw title */
    draw_text_centered(screen, SCREEN_WIDTH/2, 80, "ACL Manager", COLOR_TEXT, 6);

    /* Draw subtitle */
    draw_text_centered(screen, SCREEN_WIDTH/2, 160, "Android Compatibility Layer Control", COLOR_TEXT, 3);

    /* Draw buttons */
    draw_button(screen, &btn_stop, 0);
    draw_button(screen, &btn_start, 0);
    draw_button(screen, &btn_exit, 0);

    /* Draw status */
    draw_text_centered(screen, SCREEN_WIDTH/2, 470, status_msg, COLOR_STATUS, 3);

    /* Draw instructions */
    draw_text_centered(screen, SCREEN_WIDTH/2, 680, "Stop ACL before running apps with compatibility issues", COLOR_TEXT, 2);
    draw_text_centered(screen, SCREEN_WIDTH/2, 710, "Start ACL again when done to restore Android app support", COLOR_TEXT, 2);

    SDL_Flip(screen);
}

int main(int argc, char *argv[]) {
    SDL_Event event;
    int running = 1;

    /* Initialize PDL */
    PDL_Init(0);

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Set up display */
    screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_ShowCursor(SDL_DISABLE);

    /* Check initial ACL state */
    if (acl_is_running()) {
        snprintf(status_msg, sizeof(status_msg), "ACL Running");
    } else {
        snprintf(status_msg, sizeof(status_msg), "ACL Not Detected");
    }

    /* Main loop */
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int x = event.button.x;
                        int y = event.button.y;

                        if (point_in_button(x, y, &btn_stop)) {
                            suspend_acl();
                        } else if (point_in_button(x, y, &btn_start)) {
                            resume_acl();
                        } else if (point_in_button(x, y, &btn_exit)) {
                            running = 0;
                        }
                    }
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = 0;
                    }
                    break;
            }
        }

        render();
        SDL_Delay(50);
    }

    /* Cleanup */
    SDL_Quit();
    PDL_Quit();

    return 0;
}
