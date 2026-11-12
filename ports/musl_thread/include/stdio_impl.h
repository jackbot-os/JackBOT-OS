#ifndef _STDIO_IMPL_H
#define _STDIO_IMPL_H

/* 移植层 stdio_impl.h —— musl 完整版依赖 FILE 锁与 mbstate，体量过大。
 * pthread_create.c 只用 FILE 链表初始化 stdio 锁（init_file_lock），
 * 这里给最小形状；__ofl_lock/__ofl_unlock 实现见 src/stubs.c。
 * （非第三方代码，属移植胶水。）
 */
struct _IO_FILE {
	int lock;
	struct _IO_FILE *next;
};
#define FILE struct _IO_FILE

FILE **__ofl_lock(void);
void __ofl_unlock(void);

#endif
