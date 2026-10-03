// Larger stacks for the threads DSWiFi creates internally.
//
// DSWiFi (BlocksDS 1.24) runs lwIP's tcpip thread on a 4 KB stack and its update
// thread on 8 KB. The library is prebuilt, so the Makefile links with
// --wrap=cothread_create and every thread gets at least NET_STACK bytes. (Measured
// use on hardware stayed far below this; the margin costs little memory.)
#include <nds.h>
#include <malloc.h>
#include <stdlib.h>

#define NET_STACK (32 * 1024)

cothread_t __real_cothread_create(cothread_entrypoint_t entrypoint, void *arg,
                                  size_t stack_size, unsigned int flags);

cothread_t __wrap_cothread_create(cothread_entrypoint_t entrypoint, void *arg,
                                  size_t stack_size, unsigned int flags)
{
    size_t size = stack_size < NET_STACK ? NET_STACK : (stack_size + 7) & ~(size_t)7;
    void *stack = memalign(8, size);
    if (stack == NULL)
        return __real_cothread_create(entrypoint, arg, stack_size, flags);
    // A manual stack is not freed when a detached thread ends; only the short-lived
    // Wi-Fi connect thread ends, so that costs one stack once.
    cothread_t thread = cothread_create_manual(entrypoint, arg, stack, size, flags);
    if (thread < 0)
        free(stack);
    return thread;
}
