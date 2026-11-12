/* Origin: musl-1.2.5 src/thread/x86_64/__unmapself.s (MIT).
 * ABI adaptation: "syscall" -> "int $0x80";
 * SYS_munmap: Linux 11 -> JackBOT 39；SYS_exit: Linux 60 -> JackBOT 34。
 * 语义不变：detached 线程先 munmap 自己的栈映射再退出，期间不碰栈。
 */
.text
.global __unmapself
.type   __unmapself,@function
__unmapself:
	movl $39,%eax   /* SYS_munmap (JackBOT) */
	int $0x80       /* munmap(arg2,arg3) */
	xor %rdi,%rdi   /* exit() args: always return success */
	movl $34,%eax   /* SYS_exit (JackBOT) */
	int $0x80       /* exit(0) */
