#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <mqueue.h>
#include <sys/inotify.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/sysmacros.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <toy_message.h>
#include <shared_memory.h>
#include <dump_state.h>
#include <hardware.h>

#define BUF_LEN 1024
#define TOY_TEST_FS "./fs"

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;    ///< true if main loop should exit

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static int toy_timer = 0;
pthread_mutex_t toy_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t global_timer_sem;
static bool global_timer_stopped;

static shm_sensor_t *the_sensor_info = NULL;
void set_periodic_timer(long sec_delay, long usec_delay);

static void timer_expire_signal_handler()
{
    // signal 문맥에서는 비동기 시그널 안전 함수(async-signal-safe function) 사용
    // man signal 확인
    // sem_post는 async-signal-safe function
    // 여기서는 sem_post 사용
    sem_post(&global_timer_sem);
}

static void system_timeout_handler()
{
    // 여기는 signal hander가 아니기 때문에 안전하게 mutex lock 사용 가능
    pthread_mutex_lock(&toy_timer_mutex);
    toy_timer++;
    pthread_mutex_unlock(&toy_timer_mutex);
}

static void *timer_thread(void *not_used)
{
    signal(SIGALRM, timer_expire_signal_handler);
    set_periodic_timer(1, 1);

	while (!global_timer_stopped) {
		int rc = sem_wait(&global_timer_sem);
		if (rc == -1 && errno == EINTR) {
		    continue;
		}

		if (rc == -1) {
		    perror("sem_wait");
		    exit(-1);
		}
        // 아래 sleep을 sem_wait 함수를 사용하여 동기화 처리
        // sleep(1);
		system_timeout_handler();
	}
	return 0;
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

    while (1) {
        mqretcode = (int)mq_receive(watchdog_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("watchdog_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
    }

    return 0;
}

#define SENSOR_DATA 1
#define DUMP_STATE 2

void *monitor_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;
    int shmid;

    printf("%s", s);

    while (1) {
        mqretcode = (int)mq_receive(monitor_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("monitor_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == SENSOR_DATA) {
            shmid = msg.param1;
            the_sensor_info = toy_shm_attach(shmid);
            printf("sensor temp: %d\n", the_sensor_info->temp);
            printf("sensor info: %d\n", the_sensor_info->press);
            printf("sensor humidity: %d\n", the_sensor_info->humidity);
            toy_shm_detach(the_sensor_info);
        } else if (msg.msg_type == DUMP_STATE) {
            dumpstate();
        } else {
            printf("monitor_thread: unknown message. xxx\n");
        }
    }

    return 0;
}

// 엔진 스레드 추가
// 엔진 스레드는 가장 우선 순위가 높은 RT 스케줄링 적용 : RT_FIFO와 RT_RR이 있다.
void *engine_thread()
{

}

// https://stackoverflow.com/questions/21618260/how-to-get-total-size-of-subdirectories-in-c
static long get_directory_size(char *dirname)
{
    DIR *dir = opendir(dirname);
    if (dir == 0)
        return 0;

    struct dirent *dit;
    struct stat st;
    long size = 0;
    long total_size = 0;
    char filePath[1024];

    while ((dit = readdir(dir)) != NULL) {
        if ( (strcmp(dit->d_name, ".") == 0) || (strcmp(dit->d_name, "..") == 0) )
            continue;

        sprintf(filePath, "%s/%s", dirname, dit->d_name);
        if (lstat(filePath, &st) != 0)
            continue;
        size = st.st_size;

        if (S_ISDIR(st.st_mode)) {
            long dir_size = get_directory_size(filePath) + size;
            total_size += dir_size;
        } else {
            total_size += size;
        }
    }
    return total_size;
}


void *disk_service_thread(void* arg)
{
    char *s = arg;
    int inotifyFd, wd, j;
    char buf[BUF_LEN] __attribute__ ((aligned(8)));
    ssize_t numRead;
    char *p;
    struct inotify_event *event;
    char *directory = TOY_TEST_FS;
    int total_size;

    printf("%s", s);

    inotifyFd = inotify_init();                 /* Create inotify instance */
    if (inotifyFd == -1)
        return 0;

    wd = inotify_add_watch(inotifyFd, TOY_TEST_FS, IN_CREATE);
    if (wd == -1)
        return 0;

    for (;;) {                                  /* Read events forever */
        numRead = read(inotifyFd, buf, BUF_LEN);
        if (numRead == 0) {
            printf("read() from inotify fd returned 0!");
            return 0;
        }

        if (numRead == -1)
            return 0;

        for (p = buf; p < buf + numRead; ) {
            event = (struct inotify_event *) p;
            p += sizeof(struct inotify_event) + event->len;
        }
        total_size = get_directory_size(TOY_TEST_FS);
        printf("directory size: %d\n", total_size);
    }

    return 0;
}

#define CAMERA_TAKE_PICTURE 1

void *camera_service_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;
    hw_module_t *module = NULL;
    int res;

    printf("%s", s);

    res = hw_get_camera_module((const hw_module_t **)&module);
    assert(res == 0);
    printf("Camera module name: %s\n", module->name);
    printf("Camera module tag: %d\n", module->tag);
    printf("Camera module id: %s\n", module->id);
    module->open();

    while (1) {
        mqretcode = (int)mq_receive(camera_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("camera_service_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == CAMERA_TAKE_PICTURE) {
            module->take_picture();
        } else if (msg.msg_type == DUMP_STATE) {
            module->dump();
        } else {
            printf("camera_service_thread: unknown message. xxx\n");
        }
    }

    return 0;
}

void signal_exit(void)
{
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
    pthread_t timer_thread_tid;

    printf("나 system_server 프로세스!\n");

    /* 메시지 큐를 오픈한다. */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

    /* 스레드를 생성한다. */
    retcode = pthread_create(&watchdog_thread_tid, NULL, watchdog_thread, "watchdog thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&monitor_thread_tid, NULL, monitor_thread, "monitor thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&disk_service_thread_tid, NULL, disk_service_thread, "disk service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&camera_service_thread_tid, NULL, camera_service_thread, "camera service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&timer_thread_tid, NULL, timer_thread, "timer thread\n");
    assert(retcode == 0);

    printf("system init done.  waiting...");

    // 여기에 구현하세요... 여기서 cond wait로 대기한다. 10초 후 알람이 울리면 <== system 출력
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
