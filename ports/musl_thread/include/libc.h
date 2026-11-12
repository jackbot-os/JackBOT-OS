#ifndef LIBC_H
#define LIBC_H

/* 移植层 libc.h —— 裁剪自 musl-1.2.5 src/internal/libc.h（MIT）。
 * 只保留 pthread 子集需要的字段与宏；__libc 实例在 src/stubs.c。
 * 字段布局与原版一致（截断尾部未用字段，见下）。
 */
#include <stdlib.h>
#include <limits.h>

struct __locale_map;

struct __locale_struct {
	const struct __locale_map *cat[6];
};

typedef struct __locale_struct *locale_t;

struct tls_module {
	struct tls_module *next;
	void *image;
	size_t len, size, align, offset;
};

struct __libc {
	char can_do_threads;
	char threaded;
	char secure;
	volatile signed char need_locks;
	int threads_minus_1;
	size_t *auxv;
	struct tls_module *tls_head;
	size_t tls_size, tls_align, tls_cnt;
	size_t page_size;
	struct __locale_struct global_locale;
};

#ifndef PAGE_SIZE
#define PAGE_SIZE libc.page_size
#endif

extern struct __libc __libc;
#define libc __libc

#endif
