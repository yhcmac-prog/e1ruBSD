/*	$e1ruBSD: locore.S,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * locore.S - amd64 bootstrap.
 *
 * On entry from stage2:
 *   - Long mode active, identity page table (4GB, 2MB pages) at CR3=0x8C000
 *   - RDI = bootinfo physical address (0x6000)
 *   - Kernel loaded at physical 0x100000
 *
 * This code:
 *   1. Builds a new kernel page table (identity + DMAP 1GB)
 *   2. Switches CR3
 *   3. Jumps to the VMA of x86_boot
 *   4. Provides ISR stubs, swtch, trapret for C code
 *
 * DMAP = 0xFFFFFFFF80000000 maps physical 0..1GB via PML4[511]->PDPT[510]->PD
 */

.intel_syntax noprefix
.code64
.section .text

.extern x86_boot

/* ================================================================
 * BOOTSTRAP: runs at physical address before VMA jump
 * ================================================================ */
.globl kern_entry
kern_entry:
    /* Disable interrupts */
    cli

    /*
     * Enable SSE before any C code runs.  Even with -mno-sse clang lowers
     * small memory-clearing loops to SSE moves (movaps); executing one
     * with CR0.EM set raises #NM, and before cpu_init() installs an IDT
     * that is a triple fault.  fpu_init() later still does fninit/ldmxcsr.
     */
    mov rax, cr0
    btr rax, 2                  # clear EM (no FPU emulation)
    bts rax, 1                  # set MP
    mov cr0, rax
    mov rax, cr4
    bts rax, 9                  # OSFXSR
    bts rax, 10                 # OSXMMEXCPT
    mov cr4, rax

    /* ---- zero 0x76000-0x7BFFF (6 pages for our new tables) ---- */
    mov edi, 0x76000
    xor eax, eax
    mov ecx, 6144
    rep stosd

    /* ---- PML4 @ 0x76000 ----
     * [0]   -> identity PDPT (0x77003)
     * [511] -> high PDPT (0x79003)
     */
    mov rax, 0x76000
    mov qword ptr [rax],         0x0000000000077003   # PML4[0]
    mov qword ptr [rax + 0xFF8], 0x0000000000079007   # PML4[511] +USER

    /* ---- identity PDPT @ 0x77000: [0] -> PD @ 0x78000 ---- */
    mov rax, 0x77000
    mov qword ptr [rax], 0x0000000000078007

    /* ---- identity PD @ 0x78000: 512 x 2MB pages (phys 0..1GB) ---- */
    mov rax, 0x87                       # phys 0 + P|RW|USER|PS
    mov rdi, 0x78000
    mov ecx, 512
1:
    mov qword ptr [rdi], rax
    add rax, 0x200000
    add rdi, 8
    dec ecx
    jnz 1b

    /* ---- high PDPT @ 0x79000: [510] -> identity PD @ 0x78003 ----
     * DMAP_BASE = 0xFFFFFFFF80000000
     *   PML4 index = 511 (bit 39)
     *   PDPT index = 510 (bits 38:30)
     *   offset = 510 * 8 = 4080 = 0xFF0
     */
    mov rax, 0x79000
    mov qword ptr [rax + 0xFF0], 0x0000000000078007

    /* ---- switch CR3 ---- */
    mov eax, 0x76000
    mov cr3, rax            # flush TLB automatically

    /* ---- set up kernel stack (physical, before VMA jump) ----
     * Use a temp stack at DMAP + 64MB, 4KB top-down
     */
    mov rax, 0xFFFFFFFF84000000  # DMAP + 64MB
    mov rsp, rax

    /* ---- jump to VMA of x86_boot ----
     * We are currently at physical 0x100000+offset.
     * x86_boot's VMA = KERNEL_BASE + x86_boot_offset
     * = 0xFFFFFFFF80100000 + offset
     *
     * Use "mov rax, offset SYMBOL" to get a 64-bit immediate,
     * NOT lea which is rip-relative!
     */
    mov rax, offset vma_probe   # 64-bit absolute VMA address
    jmp rax                     # now executing in VMA!


