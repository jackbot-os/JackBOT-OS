#ifndef JB_LIBC_H
#define JB_LIBC_H

/* Minimal freestanding libc for JackBOT native (.jca) payloads.
   Syscalls go through int 0x80 with the numbers in kernel/posix.h. */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;

#define SYS_WRITE   16
#define SYS_READ    17
#define SYS_OPEN    18
#define SYS_CLOSE   19
#define SYS_LSEEK   20
#define SYS_BRK     21
#define SYS_STAT    22
#define SYS_UNLINK  23
#define SYS_TIME    24
#define SYS_GETPID  25
#define SYS_FSTAT   26
#define SYS_MKDIR   27
#define SYS_RENAME  28
#define SYS_ACCESS  29
#define SYS_ISATTY  30
#define SYS_SLEEP   31
#define SYS_UPTIME  32

#define O_RDONLY 0
#define O_WRONLY 1
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* implemented in sys.s */
long __jb_sys(long num, long a0, long a1, long a2);

/* ---- string ---- */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int v, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int atoi(const char *s);

/* ---- stdlib ---- */
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code);
int abs(int v);

/* ---- unistd ---- */
int open(const char *path, int flags);
int close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
off_t lseek(int fd, off_t off, int whence);
long time(long *t);
int getpid(void);
int unlink(const char *path);
int stat(const char *path, unsigned int out[2]);
int mkdir(const char *path);
int rename(const char *from, const char *to);
int access(const char *path, int mode);
int isatty(int fd);
int sleep_ms(unsigned int msec);
unsigned long uptime(void);

/* ---- stdio ---- */
typedef struct {
    int fd;
    int eof;
    int err;
} FILE;

extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t sz, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t sz, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fgetc(FILE *f);
int fputc(int c, FILE *f);
int puts(const char *s);
int putchar(int c);
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int snprintf(char *buf, size_t cap, const char *fmt, ...);

#endif

