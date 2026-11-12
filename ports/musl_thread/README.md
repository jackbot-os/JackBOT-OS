# ports/musl_thread — musl 1.2.5 pthread → JackBOT int 0x80 移植层

上游：`third_party/musl-1.2.5/`（musl.libc.org 官方发布包，MIT，sha256 已核对，
见 `third_party/README.md` 台账）。本目录规则：**musl 算法源码一律逐字保留**，
ABI 适配只发生在 3 个汇编文件 + 移植层胶水（`include/syscall_arch.h` 等）。

## 1. 文件来源对照（逐文件）

### verbatim（逐字复制，零改动，只加了来源头注释）

| 本目录 | musl-1.2.5 原树路径 |
| --- | --- |
| `include/features.h` | `src/include/features.h` |
| `include/pthread_arch.h` | `arch/x86_64/pthread_arch.h` |
| `include/atomic.h` | `src/internal/atomic.h` |
| `include/atomic_arch.h` | `arch/x86_64/atomic_arch.h` |
| `include/futex.h` | `src/internal/futex.h` |
| `include/pthread_impl.h` | `src/internal/pthread_impl.h` |
| `include/lock.h` | `src/internal/lock.h` |
| `include/fork_impl.h` | `src/internal/fork_impl.h` |
| `include/bits/errno.h` | `arch/generic/bits/errno.h`（x86_64 用通用版） |
| `src/pthread_create.c` | `src/thread/pthread_create.c` |
| `src/pthread_join.c` | `src/thread/pthread_join.c` |
| `src/pthread_detach.c` | `src/thread/pthread_detach.c` |
| `src/pthread_self.c` | `src/thread/pthread_self.c` |
| `src/pthread_equal.c` | `src/thread/pthread_equal.c` |
| `src/pthread_setcancelstate.c` | `src/thread/pthread_setcancelstate.c` |
| `src/pthread_testcancel.c` | `src/thread/pthread_testcancel.c` |
| `src/pthread_mutex_init.c` | `src/thread/pthread_mutex_init.c` |
| `src/pthread_mutex_destroy.c` | `src/thread/pthread_mutex_destroy.c` |
| `src/pthread_mutex_lock.c` | `src/thread/pthread_mutex_lock.c` |
| `src/pthread_mutex_unlock.c` | `src/thread/pthread_mutex_unlock.c` |
| `src/pthread_mutex_trylock.c` | `src/thread/pthread_mutex_trylock.c` |
| `src/pthread_mutex_timedlock.c` | `src/thread/pthread_mutex_timedlock.c` |
| `src/pthread_cond_init.c` | `src/thread/pthread_cond_init.c` |
| `src/pthread_cond_destroy.c` | `src/thread/pthread_cond_destroy.c` |
| `src/pthread_cond_wait.c` | `src/thread/pthread_cond_wait.c` |
| `src/pthread_cond_signal.c` | `src/thread/pthread_cond_signal.c` |
| `src/pthread_cond_broadcast.c` | `src/thread/pthread_cond_broadcast.c` |
| `src/__wait.c` | `src/thread/__wait.c` |
| `src/__lock.c` | `src/thread/__lock.c` |
| `src/__timedwait.c` | `src/thread/__timedwait.c` |
| `src/vmlock.c` | `src/thread/vmlock.c` |

### 1 处手术（算法不变，仅内核能力回退）

| 本目录 | musl 原路径 | 改动 |
| --- | --- | --- |
| `src/pthread_cond_timedwait.c` | `src/thread/pthread_cond_timedwait.c` | `unlock_requeue`：内核返回 `-ENOSYS`（无 FUTEX_REQUEUE）时回退 `__wake(l,1,1)`，否则等待链会卡死。原版依赖 Linux 必有 REQUEUE。 |

### 3 个 ABI 汇编（musl 原汇编改入口/编号/寄存器）

| 本目录 | musl 原路径 | 改动 |
| --- | --- | --- |
| `src/clone.s` | `src/thread/x86_64/clone.s` | `syscall`→`int $0x80`；编号 56→33、60→34；寄存器映射改为 **SysV 五参：rdi=flags rsi=stack rdx=ptid r10=tls r8=ctid**（原版是 Linux 内核的 r10=ctid/r8=tls） |
| `src/set_thread_area.s` | `src/thread/x86_64/__set_thread_area.s` | `syscall`→`int $0x80`；编号 158→43（arch_prctl, ARCH_SET_FS） |
| `src/unmapself.s` | `src/thread/x86_64/__unmapself.s` | `syscall`→`int $0x80`；编号 11→39、60→34 |