/*
 * vma_probe: minimal VMA test.
 * Called via jmp from phys context. Runs with identity + DMAP mapped.
 * If we get here, rip-relative addresses work correctly.
 */
vma_probe:
    /* Jump to x86_boot in VMA context */
    mov rax, offset x86_boot
    jmp rax


/* ================================================================
 * ISR stubs and trap handling
 * ================================================================ */

/* This macro generates one ISR stub.
 * Each stub makes the hardware frame look like a trapframe, then jumps
 * to the common alltraps entry.
 *
 * The CPU pushes an error code for vectors 8, 10-14, 17, 21, 29, 30.
 * Those stubs only push the vector; every other stub pushes a dummy 0
 * first so that the frame always reads [vec][err][rip] upward.
 */
.macro ISR num, haserr=0
    .global isr_stub_\num
    .type isr_stub_\num, @function
isr_stub_\num:
    .if \haserr == 0
    push 0
    .endif
    push \num
    jmp alltraps
.endm

/* Generate all 256 ISR stubs */
ISR 0
ISR 1
ISR 2
ISR 3
ISR 4
ISR 5
ISR 6
ISR 7
ISR 8, 1
ISR 9
ISR 10, 1
ISR 11, 1
ISR 12, 1
ISR 13, 1
ISR 14, 1
ISR 15
ISR 16
ISR 17, 1
ISR 18
ISR 19
ISR 20
ISR 21, 1
ISR 22
ISR 23
ISR 24
ISR 25
ISR 26
ISR 27
ISR 28
ISR 29, 1
ISR 30, 1
ISR 31
ISR 32
ISR 33
ISR 34
ISR 35
ISR 36
ISR 37
ISR 38
ISR 39
ISR 40
ISR 41
ISR 42
ISR 43
ISR 44
ISR 45
ISR 46
ISR 47
ISR 48
ISR 49
ISR 50
ISR 51
ISR 52
ISR 53
ISR 54
ISR 55
ISR 56
ISR 57
ISR 58
ISR 59
ISR 60
ISR 61
ISR 62
ISR 63
ISR 64
ISR 65
ISR 66
ISR 67
ISR 68
ISR 69
ISR 70
ISR 71
ISR 72
ISR 73
ISR 74
ISR 75
ISR 76
ISR 77
ISR 78
ISR 79
ISR 80
ISR 81
ISR 82
ISR 83
ISR 84
ISR 85
ISR 86
ISR 87
ISR 88
ISR 89
ISR 90
ISR 91
ISR 92
ISR 93
ISR 94
ISR 95
ISR 96
ISR 97
ISR 98
ISR 99
ISR 100
ISR 101
ISR 102
ISR 103
ISR 104
ISR 105
ISR 106
ISR 107
ISR 108
ISR 109
ISR 110
ISR 111
ISR 112
ISR 113
ISR 114
ISR 115
ISR 116
ISR 117
ISR 118
ISR 119
ISR 120
ISR 121
ISR 122
ISR 123
ISR 124
ISR 125
ISR 126
ISR 127
ISR 128
ISR 129
ISR 130
ISR 131
ISR 132
ISR 133
ISR 134
ISR 135
ISR 136
ISR 137
ISR 138
ISR 139
ISR 140
ISR 141
ISR 142
ISR 143
ISR 144
ISR 145
ISR 146
ISR 147
ISR 148
ISR 149
ISR 150
ISR 151
ISR 152
ISR 153
ISR 154
ISR 155
ISR 156
ISR 157
ISR 158
ISR 159
ISR 160
ISR 161
ISR 162
ISR 163
ISR 164
ISR 165
ISR 166
ISR 167
ISR 168
ISR 169
ISR 170
ISR 171
ISR 172
ISR 173
ISR 174
ISR 175
ISR 176
ISR 177
ISR 178
ISR 179
ISR 180
ISR 181
ISR 182
ISR 183
ISR 184
ISR 185
ISR 186
ISR 187
ISR 188
ISR 189
ISR 190
ISR 191
ISR 192
ISR 193
ISR 194
ISR 195
ISR 196
ISR 197
ISR 198
ISR 199
ISR 200
ISR 201
ISR 202
ISR 203
ISR 204
ISR 205
ISR 206
ISR 207
ISR 208
ISR 209
ISR 210
ISR 211
ISR 212
ISR 213
ISR 214
ISR 215
ISR 216
ISR 217
ISR 218
ISR 219
ISR 220
ISR 221
ISR 222
ISR 223
ISR 224
ISR 225
ISR 226
ISR 227
ISR 228
ISR 229
ISR 230
ISR 231
ISR 232
ISR 233
ISR 234
ISR 235
ISR 236
ISR 237
ISR 238
ISR 239
ISR 240
ISR 241
ISR 242
ISR 243
ISR 244
ISR 245
ISR 246
ISR 247
ISR 248
ISR 249
ISR 250
ISR 251
ISR 252
ISR 253
ISR 254
ISR 255

