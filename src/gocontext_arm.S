#include <sys/syscall.h>

#define g_m 0x30
#define m_g0 0x0
#define m_gsignal 0x50
#define g_sched_sp 0x38 //0x40 in 8
#define g_sched_pc 0x40
#define g_sched_ret 0x58
#define g_sched_bp 0x68
#define g_sched_lr 0x60
#define m_g0_sched_sp 0x38 //0x40 in 8

/*
 * Go 17+ will pass input params and return values in registers.
 * This macro will move us onto the system stack (like asmcgocall) and push registers onto it for our usage,
 * and so that we can restore them after calling our c_func.
 * We must not push anything onto the go stack, else the stack checker will create a panic.
 *
 * From: https://go.googlesource.com/go/+/refs/heads/dev.regabi/src/cmd/compile/internal-abi.md
 * The following registers are used for integer arguments and results:
 * RAX, RBX, RCX, RDI, RSI, R8, R9, R10, R11
 * Note that X15 is defined to be always a fixed value of zero
 *
 * Register usage with the Go ARM64 ABI:
 *    x28 == g
 *    x16 == scratch (stack check)
 *    x17 == scratch (ugh. defined as scratch, but not always)
 *    x18 == ARM64 platform register, unused in the Go toolchain. is it safe to use as scratch?
 *    sp == stack
 *    x29 == frame pointer
 *    x30 == link register
 *    x8 == syscall number
 *    x0-x6 == params
 *    x0 == return value from syscalls
 *    xzr == zero register
 */
