#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <sys/mman.h>
#include <signal.h>

void init_timer(void);
int rt_init(void);
void init_pwm(void);
char kbhit(void);

u_int32_t T = 1000000, c = 0, duty = 0;

void *thread_adc_func(void *);
void *thread_pwm_func(void *);

pthread_cond_t timer;
pthread_cond_t adc;
pthread_cond_t pwm;
pthread_mutex_t lock;

timer_t timerid;
struct sigevent sev;
struct itimerspec trigger;

void Tstimer_thread(union sigval sv) {
    puts("100ms elapsed.");
    pthread_mutex_lock(&lock);
    pthread_cond_signal(&timer);
    pthread_mutex_unlock(&lock);
    timer_settime(timerid, 0, &trigger, NULL);
}

void *thread_adc_func(void *v) {
    int f;
    char a[5];
    while (1) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&timer, &lock);
        pthread_mutex_unlock(&lock);

        f = open("/sys/bus/iio/devices/iio:device0/in_voltage5_raw", O_RDONLY);
        read(f, &a, sizeof(a));
        c = atoi(a);
        printf("ADC value: %d\n", c);
        close(f);

        pthread_mutex_lock(&lock);
        pthread_cond_signal(&adc);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

void *thread_pwm_func(void *v) {
    int fd;
    char str[10];
    while (1) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&pwm, &lock);
        pthread_mutex_unlock(&lock);

        fd = open("/sys/class/pwm/pwmchip5/pwm1/duty_cycle", O_WRONLY);
        duty = (2 * T / 10) + (8 * T / 10 * c >> 12);
        sprintf(str, "%d", duty);
        printf("Duty cycle: %s\n", str);
        write(fd, str, sizeof(str));
        close(fd);
    }
    return NULL;
}

int main(void) {
    pthread_t *thread_adc;
    pthread_t *thread_pwm;

    init_timer();
    init_pwm();

    if (rt_init()) exit(0);

    thread_adc = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(thread_adc, NULL, thread_adc_func, NULL);

    thread_pwm = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(thread_pwm, NULL, thread_pwm_func, NULL);

    while (kbhit() != 'q');

    timer_delete(timerid);
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&pwm);

    return EXIT_SUCCESS;
}

int rt_init(void) {
    struct sched_param param;

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        printf("mlockall failed: %m\n");
        exit(-2);
    }

    ret = pthread_attr_init(&attr);
    if (ret) printf("init pthread attributes failed\n");

    ret = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    if (ret) printf("pthread setstacksize failed\n");

    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if (ret) printf("pthread setschedpolicy failed\n");

    param.sched_priority = 80;
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret) printf("pthread setschedparam failed\n");

    ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    if (ret) printf("pthread setinheritsched failed\n");

    return ret;
}

void init_pwm(void) {
    char str[10];
    int fd = open("/sys/devices/platform/ocp/ocp:P9_16_pinmux/state", O_WRONLY);
    write(fd, "pwm", sizeof("pwm"));
    close(fd);

    fd = open("/sys/class/pwm/pwmchip5/pwm1/period", O_WRONLY);
    sprintf(str, "%d", T);
    write(fd, str, sizeof(str));
    close(fd);

    fd = open("/sys/class/pwm/pwmchip5/pwm1/duty_cycle", O_WRONLY);
    sprintf(str, "%d", (T * 2) / 10);
    write(fd, str, sizeof(str));
    close(fd);

    fd = open("/sys/class/pwm/pwmchip5/pwm1/enable", O_WRONLY);
    write(fd, "1", sizeof("1"));
    close(fd);
}

void init_timer(void) {
    memset(&sev, 0, sizeof(struct sigevent));
    memset(&trigger, 0, sizeof(struct itimerspec));

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = &Tstimer_thread;

    timer_create(CLOCK_REALTIME, &sev, &timerid);

    trigger.it_value.tv_sec = 0;
    trigger.it_value.tv_nsec = 100000000;

    timer_settime(timerid, 0, &trigger, NULL);
}
