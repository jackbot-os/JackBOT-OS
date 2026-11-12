/* 验收冒烟：8 线程 mutex 自增 10 万 + 2 线程 condvar ping-pong + join 回收。
 * 只调用 musl 官方 pthread API；打印走 SYS_write(48)。
 * 预期：counter=800000  pingpong=2000
 */
#include "pthread.h"
#include "syscall.h"

void __pthread_self_bootstrap(void);

static void put_str(const char *s)
{
	long n = 0;
	while (s[n]) n++;
	__syscall(SYS_write, 1, s, n);
}
static void put_int(long v)
{
	char b[24]; int i = 0;
	if (v < 0) { put_str("-"); v = -v; }
	do { b[i++] = '0' + (v % 10); v /= 10; } while (v);
	while (i) { char c[1]; c[0] = b[--i]; __syscall(SYS_write, 1, c, 1); }
}

#define N_THREADS 8
#define N_ITERS   100000

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_counter = 0;

static void *worker(void *arg)
{
	(void)arg;
	for (int i = 0; i < N_ITERS; i++) {
		pthread_mutex_lock(&g_lock);
		g_counter++;
		pthread_mutex_unlock(&g_lock);
	}
	return 0;
}

#define PINGPONG 1000
static pthread_mutex_t pp_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pp_cv = PTHREAD_COND_INITIALIZER;
static int pp_turn = 0;
static int pp_count = 0;

static void *pinger(void *arg)
{
	(void)arg;
	for (int i = 0; i < PINGPONG; i++) {
		pthread_mutex_lock(&pp_mu);
		while (pp_turn != 0) pthread_cond_wait(&pp_cv, &pp_mu);
		pp_count++;
		pp_turn = 1;
		pthread_cond_signal(&pp_cv);
		pthread_mutex_unlock(&pp_mu);
	}
	return 0;
}

static void *ponger(void *arg)
{
	(void)arg;
	for (int i = 0; i < PINGPONG; i++) {
		pthread_mutex_lock(&pp_mu);
		while (pp_turn != 1) pthread_cond_wait(&pp_cv, &pp_mu);
		pp_count++;
		pp_turn = 0;
		pthread_cond_signal(&pp_cv);
		pthread_mutex_unlock(&pp_mu);
	}
	return 0;
}

int main(void)
{
	/* 若 crt0 未做，主线程自举（设 fs 基址到静态 TCB）。 */
	__pthread_self_bootstrap();

	pthread_t t[N_THREADS];
	for (int i = 0; i < N_THREADS; i++)
		pthread_create(&t[i], 0, worker, 0);
	for (int i = 0; i < N_THREADS; i++)
		pthread_join(t[i], 0);

	pthread_t a, b;
	pthread_create(&a, 0, pinger, 0);
	pthread_create(&b, 0, ponger, 0);
	pthread_join(a, 0);
	pthread_join(b, 0);

	put_str("counter=");  put_int(g_counter);
	put_str(" expected="); put_int(N_THREADS * N_ITERS); put_str("\n");
	put_str("pingpong="); put_int(pp_count);
	put_str(" expected="); put_int(PINGPONG * 2); put_str("\n");
	return 0;
}
