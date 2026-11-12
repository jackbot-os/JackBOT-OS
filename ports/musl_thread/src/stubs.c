/* 移植层宿主桩 —— JackBOT 内核服务适配，非第三方代码。
 * 每一项注明其替代的 musl 原文件，保证“musl 算法代码零改动、宿主依赖显式化”。
 */
#include <stddef.h>
#include "pthread_impl.h"
#include "stdio_impl.h"
#include "sys/mman.h"

/* ---- libc 全局（musl src/env/__libc_start_main.c 初始化；这里静态初始化） ---- */
struct __libc __libc = {
	.can_do_threads = 1,
	.need_locks = -1,   /* musl __lock.c 语义：0=单线程直通；-1 表示首个 __lock 置 0 */
	.page_size = 4096,
};

/* musl src/env/__init_tls.c 第 12 行定义；用作线程链表锁 + clone 的 ctid。
 * 线程退出时内核写 0 并 futex 唤醒（AS 安全解锁的关键）。 */
volatile int __thread_list_lock;

/* musl src/thread/default_attr.c */
unsigned __default_stacksize = DEFAULT_STACK_SIZE;
unsigned __default_guardsize = DEFAULT_GUARD_SIZE;

/* ---- mmap 薄封装（musl src/mman/mmap.c、munmap.c、mprotect.c） ---- */
void *__mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	return (void *)__syscall(SYS_mmap, addr, len, prot, flags, fd, off);
}
int __munmap(void *addr, size_t len)
{
	return __syscall(SYS_munmap, addr, len);
}
int __mprotect(void *addr, size_t len, int prot)
{
	return __syscall(SYS_mprotect, addr, len, prot);
}

/* ---- TLS 拷贝（musl src/env/__init_tls.c 的 __copy_tls） ----
 * 原版按 TLS 模块表拷贝动态 TLS；移植版尚无动态链接器/TLS 模块，
 * 只零初始化 struct pthread（fs:0 指向它，TP_ADJ(p)=p）。
 * 后续接入 TLS 时在此扩展，不影响 musl 线程算法。 */
void *__copy_tls(unsigned char *mem)
{
	struct pthread *td = (struct pthread *)mem;
	__builtin_memset(td, 0, sizeof *td);
	return td;
}

/* ---- exit（musl src/exit/exit.c 走 atexit 链；移植版直接 exit_group） ---- */
_Noreturn void exit(int code)
{
	for (;;) __syscall(SYS_exit_group, code);
}

/* ---- 时钟（musl src/time/clock_gettime.c；仅带超时路径引用） ---- */
int __clock_gettime(clockid_t clk, struct timespec *ts)
{
	return __syscall(SYS_clock_gettime, clk, ts);
}

/* ---- errno 线程本地（musl __errno_location 读当前线程 errno_val） ---- */
int *__errno_location(void)
{
	return &__pthread_self()->errno_val;
}

/* ---- 信号（musl src/signal/block.c） ----
 * 内核无信号阶段：全部为空操作。SIGCANCEL 不存在，取消机制不触发，
 * 与 include/syscall.h 中 __syscall_cp 的简化一致。 */
void __block_all_sigs(void *set) { (void)set; }
void __block_app_sigs(void *set)  { (void)set; }
void __restore_sigs(void *set)    { (void)set; }

/* ---- stdio 锁链表（musl src/stdio/__ofl_lock.c；移植版恒为空链表） ---- */
static FILE *__ofl_head;
FILE **__ofl_lock(void) { return &__ofl_head; }
void __ofl_unlock(void) { }

/* ---- 主线程自举（musl src/env/__init_tls.c 的 __init_tp 路径） ----
 * crt0 在进入用户程序前调用本函数（或在 kernel 侧直接设 fs 基址）。
 * 依赖 SYS_arch_prctl(ARCH_SET_FS=0x1002) 或内核等价能力。
 * tid 暂由 gettid 取；内核未实现时置 0（仅影响 ERRORCHECK 互斥锁属主校验，
 * NORMAL 互斥锁不受影响）。 */
void __pthread_self_bootstrap(void)
{
	static struct pthread td;
	td.self = &td;
	td.tid = (int)__syscall(SYS_gettid);
	td.detach_state = DT_JOINABLE;
	td.robust_list.head = &td.robust_list.head;
	__set_thread_area(&td);
}
