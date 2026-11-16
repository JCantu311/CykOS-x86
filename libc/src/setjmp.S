.section .text
.code32

.globl setjmp
.type setjmp, @function
setjmp:
    movl 4(%esp), %edx
    movl %ebx, 0(%edx)
    movl %esi, 4(%edx)
    movl %edi, 8(%edx)
    movl %ebp, 12(%edx)
    leal 4(%esp), %ecx
    movl %ecx, 16(%edx)
    movl (%esp), %ecx
    movl %ecx, 20(%edx)
    xorl %eax, %eax
    ret
.size setjmp, .-setjmp

.globl longjmp
.type longjmp, @function
longjmp:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    testl %eax, %eax
    jnz 1f
    movl $1, %eax
1:
    movl 0(%edx), %ebx
    movl 4(%edx), %esi
    movl 8(%edx), %edi
    movl 12(%edx), %ebp
    movl 20(%edx), %ecx
    movl 16(%edx), %esp
    jmp *%ecx
.size longjmp, .-longjmp

.section .note.GNU-stack,"",@progbits
