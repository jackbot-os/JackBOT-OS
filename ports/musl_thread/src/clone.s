/* Origin: musl-1.2.5 src/thread/x86_64/clone.s (MIT).
 * ABI adaptation for JackBOT:
 *   1) "syscall" -> "int $0x80"
 *   2) SYS_clone: Linux 56 -> JackBOT 33；SYS_exit: Linux 60 -> JackBOT 34
 *   3) 寄存器映射改为 JackBOT 内核约定（SysV 五参顺序）：
 *      rdi=flags  rsi=child_stack  rdx=ptid  r10=tls  r8=ctid
 *      （musl 原版按 Linux 内核的 r10=ctid / r8=tls 映射，这里调换了 r10/r8）
 * 调用约定不变：__clone(fn, stack, flags, arg, ptid, tls, ctid)
 *   入口: rdi=fn  rsi=stack  rdx=flags  rcx=arg  r8=ptid  r9=tls  [rsp+8]=ctid
 */
.text
.global __clone
.hidden __clone
.type   __clone,@function
__clone:
	mov %rdi,%r11        # r11 = fn
	mov %rdx,%rdi        # rdi = flags
	mov %r8,%rdx         # rdx = ptid
	mov %r9,%r10         # r10 = tls  (CLONE_SETTLS → 内核设 fs 基址)
	mov 8(%rsp),%r8      # r8  = ctid (CLONE_CHILD_CLEARTID)
	mov %r11,%r9         # r9 = fn
	and $-16,%rsi        # 子栈 16 字节对齐
	sub $8,%rsi
	mov %rcx,(%rsi)      # 压 arg 到子栈
	mov $33,%eax         # SYS_clone (JackBOT)
	int $0x80
	test %eax,%eax
	jnz 1f               # 父线程：eax = 子 tid，返回
	xor %ebp,%ebp        # 子线程：清帧指针
	pop %rdi             # rdi = arg
	call *%r9            # 调 fn(arg)
	mov %eax,%edi
	mov $34,%eax         # SYS_exit (JackBOT)
	int $0x80
	hlt
1:	ret