/*
 * Common trap entry: all stubs jump here after pushing vec.
 *
 * Stack on entry (after CPU pushes + stub pushes vec):
 *   vec  <- [rsp]     (pushed by stub)
 *   err  <- [rsp+8]   (0 for no-error vectors, real for some)
 *   rip  <- [rsp+16]
 *   cs   <- [rsp+24]
 *   rflags <- [rsp+32]
 *   rsp  <- [rsp+40]  (only for user->kernel)
 *   ss   <- [rsp+48]  (only for user->kernel)
 *
 * trapframe layout (trap.h):
 *   r15 r14 r13 r12 r11 r10 r9 r8 rdi rsi rbp rbx rdx rcx rax vec err rip cs rflags rsp ss
 */
.extern trap
.extern curproc

.globl alltraps
alltraps:
    /* Save all GP registers in trapframe order */
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    /* Move vec and err to match trapframe layout */
    /* Currently stack: r15..r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, vec, err, rip... */
    /* We want: r15, r14..r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, vec, err, rip, cs, rflags, rsp, ss */
    /* This is actually already correct! vec and err are right after rax */

    /* RDI = &tf, RSI = curproc (NULL for early boot) */
    mov rdi, rsp
    mov rax, offset curproc
    mov rsi, [rax]

    /* Call trap(tf, curproc) */
    mov rax, offset trap
    call rax

    /* Fall through to trapret */

.globl trapret
trapret:
    /* Restore GP registers */
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    /* Skip vec and err (they are above rax now) */
    add rsp, 16

    /* iretq restores rip, cs, rflags, rsp, ss */
    iretq


/* ================================================================
 * isr_syscall - syscall entry (int 0x80)
 *
 * x86_boot sets up IDT vector 0x80 pointing here.
 * ================================================================ */
.extern syscall

.globl isr_syscall
isr_syscall:
    /*
     * Build a trapframe identical to alltraps: the hardware frame is
     * followed by err, vec, then the 15 GP registers pushed in reverse
     * (so r15 ends up at the lowest address and rdi points at it).
     */
    push 0
    push 0x80

    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    mov rax, offset curproc
    mov rsi, [rax]

    /* Call syscall handler — it modifies tf->rax with retval */
    mov rax, offset syscall
    call rax

    jmp trapret


/* ================================================================
 * ISR table — cpu.c loads IDT from these pointers
 * ================================================================ */

