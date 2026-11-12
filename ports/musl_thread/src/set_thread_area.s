/* Origin: musl-1.2.5 src/thread/x86_64/__set_thread_area.s (MIT).
 * ABI adaptation: "syscall" -> "int $0x80";
 * SYS_arch_prctl: Linux 158 -> JackBOT 43（编号见 include/syscall.h）。
 * 语义不变：arch_prctl(ARCH_SET_FS=0x1002, arg)，主线程自举用。
 */
.text
.global __set_thread_area
.hidden __set_thread_area
.type __set_thread_area,@function
__set_thread_area:
	mov %rdi,%rsi           /* shift for syscall */
	movl $0x1002,%edi       /* SET_FS register */
	movl $43,%eax           /* SYS_arch_prctl (JackBOT) */
	int $0x80
	ret