### 移植层胶水（本目录自写，非第三方代码）

| 文件 | 作用 |
| --- | --- |
| `include/syscall_arch.h` | `int $0x80` 入口，`__syscall0..6`（rax=号，rdi/rsi/rdx/r10/r8/r9） |
| `include/syscall.h` | 编号表（33 起，**占位，待内核表确认**）；`__syscall_cp` 简化为直转 |
| `include/libc.h` | 裁剪自 `src/internal/libc.h`（`struct __libc` 等） |
| `include/stdio_impl.h` | 最小 FILE 形状（pthread_create 初始化 stdio 锁用） |
| `include/pthread.h` | 公开头，union 布局/常量**严格对齐 musl ABI** |
| `include/signal.h`、`time.h`、`threads.h`、`errno.h`、`sys/mman.h` | 最小宿主头 |
| `src/stubs.c` | 宿主桩：`__libc` 实例、`__thread_list_lock`、默认栈尺寸、`__mmap/__munmap/__mprotect`、`__copy_tls`（零初始化版）、`exit`、`__clock_gettime`、`__errno_location`、信号空操作、stdio 空链表、主线程自举 `__pthread_self_bootstrap()` |
| `tests/smoke_pthread.c` | 验收：8 线程×10 万 mutex 自增 + 2 线程 condvar ping-pong + join |

## 2. 内核侧对接契约（必须对齐）

1. `int $0x80`；rax=号；参数 rdi/rsi/rdx/r10/r8/r9（六参 SysV 顺序）；返回 rax，负数=-errno。
2. `SYS_clone`：五参 SysV。必须实现 `CLONE_SETTLS`（r10→fs 基址）、
   `CLONE_PARENT_SETTID`（写子 tid 到 rdx）、`CLONE_CHILD_CLEARTID`（记录 r8 地址）。
3. 线程退出路径（`SYS_exit`）：**先写 `*ctid=0` 并 futex 唤醒，再销毁线程**；
   写 0 之后线程绝不再访问用户态内存。musl 把 ctid 指到 `__thread_list_lock`，
   这就是线程链表锁的 AS 安全解锁，缺了会死锁。
4. `SYS_futex`：`FUTEX_WAIT(0)`/`FUTEX_WAKE(1)` 必须；`FUTEX_REQUEUE(3)` 建议
   （无 REQUEUE 时有回退，正确性不受影响，仅唤醒链变朴素）。
5. `SYS_mmap`/`SYS_munmap`：栈映射分配与回收（detached 线程用 `__unmapself`）。
6. `SYS_arch_prctl`：主线程自举（或内核在进程入口直接设 fs 基址）。
7. `SYS_exit_group`、`SYS_write`、`SYS_gettid`：测试与进程退出。
8. 信号类（41/44）、`set_tid_address`(37)、`set_robust_list`(42)、
   `sched_setscheduler`(45)、`clock_gettime`(46)：默认路径不触发，
   可先返回 `-ENOSYS`，musl 侧均已按此处理。

## 3. 与早期草案的偏差（请确认）

- **pthread_t = unsigned long（TCB 句柄），不是整数 tid**：musl 官方 join/detach
  解引用该句柄；改整数 tid 会破坏 musl 算法。内核侧不受影响。
- **musl 1.2.5 没有 `src/futex` 目录，也没有 `__futex.c`**：futex 封装实际在
  `src/internal/pthread_impl.h`（`__wake`/`__futexwait` 内联）、`src/thread/__wait.c`、
  `src/thread/__timedwait.c`。本移植按真实布局落。
- 取消（SIGCANCEL）、完整 TLS 拷贝、atexit 链：本阶段宿主桩简化版
  （见 stubs.c 注释），musl 算法代码未改，内核补能力后只扩桩。
- 编译需 `-fno-stack-protector`（或内核把 fs:0x28 canary 放进 TCB/TLS 区）。

## 4. 待办（依赖内核侧）

- [ ] `include/syscall.h` 编号表与内核 syscall 表最终对齐（现在 33 起为占位）。
- [ ] crt0 调用 `__pthread_self_bootstrap()`（或内核入口设主线程 fs）。
- [ ] detached 线程栈回收路径（`__unmapself`）实测。