.section .rodata
.align 8
.globl isr_table
isr_table:
    .quad isr_stub_0
    .quad isr_stub_1
    .quad isr_stub_2
    .quad isr_stub_3
    .quad isr_stub_4
    .quad isr_stub_5
    .quad isr_stub_6
    .quad isr_stub_7
    .quad isr_stub_8
    .quad isr_stub_9
    .quad isr_stub_10
    .quad isr_stub_11
    .quad isr_stub_12
    .quad isr_stub_13
    .quad isr_stub_14
    .quad isr_stub_15
    .quad isr_stub_16
    .quad isr_stub_17
    .quad isr_stub_18
    .quad isr_stub_19
    .quad isr_stub_20
    .quad isr_stub_21
    .quad isr_stub_22
    .quad isr_stub_23
    .quad isr_stub_24
    .quad isr_stub_25
    .quad isr_stub_26
    .quad isr_stub_27
    .quad isr_stub_28
    .quad isr_stub_29
    .quad isr_stub_30
    .quad isr_stub_31
    .quad isr_stub_32
    .quad isr_stub_33
    .quad isr_stub_34
    .quad isr_stub_35
    .quad isr_stub_36
    .quad isr_stub_37
    .quad isr_stub_38
    .quad isr_stub_39
    .quad isr_stub_40
    .quad isr_stub_41
    .quad isr_stub_42
    .quad isr_stub_43
    .quad isr_stub_44
    .quad isr_stub_45
    .quad isr_stub_46
    .quad isr_stub_47
    .quad isr_stub_48
    .quad isr_stub_49
    .quad isr_stub_50
    .quad isr_stub_51
    .quad isr_stub_52
    .quad isr_stub_53
    .quad isr_stub_54
    .quad isr_stub_55
    .quad isr_stub_56
    .quad isr_stub_57
    .quad isr_stub_58
    .quad isr_stub_59
    .quad isr_stub_60
    .quad isr_stub_61
    .quad isr_stub_62
    .quad isr_stub_63
    .quad isr_stub_64
    .quad isr_stub_65
    .quad isr_stub_66
    .quad isr_stub_67
    .quad isr_stub_68
    .quad isr_stub_69
    .quad isr_stub_70
    .quad isr_stub_71
    .quad isr_stub_72
    .quad isr_stub_73
    .quad isr_stub_74
    .quad isr_stub_75
    .quad isr_stub_76
    .quad isr_stub_77
    .quad isr_stub_78
    .quad isr_stub_79
    .quad isr_stub_80
    .quad isr_stub_81
    .quad isr_stub_82
    .quad isr_stub_83
    .quad isr_stub_84
    .quad isr_stub_85
    .quad isr_stub_86
    .quad isr_stub_87
    .quad isr_stub_88
    .quad isr_stub_89
    .quad isr_stub_90
    .quad isr_stub_91
    .quad isr_stub_92
    .quad isr_stub_93
    .quad isr_stub_94
    .quad isr_stub_95
    .quad isr_stub_96
    .quad isr_stub_97
    .quad isr_stub_98
    .quad isr_stub_99
    .quad isr_stub_100
    .quad isr_stub_101
    .quad isr_stub_102
    .quad isr_stub_103
    .quad isr_stub_104
    .quad isr_stub_105
    .quad isr_stub_106
    .quad isr_stub_107
    .quad isr_stub_108
    .quad isr_stub_109
    .quad isr_stub_110
    .quad isr_stub_111
    .quad isr_stub_112
    .quad isr_stub_113
    .quad isr_stub_114
    .quad isr_stub_115
    .quad isr_stub_116
    .quad isr_stub_117
    .quad isr_stub_118
    .quad isr_stub_119
    .quad isr_stub_120
    .quad isr_stub_121
    .quad isr_stub_122
    .quad isr_stub_123
    .quad isr_stub_124
    .quad isr_stub_125
    .quad isr_stub_126
    .quad isr_stub_127
    .quad isr_stub_128
    .quad isr_stub_129
    .quad isr_stub_130
    .quad isr_stub_131
    .quad isr_stub_132
    .quad isr_stub_133
    .quad isr_stub_134
    .quad isr_stub_135
    .quad isr_stub_136
    .quad isr_stub_137
    .quad isr_stub_138
    .quad isr_stub_139
    .quad isr_stub_140
    .quad isr_stub_141
    .quad isr_stub_142
    .quad isr_stub_143
    .quad isr_stub_144
    .quad isr_stub_145
    .quad isr_stub_146
    .quad isr_stub_147
    .quad isr_stub_148
    .quad isr_stub_149
    .quad isr_stub_150
    .quad isr_stub_151
    .quad isr_stub_152
    .quad isr_stub_153
    .quad isr_stub_154
    .quad isr_stub_155
    .quad isr_stub_156
    .quad isr_stub_157
    .quad isr_stub_158
    .quad isr_stub_159
    .quad isr_stub_160
    .quad isr_stub_161
    .quad isr_stub_162
    .quad isr_stub_163
    .quad isr_stub_164
    .quad isr_stub_165
    .quad isr_stub_166
    .quad isr_stub_167
    .quad isr_stub_168
    .quad isr_stub_169
    .quad isr_stub_170
    .quad isr_stub_171
    .quad isr_stub_172
    .quad isr_stub_173
    .quad isr_stub_174
    .quad isr_stub_175
    .quad isr_stub_176
    .quad isr_stub_177
    .quad isr_stub_178
    .quad isr_stub_179
    .quad isr_stub_180
    .quad isr_stub_181
    .quad isr_stub_182
    .quad isr_stub_183
    .quad isr_stub_184
    .quad isr_stub_185
    .quad isr_stub_186
    .quad isr_stub_187
    .quad isr_stub_188
    .quad isr_stub_189
    .quad isr_stub_190
    .quad isr_stub_191
    .quad isr_stub_192
    .quad isr_stub_193
    .quad isr_stub_194
    .quad isr_stub_195
    .quad isr_stub_196
    .quad isr_stub_197
    .quad isr_stub_198
    .quad isr_stub_199
    .quad isr_stub_200
    .quad isr_stub_201
    .quad isr_stub_202
    .quad isr_stub_203
    .quad isr_stub_204
    .quad isr_stub_205
    .quad isr_stub_206
    .quad isr_stub_207
    .quad isr_stub_208
    .quad isr_stub_209
    .quad isr_stub_210
    .quad isr_stub_211
    .quad isr_stub_212
    .quad isr_stub_213
    .quad isr_stub_214
    .quad isr_stub_215
    .quad isr_stub_216
    .quad isr_stub_217
    .quad isr_stub_218
    .quad isr_stub_219
    .quad isr_stub_220
    .quad isr_stub_221
    .quad isr_stub_222
    .quad isr_stub_223
    .quad isr_stub_224
    .quad isr_stub_225
    .quad isr_stub_226
    .quad isr_stub_227
    .quad isr_stub_228
    .quad isr_stub_229
    .quad isr_stub_230
    .quad isr_stub_231
    .quad isr_stub_232
    .quad isr_stub_233
    .quad isr_stub_234
    .quad isr_stub_235
    .quad isr_stub_236
    .quad isr_stub_237
    .quad isr_stub_238
    .quad isr_stub_239
    .quad isr_stub_240
    .quad isr_stub_241
    .quad isr_stub_242
    .quad isr_stub_243
    .quad isr_stub_244
    .quad isr_stub_245
    .quad isr_stub_246
    .quad isr_stub_247
    .quad isr_stub_248
    .quad isr_stub_249
    .quad isr_stub_250
    .quad isr_stub_251
    .quad isr_stub_252
    .quad isr_stub_253
    .quad isr_stub_254
    .quad isr_stub_255

