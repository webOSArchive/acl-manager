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

/*
 * Strings used to identify the ACL upstart job by scanning /etc/event.d/.
 * The job files themselves reference shell scripts, not process names —
 * so we search for strings that appear in those script paths.
 *
 * On observed devices:
 *   start-acl-services  exec /media/omww/bin/webos-services.sh
 *   start-acl           exec /media/omww/bin/start-acl.sh
 */
/*
 * These strings appear in the exec lines of the ACL upstart jobs:
 *   start-acl-services  →  /media/omww/bin/webos-services.sh
 *   start-acl           →  /media/omww/bin/start-acl.sh
 *
 * We intentionally do NOT use a broad match like "/media/omww" because
 * other jobs (e.g. start-acl-icon) also reference that path and would
 * be selected instead of start-acl-services.
 */
static const char *ACL_UPSTART_STRINGS[] = {
    "webos-services",    /* uniquely identifies start-acl-services */
    "start-acl.sh",      /* identifies start-acl (fallback) */
    NULL
};

/*
 * Cmdline prefixes/substrings used to identify running ACL processes in /proc.
 *
 * "omww-" catches all omww-service-mngr, omww-proxy, omww-powerd,
 * omww-sensord, etc.  We use a prefix so we don't have to enumerate
 * every sub-service — new ones are caught automatically.
 *
 * We intentionally avoid matching "/media/omww" because that path appears
 * in webos-services.sh's argv[0] and we don't want to kill that launcher.
 */
static const char *ACL_PROCS[] = {
    "omww-",       /* all omww-* daemons */
    "vfb-agent",
    "vfb-client",
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
            for (int i = 0; ACL_UPSTART_STRINGS[i]; i++) {
                if (strstr(line, ACL_UPSTART_STRINGS[i])) {
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
/* Process kill by name                                                 */
/* ------------------------------------------------------------------ */

/*
 * Send `sig` to every process whose cmdline matches one of ACL_PROCS.
 *
 * Session-based killing (pkill -s, kill-by-session) was unreliable:
 * the ACL launcher (webos-services.sh) is a compiled binary that forks
 * services into its own session.  That session ID changes on every restart
 * and is not recorded in any predictable file.  Direct name-based killing
 * is simpler and works regardless of how ACL was launched.
 */
static void kill_acl_by_name(int sig)
{
    DIR *proc = opendir("/proc");
    if (!proc) {
        perror("acl-helper: opendir /proc");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!isdigit(entry->d_name[0])) continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char cmdline[256] = {0};
        fread(cmdline, 1, sizeof(cmdline) - 1, f);
        fclose(f);

        for (int i = 0; ACL_PROCS[i]; i++) {
            if (strstr(cmdline, ACL_PROCS[i])) {
                pid_t target = (pid_t)atoi(entry->d_name);
                fprintf(stderr, "acl-helper: kill(%d) pid %d [%s]\n",
                        sig, (int)target, ACL_PROCS[i]);
                kill(target, sig);
                break;
            }
        }
    }
    closedir(proc);
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
    if (find_acl_job(job, sizeof(job))) {
        fprintf(stderr, "acl-helper: found ACL upstart job '%s'\n", job);
        int ret = run_upstart(do_stop ? "stop" : "start", job);
        if (ret == 0) {
            fprintf(stderr, "acl-helper: upstart %s %s OK\n", cmd, job);
        } else {
            /*
             * Expected when job is in "waiting" state (one-shot that already
             * ran at boot) — not a hard error.  Signal-based kill handles the
             * actual processes below.
             */
            fprintf(stderr, "acl-helper: upstart %s %s returned %d (job may be one-shot)\n",
                    cmd, job, ret);
        }
    } else {
        fprintf(stderr, "acl-helper: no ACL upstart job found\n");
    }

    /*
     * Signal ACL processes directly.
     *
     * On this device the ACL upstart jobs (start-acl, start-acl-services)
     * are one-shot: they exec a shell script that spawns services in the
     * background, then exit.  By the time we run, upstart's job is already
     * in "waiting" state -- so "stop <job>" is a no-op and the running ACL
     * processes are orphans from upstart's perspective.
     *
     * For stop: always SIGKILL every ACL process.
     *   SIGSTOP would leave them as frozen-but-alive in /proc, which the
     *   Luna service bus monitor interprets as "ACL unresponsive" and
     *   triggers a restart.  Dead processes (SIGKILL) cannot be revived by
     *   any watchdog.
     *
     * For start: rely on upstart to re-launch ACL ("start start-acl-services").
     *   SIGCONT is useless for SIGKILL'd processes, so skip it.
     */
    if (do_stop) {
        fprintf(stderr, "acl-helper: killing ACL processes by name\n");
        kill_acl_by_name(SIGKILL);
    }
    /* For "start", upstart already handled it above; no signal needed. */

    if (do_stop) mark_stopped(); else mark_running();
    return 0;
}
