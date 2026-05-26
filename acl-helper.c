/*
 * acl-helper - Setuid root helper for ACL process management
 *
 * Usage: acl-helper stop|start
 *
 * "stop": Properly stops ACL via upstart so WebOS won't auto-restart it.
 *         Falls back to session-based SIGSTOP if no upstart job is found.
 * "start": Resumes ACL via upstart start, or SIGCONT fallback.
 *
 * Why upstart instead of SIGSTOP?
 *   SIGSTOP pauses processes but leaves them "running" in upstart's view.
 *   The Luna service bus monitor (or an ACL internal watchdog) detects that
 *   ACL services stop responding and triggers a restart.  Using upstart's
 *   `stop` command marks the job as intentionally stopped, which prevents
 *   any respawn logic from firing.
 *
 * Compile (setuid root required on device):
 *   arm-linux-gnueabi-gcc -Wall -O2 -std=gnu99 -o acl-helper acl-helper.c
 *   chmod 4755 /usr/bin/acl-helper
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Config                                                               */
/* ------------------------------------------------------------------ */

/* State file in shared storage: tells the app we intentionally stopped ACL */
#define STATE_FILE "/media/internal/.acl-stopped"

/* PID file written by ACL's Android init layer */
#define ANDROID_INIT_PID_FILE "/media/omww/android/tmp/init_pid.txt"

/* Process names used to identify the ACL upstart job */
static const char *ACL_PROCS[] = {
    "omww-service-mngr",
    "omww-proxy",
    "vfb-agent",
    NULL
};

/* ------------------------------------------------------------------ */
/* Upstart helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * Scan /etc/event.d/ for a job file that mentions one of the ACL processes.
 * The filename IS the upstart job name (e.g. "acl" → /sbin/stop acl).
 * Returns 1 and fills job_name on success, 0 on failure.
 */
static int find_acl_job(char *job_name, size_t job_len)
{
    DIR *dir = opendir("/etc/event.d");
    if (!dir) {
        perror("opendir /etc/event.d");
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[512];
        snprintf(path, sizeof(path), "/etc/event.d/%s", entry->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char line[512];
        int found = 0;
        while (fgets(line, sizeof(line), f)) {
            for (int i = 0; ACL_PROCS[i]; i++) {
                if (strstr(line, ACL_PROCS[i])) {
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        fclose(f);

        if (found) {
            snprintf(job_name, job_len, "%s", entry->d_name);
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);
    return 0;
}

/*
 * Execute /sbin/stop <job> or /sbin/start <job>.
 * Returns 0 on success, non-zero on failure.
 */
static int run_upstart(const char *cmd, const char *job)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* Child: /sbin/stop and /sbin/start are the canonical upstart shortcuts */
        char prog[64];
        snprintf(prog, sizeof(prog), "/sbin/%s", cmd);
        execl(prog, cmd, job, NULL);

        /* Fallback: full initctl path */
        execl("/sbin/initctl", "initctl", cmd, job, NULL);

        fprintf(stderr, "acl-helper: could not exec upstart command\n");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Signal-based fallback (session kill, like run-pcsx.sh)              */
/* ------------------------------------------------------------------ */

/*
 * Send sig to every process in the same session as `leader_pid`.
 * Uses pkill -STOP/-CONT -s <sid> if available, otherwise signals
 * individually from /proc.
 */
static void signal_session(pid_t leader_pid, int sig)
{
    /* Try pkill first (handles session kill atomically) */
    char sid_str[32];
    snprintf(sid_str, sizeof(sid_str), "%d", (int)leader_pid);

    pid_t pid = fork();
    if (pid == 0) {
        char sigarg[32];
        snprintf(sigarg, sizeof(sigarg), "-%d", sig);
        execl("/usr/bin/pkill", "pkill", sigarg, "-s", sid_str, NULL);
        execl("/bin/pkill",     "pkill", sigarg, "-s", sid_str, NULL);
        _exit(1);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            return; /* pkill succeeded */
    }

    /* Fallback: iterate /proc and signal matching PIDs */
    DIR *proc = opendir("/proc");
    if (!proc) return;

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!isdigit(entry->d_name[0])) continue;

        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", entry->d_name);

        FILE *f = fopen(stat_path, "r");
        if (!f) continue;

        int pid_val; char comm[256]; char state;
        int ppid, pgrp, session;
        int matched = (fscanf(f, "%d %255s %c %d %d %d",
                              &pid_val, comm, &state,
                              &ppid, &pgrp, &session) == 6);
        fclose(f);

        if (matched && session == (int)leader_pid)
            kill((pid_t)pid_val, sig);
    }
    closedir(proc);
}

/*
 * Signal-based stop/start: mimics the approach in run-pcsx.sh.
 * Signals all ACL sessions (Android init session + service manager session).
 */
static void signal_acl(int sig)
{
    /* 1. Android init session (most reliable source of the root PID) */
    FILE *f = fopen(ANDROID_INIT_PID_FILE, "r");
    if (f) {
        pid_t init_pid = 0;
        fscanf(f, "%d", &init_pid);
        fclose(f);
        if (init_pid > 0) {
            fprintf(stderr, "acl-helper: signaling Android init session %d\n", (int)init_pid);
            signal_session(init_pid, sig);
        }
    }

    /* 2. ACL service manager session (may differ from Android init session) */
    DIR *proc = opendir("/proc");
    if (!proc) return;

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!isdigit(entry->d_name[0])) continue;

        char cmdline_path[64];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);

        FILE *cf = fopen(cmdline_path, "r");
        if (!cf) continue;

        char cmdline[256];
        memset(cmdline, 0, sizeof(cmdline));
        fread(cmdline, 1, sizeof(cmdline) - 1, cf);
        fclose(cf);

        if (strstr(cmdline, "omww-service-mngr") == NULL) continue;

        /* Found the service manager — get its session */
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", entry->d_name);
        FILE *sf = fopen(stat_path, "r");
        if (!sf) continue;

        int pid_val; char comm[256]; char state;
        int ppid, pgrp, session;
        if (fscanf(sf, "%d %255s %c %d %d %d",
                   &pid_val, comm, &state,
                   &ppid, &pgrp, &session) == 6) {
            fprintf(stderr, "acl-helper: signaling service manager session %d\n", session);
            signal_session((pid_t)session, sig);
        }
        fclose(sf);
        break; /* only need the first match */
    }
    closedir(proc);

    /* 3. vfb-agent / vfb-client might be in their own sessions */
    DIR *proc2 = opendir("/proc");
    if (!proc2) return;
    while ((entry = readdir(proc2)) != NULL) {
        if (!isdigit(entry->d_name[0])) continue;
        char cmdline_path[64];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
        FILE *cf = fopen(cmdline_path, "r");
        if (!cf) continue;
        char cmdline[256];
        memset(cmdline, 0, sizeof(cmdline));
        fread(cmdline, 1, sizeof(cmdline) - 1, cf);
        fclose(cf);
        if (strstr(cmdline, "vfb-agent") || strstr(cmdline, "vfb-client"))
            kill((pid_t)atoi(entry->d_name), sig);
    }
    closedir(proc2);
}