/* isr_syscall is its own label (not in the table — CPU uses it directly) */
/* Already defined above */


/* ================================================================
 * swtch(struct ctx *old, struct ctx *new)
 *
 * Save current context, load new context. Called with interrupts off.
 * ctx layout must match sched.c: r15 r14 r13 r12 r11 r10 r9 r8 rdi rsi rbp rbx rsp
 * ================================================================ */
.globl swtch
.type swtch, @function
swtch:
    /* Save callee-saved registers into *old */
    mov [rdi],    r15
    mov [rdi+8],  r14
    mov [rdi+16], r13
    mov [rdi+24], r12
    mov [rdi+32], r11
    mov [rdi+40], r10
    mov [rdi+48], r9
    mov [rdi+56], r8
    mov [rdi+64], rdi
    mov [rdi+72], rsi
    mov [rdi+80], rbp
    mov [rdi+88], rbx
    mov [rdi+96], rsp

    /* Load new context from *new.  Use rax to hold the pointer so we
     * can reload rdi/rsi early without losing it for rbp/rbx/rsp. */
    mov rax, rsi
    mov r15, [rax]
    mov r14, [rax+8]
    mov r13, [rax+16]
    mov r12, [rax+24]
    mov r11, [rax+32]
    mov r10, [rax+40]
    mov r9,  [rax+48]
    mov r8,  [rax+56]
    mov rdi, [rax+64]
    mov rsi, [rax+72]
    mov rbp, [rax+80]
    mov rbx, [rax+88]
    mov rsp, [rax+96]

    ret