.macro interpose_system_stack a_func, c_func, s_func
.global \a_func
.hidden \c_func
\a_func:
    // Is g valid?
    cbz  x28, 1f

    // Is this g m->gsignal? If so, already on the system stack
    ldr	 x17, [x28, #g_m]                          // g_m -> x18
    ldr	 x17, [x17, #m_gsignal]                    // m_gsignal -> x17
    cmp	 x28, x17                                  // is this g m_gsignal?
    b.eq 1f                                        // already on the sys stack

    // Is this g g->m->g0 (new thread)? If so, already on the system stack
    ldr	 x17, [x28, #g_m]                          // g_m -> x17
    ldr  x17, [x17]                                // m_g0 -> x17
    cmp  x28, x17                                  // is this g m->g0?
    b.eq 1f                                        // already on the sys stack

    // Else switch to system stack
    // Set g state to use system stack
    mov  x10, sp                                   // save caller stack in x10. should be safe across C ABI
    adrp x17, go_systemstack_switch
    str  x17, [x28, #g_sched_pc]
    str  x10, [x28, #g_sched_sp]                   // sp -> g_sched_sp
    str  x29, [x28, #g_sched_bp]                   // fp -> g_sched_bp
    str  x30, [x28, #g_sched_lr]                   // save link register -> g_sched_lr
    str  xzr, [x28, #g_sched_ret]                  // 0 -> g_sched_ret

    mov  x9, x28                                   // save current g in x9. should be safe across C ABI
    ldr	 x17, [x28, #g_m]                          // g_m -> x17
    ldr  x17, [x17]                                // m_g0 -> x17
    mov  x28, x17                                  // m_g0 becomes the current g

    // add iscgo check here?

    ldr	 x17, [x28, #g_sched_sp]                  // g_sched_sp -> x17
    mov  sp, x17                                  // bang: change to the system stack
    ldr  x29, [x28, #g_sched_bp]                  // g_sched_bp -> frame pointer

    str  x9,  [sp, #-0x10]!
    str  x10, [sp, #-0x08]

    ldr  x17, =\c_func                            // pass address of c_func to s_func

    // link register is saved in the g
    bl   \s_func

    ldr  x28, [sp, #0x10]
    ldr  x30, [x28, #g_sched_lr]                   // restore link register from saved g
    ldr  x29, [x28, #g_sched_bp]                   // restore fp from saved g
    ldr  x18, [x28, #g_sched_sp]                   // restore stack from saved g
    mov  sp, x18

    br   x17

1:
    // The current g is using the system stack, don't switch stacks
    mov  x10, sp                                 // save g stack for use in the C func
    ldr  x17, =\c_func                           // pass address of c_func to s_func

    mov  x9, x30                                 // save the link register
    bl   \s_func
    mov  x30, x9                                 // restore the link register
    add  sp, sp, #16
    br   x17
.endm

.macro syscall_filter a_func, b_func, retptr
.global \a_func
\a_func:
    cmp  x8, #SYS_write
    b.eq 2f
    cmp  x8, #SYS_openat
    b.eq 2f
    cmp  x8, #SYS_unlinkat
    b.eq 2f
    cmp  x8, #SYS_getdents64
    b.eq 2f
    cmp  x8, #SYS_socket
    b.eq 2f
    cmp  x8, #SYS_accept4
    b.eq 2f
    cmp  x8, #SYS_read
    b.eq 2f
    cmp  x8, #SYS_close
    b.eq 2f

    ldr  x17, =\retptr
    ldr  x17, [x17]
    br   x17
2:
    b    \b_func

.endm

.text
c_handler:
    /*
     * Inputs:
     * x17 is the c_func address
     * x10 is the g stack
     *
     * Returns:
     * x17 is the trampoline address
    */
    // Save regs on the stack
    stp  x0, x1, [sp, #-16]!                     // “!” implies adding the offset after the load or store
    stp  x2, x3, [sp, #-16]!
    stp  x4, x5, [sp, #-16]!
    stp  x6, x7, [sp, #-16]!
    stp  x8, x9, [sp, #-16]!
    stp  x10, x11, [sp, #-16]!
    stp  x29, x30, [sp, #-16]!

    mov  x0, sp
    blr  x17
    mov  x17, x0                                 // return value

    // Restore regs
    sub  sp, sp, #16                             // TODO: why is this needed?
    ldp  x29, x30, [sp, #16]!
    ldp  x10, x11, [sp, #16]!
    ldp  x8, x9, [sp, #16]!
    ldp  x6, x7, [sp, #16]!
    ldp  x4, x5, [sp, #16]!
    ldp  x2, x3, [sp, #16]!
    ldp  x0, x1, [sp, #16]!
    ret
 
c_syscall_handler:
    /*
     * Inputs:
     * x17 is c_func address
     * x10 is the g stack
     *
     * Returns:
     * x17 is the trampoline address
    */

    // Save regs on the stack
    stp  x0, x1, [sp, #-16]!                     // “!” implies adding the offset after the load or store
    stp  x2, x3, [sp, #-16]!
    stp  x4, x5, [sp, #-16]!
    stp  x6, x7, [sp, #-16]!
    stp  x8, x9, [sp, #-16]!
    stp  x10, x11, [sp, #-16]!
    stp  x29, x30, [sp, #-16]!

    svc  #0                                      // system call - syscall number is in x8

    mov  x18, x0                                 // TODO: push to stack
    str  x0, [sp, #0]

    mov  x0, sp
    blr  x17
    mov  x17, x0                                 // return value
    add  x17, x17, #4                            // don't re-execute the syscall

    // Restore regs
    sub  sp, sp, #16                             // TODO: why is this needed?
    ldp  x29, x30, [sp, #16]!
    ldp  x10, x11, [sp, #16]!
    ldp  x8, x9, [sp, #16]!
    ldp  x6, x7, [sp, #16]!
    ldp  x4, x5, [sp, #16]!
    ldp  x2, x3, [sp, #16]!
    ldp  x0, x1, [sp, #16]!
    mov  x0, x18
    ret


    interpose_system_stack hook_syscall                      go_syscall              c_syscall_handler
    interpose_system_stack hook_rawsyscall                   go_rawsyscall           c_syscall_handler
    interpose_system_stack hook_syscall6                     go_syscall6             c_syscall_handler
    interpose_system_stack go_hook_reg_tls_server_read       go_tls_server_read      c_handler
    interpose_system_stack go_hook_reg_tls_server_write      go_tls_server_write     c_handler
    interpose_system_stack go_hook_reg_tls_client_read       go_tls_client_read      c_handler
    interpose_system_stack go_hook_reg_tls_client_write      go_tls_client_write     c_handler
    interpose_system_stack go_hook_reg_http2_server_read     go_http2_server_read    c_handler
    interpose_system_stack go_hook_reg_http2_server_write    go_http2_server_write   c_handler
    interpose_system_stack go_hook_reg_http2_server_preface  go_http2_server_preface c_handler
    interpose_system_stack go_hook_reg_http2_client_read     go_http2_client_read    c_handler
    interpose_system_stack go_hook_reg_http2_client_write    go_http2_client_write   c_handler
    interpose_system_stack go_hook_exit                      go_exit                 c_handler
    interpose_system_stack go_hook_die                       go_die                  c_handler
    interpose_system_stack go_hook_sighandler                go_sighandler           c_handler

    syscall_filter         go_hook_reg_syscall               hook_syscall            g_syscall_return
    syscall_filter         go_hook_reg_rawsyscall            hook_rawsyscall         g_rawsyscall_return
    syscall_filter         go_hook_reg_syscall6              hook_syscall6           g_syscall6_return

/*
.text
.global go_hook_reg_syscall
go_hook_reg_syscall:
    // Is g valid?
    cbz  x28, 1f

    // Is this g m->gsignal? If so, already on the system stack
    ldr	 x17, [x28, #g_m]                          // g_m -> x18
    ldr	 x17, [x17, #m_gsignal]                    // m_gsignal -> x17
    cmp	 x28, x17                                  // is this g m_gsignal?
    b.eq 1f                                        // already on the sys stack

    // Is this g g->m->g0 (new thread)? If so, already on the system stack
    ldr	 x17, [x28, #g_m]                          // g_m -> x17
    ldr  x17, [x17]                                // m_g0 -> x17
    cmp  x28, x17                                  // is this g m->g0?
    b.eq 1f                                        // already on the sys stack

    // Else switch to system stack
    // Set g state to use system stack
    mov  x10, sp                                   // save caller stack in x10. should be safe across C ABI
    adrp x17, go_systemstack_switch
    str  x17, [x28, #g_sched_pc]
    str	 x10, [x28, #g_sched_sp]                   // sp -> g_sched_sp
    str	 x29, [x28, #g_sched_bp]                   // fp -> g_sched_bp
    str	 x30, [x28, #g_sched_lr]                   // save link register -> g_sched_lr
    str	 xzr, [x28, #g_sched_ret]                  // 0 -> g_sched_ret

    mov  x9, x28                                   // save current g in x9. should be safe across C ABI
    ldr	 x17, [x28, #g_m]                          // g_m -> x17
    ldr  x17, [x17]                                // m_g0 -> x17
    mov  x28, x17                                  // m_g0 becomes the current g

    // add iscgo check here?

    ldr	 x17, [x28, #g_sched_sp]                  // g_sched_sp -> x17
    mov  sp, x17                                  // bang: change to the system stack
    ldr  x29, [x28, #g_sched_bp]                  // g_sched_bp -> frame pointer

    str	 x9,  [sp, #-0x10]!      // validate here
    str	 x10, [sp, #-0x08]
    //add	 sp, sp, #0x10
    ldr  x17, =go_syscall                            // pass address of c_func to s_func

    // link register is saved in the g
    bl   c_syscall_handler

    //mov  x28, x9                                 // restore caller g
    ldr	 x28, [sp, #0x10]     // validate here
    ldr  x30, [x28, #g_sched_lr]                   // restore link register from saved g
    ldr  x29, [x28, #g_sched_bp]                   // restore fp from saved g
    ldr  x18, [x28, #g_sched_sp]                   // restore stack from saved g
    mov  sp, x18

    // add iscgo check here?

    //mov  sp, x10                                 // restore caller stack
    //ldr	 x9, [x28, #8]  // validate here
    //mov  sp, x9
    br   x17

1:
    // The current g is using the system stack, don't switch stacks
    mov  x10, sp                                 // save g stack for use in the C func
    ldr  x17, =go_syscall                           // pass address of c_func to s_func

    mov  x9, x30                                 // save the link register
    bl   c_syscall_handler
    mov  x30, x9                                 // restore the link register
    br   x17
*/
