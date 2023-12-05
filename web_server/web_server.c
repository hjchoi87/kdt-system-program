#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/mman.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

#define STACK_SIZE (8 * 1024 * 1024)
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

static int child_func(void *arg)
{
    char path[1024];

    if (getcwd(path, 1024) == NULL) {
        fprintf(stderr, "current working directory get error: %s\n", strerror(errno));
        return -1;
    }

    printf(" - [%4d] Current namspace, Parent PID : %d\n", getpid(), getppid() );
    printf("current working directory: %s\n", path);

    // 파일 시스템 완전 격리를 위해서는 pivot_root 필요
    // https://zbvs.tistory.com/14

    if (execl("/usr/local/bin/filebrowser", "filebrowser", "-p", "8282", (char *) NULL)) {
        printf("execfailed\n");
    }
}

int create_web_server()
{
    pid_t child_pid;

    printf("여기서 Web Server 프로세스를 생성합니다.\n");

    char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        printf("mmap failed\n");
        return -1;
    }

    child_pid = clone(child_func, stack + STACK_SIZE, CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, "Hello");
    if (child_pid == -1) {;
        errExit("clone\n");
    }

    printf("PID of child created by clone() is %ld\n", (long) child_pid);

    munmap(stack, STACK_SIZE);

    return 0;
}