/* ------------------------------------------------------------------ */
/* State file                                                           */
/* ------------------------------------------------------------------ */

static void mark_stopped(void) {
    FILE *f = fopen(STATE_FILE, "w");
    if (f) { fprintf(f, "stopped\n"); fclose(f); }
}

static void mark_running(void) {
    unlink(STATE_FILE);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: acl-helper stop|start\n");
        return 1;
    }

    const char *cmd = argv[1];
    int do_stop  = (strcmp(cmd, "stop")  == 0);
    int do_start = (strcmp(cmd, "start") == 0);

    if (!do_stop && !do_start) {
        fprintf(stderr, "acl-helper: unknown command '%s'\n", cmd);
        return 1;
    }

    /* --- Try upstart first ----------------------------------------- */
    char job[256] = {0};
    int upstart_ok = 0;
    if (find_acl_job(job, sizeof(job))) {
        fprintf(stderr, "acl-helper: found ACL upstart job '%s'\n", job);
        int ret = run_upstart(do_stop ? "stop" : "start", job);
        if (ret == 0) {
            fprintf(stderr, "acl-helper: upstart %s %s OK\n", cmd, job);
            upstart_ok = 1;
        } else {
            fprintf(stderr, "acl-helper: upstart %s %s failed (exit %d)\n", cmd, job, ret);
        }
    } else {
        fprintf(stderr, "acl-helper: no ACL upstart job found\n");
    }

    /*
     * Always follow up with signals, even after a successful upstart stop.
     *
     * upstart only kills the main tracked process (omww-service-mngr).
     * Its children (omww-proxy, vfb-agent, Android init, etc.) are in
     * separate process groups and become orphans -- they keep running
     * until explicitly signalled.
     *
     * For "start": only send SIGCONT if upstart didn't handle it (upstart
     * start re-launches the service manager which spawns fresh children;
     * sending SIGCONT to stale PIDs from a previous run is harmless but
     * noisy, so skip it when upstart succeeded).
     */
    if (do_stop) {
        fprintf(stderr, "acl-helper: sending SIGSTOP to remaining ACL processes\n");
        signal_acl(SIGSTOP);
    } else if (!upstart_ok) {
        fprintf(stderr, "acl-helper: sending SIGCONT (upstart unavailable)\n");
        signal_acl(SIGCONT);
    }

    if (do_stop) mark_stopped(); else mark_running();
    return 0;
}
