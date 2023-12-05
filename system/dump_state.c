//  refer from AOSP

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/klog.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>

#include <dump_state.h>

typedef void (for_each_pid_func)(int, const char *);
typedef void (for_each_tid_func)(int, int, const char *);

static void __for_each_pid(void (*helper)(int, const char *, void *), const char *header, void *arg) {
    DIR *d;
    struct dirent *de;

    if (!(d = opendir("/proc"))) {
        printf("Failed to open /proc (%s)\n", strerror(errno));
        return;
    }

    printf("\n------ %s ------\n", header);
    while ((de = readdir(d))) {
        int pid;
        int fd;
        char cmdpath[255];
        char cmdline[255];

        if (!(pid = atoi(de->d_name))) {
            continue;
        }

        sprintf(cmdpath,"/proc/%d/cmdline", pid);
        memset(cmdline, 0, sizeof(cmdline));
        if ((fd = open(cmdpath, O_RDONLY)) < 0) {
            strcpy(cmdline, "N/A");
        } else {
            read(fd, cmdline, sizeof(cmdline) - 1);
            close(fd);
        }
        helper(pid, cmdline, arg);
    }

    closedir(d);
}

static void for_each_pid_helper(int pid, const char *cmdline, void *arg) {
    for_each_pid_func *func = arg;
    func(pid, cmdline);
}

void for_each_pid(for_each_pid_func func, const char *header) {
    __for_each_pid(for_each_pid_helper, header, func);
}

static void for_each_tid_helper(int pid, const char *cmdline, void *arg) {
    DIR *d;
    struct dirent *de;
    char taskpath[255];
    for_each_tid_func *func = arg;

    sprintf(taskpath, "/proc/%d/task", pid);

    if (!(d = opendir(taskpath))) {
        printf("Failed to open %s (%s)\n", taskpath, strerror(errno));
        return;
    }

    func(pid, pid, cmdline);

    while ((de = readdir(d))) {
        int tid;
        int fd;
        char commpath[255];
        char comm[255];

        if (!(tid = atoi(de->d_name))) {
            continue;
        }

        if (tid == pid)
            continue;

        sprintf(commpath,"/proc/%d/comm", tid);
        memset(comm, 0, sizeof(comm));
        if ((fd = open(commpath, O_RDONLY)) < 0) {
            strcpy(comm, "N/A");
        } else {
            char *c;
            read(fd, comm, sizeof(comm) - 1);
            close(fd);

            c = strrchr(comm, '\n');
            if (c) {
                *c = '\0';
            }
        }
        func(pid, tid, comm);
    }

    closedir(d);
}

void for_each_tid(for_each_tid_func func, const char *header) {
    __for_each_pid(for_each_tid_helper, header, func);
}

void show_wchan(int pid, int tid, const char *name) {
    char path[255];
    char buffer[255];
    int fd;
    char name_buffer[255];

    memset(buffer, 0, sizeof(buffer));

    sprintf(path, "/proc/%d/wchan", tid);
    if ((fd = open(path, O_RDONLY)) < 0) {
        printf("Failed to open '%s' (%s)\n", path, strerror(errno));
        return;
    }

    if (read(fd, buffer, sizeof(buffer)) < 0) {
        printf("Failed to read '%s' (%s)\n", path, strerror(errno));
        goto out_close;
    }

    snprintf(name_buffer, sizeof(name_buffer), "%*s%s",
             pid == tid ? 0 : 3, "", name);

    printf("%-7d %-32s %s\n", tid, name_buffer, buffer);

out_close:
    close(fd);
    return;
}

void do_dmesg() {
    printf("------ KERNEL LOG (dmesg) ------\n");
    /* Get size of kernel buffer */
    int size = klogctl(KLOG_SIZE_BUFFER, NULL, 0);
    if (size <= 0) {
        printf("Unexpected klogctl return value: %d\n\n", size);
        return;
    }
    char *buf = (char *) malloc(size + 1);
    if (buf == NULL) {
        printf("memory allocation failed\n\n");
        return;
    }
    int retval = klogctl(KLOG_READ_ALL, buf, size);
    if (retval < 0) {
        printf("klogctl failure\n\n");
        free(buf);
        return;
    }
    buf[retval] = '\0';
    printf("%s\n\n", buf);
    free(buf);
    return;
}

