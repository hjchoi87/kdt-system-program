#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <mqueue.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <camera_HAL.h>
#include <toy_message.h>

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;    ///< true if main loop should exit

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static int toy_timer = 0;

void signal_exit(void);

static void timer_expire_signal_handler()
{
    toy_timer++;
    signal_exit();
}

void set_periodic_timer(long sec_delay, long usec_delay)
{
	struct itimerval itimer_val = {
		 .it_interval = { .tv_sec = sec_delay, .tv_usec = usec_delay },
		 .it_value = { .tv_sec = sec_delay, .tv_usec = usec_delay }
    };

	setitimer(ITIMER_REAL, &itimer_val, (struct itimerval*)0);
}

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

void *watchdog_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);
  
    /* 여기에 구현하세요. */
    while (1) {
        mqretcode = mq_receive(watchdog_queue, (char*)&msg, sizeof(msg), 0);
        if (mqretcode >= 0) {
            printf("watchdog_thread: message received\n");
            printf("msg_type: %d\n", msg.msg_type);
            printf("param1: %d\n", msg.param1);
            printf("param2: %d\n", msg.param1);
            printf("param3: %s\n", (char *)msg.param3);
        }
    }


    return 0;
}

void *monitor_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

    /* 여기에 구현하세요. */
    while (1) {
        mqretcode = mq_receive(monitor_queue, (char*)&msg, sizeof(msg), 0);
        if (mqretcode >= 0) {
            printf("monitor_thread: message received\n");
            printf("msg_type: %d\n", msg.msg_type);
            printf("param1: %d\n", msg.param1);
            printf("param2: %d\n", msg.param1);
            printf("param3: %s\n", (char *)msg.param3);
        }
    }

    return 0;
}

void *disk_service_thread(void* arg)
{
    char *s = arg;
    FILE* apipe;
    char buf[1024];
    char cmd[]="df -h ./" ;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

    /* 여기에 구현하세요. */
    while (1) {
        mqretcode = mq_receive(disk_queue, (char*)&msg, sizeof(msg), NULL);
        if (mqretcode >= 0) {
            printf("disk_service_thread: message received\n");
            printf("msg_type: %d\n", msg.msg_type);
            printf("param1: %d\n", msg.param1);
            printf("param2: %d\n", msg.param1);
            printf("param3: %s\n", (char *)msg.param3);
        }
    }

    return 0;
}

#define CAMERA_TAKE_PICTURE 1

void *camera_service_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

    toy_camera_open();

    /* 여기에 구현하세요. */
    while (1) {
        mqretcode = mq_receive(camera_queue, (char*)&msg, sizeof(msg), 0);
        if (mqretcode >= 0) {
            printf("camera_service_thread: message received\n");
            printf("msg_type: %d\n", msg.msg_type);
            printf("param1: %d\n", msg.param1);
            printf("param2: %d\n", msg.param1);
            printf("param3: %s\n", (char *)msg.param3);
            if (msg.msg_type == CAMERA_TAKE_PICTURE) {
                toy_camera_take_picture();
            }
        }
    }

    return 0;
}

void signal_exit(void)
{
    /* 여기에 구현하세요..  종료 메시지를 보내도록.. */
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    pthread_cond_broadcast(&system_loop_cond);
    pthread_mutex_unlock(&system_loop_mutex);
}

int system_server()
{
    struct itimerspec ts;
    struct sigaction  sa;
    struct sigevent   sev;
    timer_t *tidlist;
    int retcode;
    pthread_t watchdog_thread_tid, monitor_thread_tid, disk_service_thread_tid, camera_service_thread_tid;

    printf("나 system_server 프로세스!\n");

    signal(SIGALRM, timer_expire_signal_handler);
    /* 10초 타이머 등록 */
    set_periodic_timer(10, 0);

    /* 여기에 구현하세요. */
    /* 메시지 큐를 오픈한다. */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    if (watchdog_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    if (monitor_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }
    disk_queue = mq_open("/disk_queue", O_RDWR);
    if (disk_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }
    camera_queue = mq_open("/camera_queue", O_RDWR);
    if (camera_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }

    /* 스레드를 생성한다. */
    retcode = pthread_create(&watchdog_thread_tid, NULL, watchdog_thread, "watchdog thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&monitor_thread_tid, NULL, monitor_thread, "monitor thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&disk_service_thread_tid, NULL, disk_service_thread, "disk service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&camera_service_thread_tid, NULL, camera_service_thread, "camera service thread\n");
    assert(retcode == 0);

    printf("system init done.  waiting...");

    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false) {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    printf("<== system\n");

    while (1) {
        sleep(1);
    }

    return 0;
}

int create_system_server()
{
    pid_t systemPid;
    const char *name = "system_server";

    printf("여기서 시스템 프로세스를 생성합니다.\n");

    /* fork 를 이용하세요 */
    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        /* 프로세스 이름 변경 */
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
            perror("prctl()");
        system_server();
        break;
    default:
        break;
    }

    return 0;
}