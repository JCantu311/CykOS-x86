#ifndef _SETJMP_H
#define _SETJMP_H

/* Saved i386 callee-saved registers, post-return stack pointer, and EIP. */
typedef int jmp_buf[6];

int setjmp(jmp_buf environment) __attribute__((returns_twice));
void longjmp(jmp_buf environment, int value) __attribute__((noreturn));

#endif