/* prints the contents of a file */
int dump_file(const char *title, const char* path) {
    char buffer[32768];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        int err = errno;
        if (title) printf("------ %s (%s) ------\n", title, path);
        printf("*** %s: %s\n", path, strerror(err));
        if (title) printf("\n");
        return -1;
    }

    if (title) printf("------ %s (%s", title, path);

    if (title) {
        struct stat st;
        if (memcmp(path, "/proc/", 6) && memcmp(path, "/sys/", 5) && !fstat(fd, &st)) {
            char stamp[80];
            time_t mtime = st.st_mtime;
            strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&mtime));
            printf(": %s", stamp);
        }
        printf(") ------\n");
    }

    int newline = 0;
    for (;;) {
        int ret = read(fd, buffer, sizeof(buffer));
        if (ret > 0) {
            newline = (buffer[ret - 1] == '\n');
            ret = fwrite(buffer, ret, 1, stdout);
        }
        if (ret <= 0) break;
    }

    close(fd);
    if (!newline) printf("\n");
    if (title) printf("\n");
    return 0;
}

/* redirect output to a file, optionally gzipping; returns gzip pid (or -1) */
pid_t redirect_to_file(FILE *redirect, char *path, int gzip_level) {
    char *chp = path;

    /* skip initial slash */
    if (chp[0] == '/')
        chp++;

    /* create leading directories, if necessary */
    while (chp && chp[0]) {
        chp = strchr(chp, '/');
        if (chp) {
            *chp = 0;
            mkdir(path, 0770);  /* drwxrwx--- */
            *chp++ = '/';
        }
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        exit(1);
    }

    pid_t gzip_pid = -1;
    if (gzip_level > 0) {
        int fds[2];
        if (pipe(fds)) {
            fprintf(stderr, "pipe: %s\n", strerror(errno));
            exit(1);
        }

        fflush(redirect);
        fflush(stdout);

        gzip_pid = fork();
        if (gzip_pid < 0) {
            fprintf(stderr, "fork: %s\n", strerror(errno));
            exit(1);
        }

        if (gzip_pid == 0) {
            dup2(fds[0], STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);

            close(fd);
            close(fds[0]);
            close(fds[1]);

            char level[10];
            snprintf(level, sizeof(level), "-%d", gzip_level);
            execlp("gzip", "gzip", level, NULL);
            fprintf(stderr, "exec(gzip): %s\n", strerror(errno));
            _exit(-1);
        }

        close(fd);
        close(fds[0]);
        fd = fds[1];
    }

    dup2(fd, fileno(redirect));
    close(fd);
    return gzip_pid;
}

void dumpstate(void)
{
    char date[80];
    time_t now = time(NULL);

    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("========================================================\n");
    printf("== dumpstate: %s\n", date);
    printf("========================================================\n");
    printf("\n");
    printf("Kernel: ");
    dump_file(NULL, "/proc/version");
    printf("\n");
    dump_file("MEMORY INFO", "/proc/meminfo");
    dump_file("VIRTUAL MEMORY STATS", "/proc/vmstat");
    dump_file("VMALLOC INFO", "/proc/vmallocinfo");
    dump_file("SLAB INFO", "/proc/slabinfo");
    dump_file("ZONEINFO", "/proc/zoneinfo");
    dump_file("PAGETYPEINFO", "/proc/pagetypeinfo");
    dump_file("BUDDYINFO", "/proc/buddyinfo");
    do_dmesg();
    for_each_tid(show_wchan, "BLOCKED PROCESS WAIT-CHANNELS");
    dump_file("NETWORK DEV INFO", "/proc/net/dev");
    dump_file("NETWORK ROUTES", "/proc/net/route");
    dump_file("NETWORK ROUTES IPV6", "/proc/net/ipv6_route");
    dump_file("INTERRUPTS (1)", "/proc/interrupts");
    printf("\n");
    printf("========================================================\n");
    printf("== dumpstate: done\n");
    printf("========================================================\n");
}
