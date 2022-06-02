#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
	movq %rbx, temp1(%rip)	   /* rbx = 유저 프로그램의 메모리 주소 -> rip 주소에 임시로 저장해둔다. */
	movq %r12, temp2(%rip)     /* callee(user program) saved registers */
	movq %rsp, %rbx            /* rsp = 유저 프로그램의 현재 스택 주소 -> rbx에 옮겨둔다. */
	movabs $tss, %r12		   /* movabs = 64비트값을 레지스터에 쓰는 명령어 -> $tss(tss 상수 값)를 r12주소에 저장한다. */
	movq (%r12), %r12		   /* (%r12) = tss값 -> r12 주소에 옮긴다. */
	movq 4(%r12), %rsp         /* 4(%r12) = (4+%r12) 주소값 -> rsp 주소에 옮긴다. - 스레드를 생성할 때 저장해뒀던 커널 스택 공간을 가리키는 rsp값 */
	/* Now we are in the kernel stack */
	push $(SEL_UDSEG)      /* if->ss */
	push %rbx              /* if->rsp */
	push %r11              /* if->eflags */
	push $(SEL_UCSEG)      /* if->cs */
	push %rcx              /* if->rip */
	subq $16, %rsp         /* skip error_code, vec_no */
	push $(SEL_UDSEG)      /* if->ds */
	push $(SEL_UDSEG)      /* if->es */
	push %rax			   /* 레지스터에 값 push */
	movq temp1(%rip), %rbx
	push %rbx
	pushq $0
	push %rdx
	push %rbp
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	pushq $0 /* skip r11 */
	movq temp2(%rip), %r12
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, %rdi			

check_intr:
	btsq $9, %r11          /* Check whether we recover the interrupt */
	jnb no_sti
	sti                    /* restore interrupt */
no_sti:
	movabs $syscall_handler, %r12
	call *%r12
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp
	popq %rcx              /* if->rip */
	addq $8, %rsp
	popq %r11              /* if->eflags */
	popq %rsp              /* if->rsp */
	sysretq

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0
