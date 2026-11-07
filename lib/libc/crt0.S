# e1ruBSD userland crt0
# _start receives rsp -> [argc][argv...][NULL][envp NULL][auxv]
.intel_syntax noprefix
.text
.global _start
_start:
    xor rbp, rbp
    mov rdi, rsp                  # __libc_start(stack)
    and rsp, -16
    call __libc_start
    mov rdi, rax                  # main() return value
    mov rax, 1                    # SYS_exit
    mov rsi, 0
    mov rdx, 0
    int 0x80
1:  jmp 1b
