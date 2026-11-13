#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stdint.h>

/* Kernel IPC: pipes, System V style shared memory, counting semaphores and
   byte message queues. Every object lives in a fixed table inside the kernel;
   handles are plain small integers, so a user process never gets a pointer.

   Pipes own a pair of file descriptors in a reserved range so that the
   ordinary read/write syscalls can route to them without a per-process fd
   table. */

#define IPC_FD_BASE  64     /* fd 64.. are pipe ends, never FAT handles */
#define IPC_FD_LIMIT (IPC_FD_BASE + 24)

void ipc_init(void);

/* ---- pipes ---- */
/* Creates both ends; out[0] reads, out[1] writes. 0 on success, -1 if full. */
int ipc_pipe_create(int *out);
/* fd belongs to the pipe subsystem (so read/write can dispatch on it). */
int ipc_fd_is_pipe(int fd);
/* Blocking read: returns bytes, 0 on end-of-pipe, -1 on a bad fd. */
int64_t ipc_pipe_read(int fd, void *buf, uint32_t len);
int64_t ipc_pipe_write(int fd, const void *buf, uint32_t len);
int ipc_pipe_close(int fd);
/* Readiness probes used by epoll/poll. 1 means the end can move data now. */
int ipc_pipe_alive(int fd);
int ipc_pipe_readable(int fd);
int ipc_pipe_writable(int fd);

/* ---- shared memory ---- */
/* size is rounded up to whole pages. Returns a handle or -1. */
int ipc_shm_create(uint32_t size);
/* Maps the whole object at vaddr in the given address space. 0 on success. */
int ipc_shm_map(int handle, uint64_t p4phys, uint64_t vaddr);
int ipc_shm_pages(int handle);
uint64_t ipc_shm_phys(int handle, int page);

/* ---- counting semaphores ---- */
int ipc_sem_create(int value);
/* Blocks until the count is positive, or gives up after the wait budget. */
int ipc_sem_wait(int id);
int ipc_sem_post(int id);
int ipc_sem_value(int id);

/* ---- message queues (fixed 64 byte messages) ---- */
int ipc_mq_create(void);
/* Copies the payload into the queue; -1 when the queue is full. */
int ipc_mq_send(int id, const void *msg, uint32_t len);
/* Copies out the oldest message, returns its length, -1 when empty. */
int ipc_mq_recv(int id, void *msg, uint32_t len);
int ipc_mq_count(int id);

/* Prints one [IPC] line per subsystem after exercising it at boot. */
void ipc_selftest(void);

#endif
