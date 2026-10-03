// Larger stacks for the threads DSWiFi creates internally.
//
// DSWiFi (BlocksDS 1.24) runs lwIP's tcpip thread on a 4 KB stack and its update
// thread on 8 KB. On real Wi-Fi, retransmitted and out-of-order TCP segments make
// those call chains deeper; the tcpip stack overflowed downwards into the update
// thread's context (allocated just below it), and resuming that thread crashed in
// the cothread scheduler, always at the same heap address. The emulator's lossless
// network never went that deep, which is why it only happened on hardware.
//
// The library is prebuilt, so the Makefile links with --wrap=cothread_create and
// every thread gets at least NET_STACK bytes. Stacks are painted so their deepest
// use can be shown in the stream details.
#include <nds.h>
#include <malloc.h>
#include <stdlib.h>
#include "hq_player.h"

#define NET_STACK (32 * 1024)
#define PAINT 0xA5A5A5A5u
#define MAX_TRACKED 4

static struct { uint32_t *base; unsigned size; } tracked[MAX_TRACKED];
static int tracked_count;
static cothread_t tracked_ids[MAX_TRACKED];
cothread_t hq_thread_id(int i) { return i >= 0 && i < tracked_count ? tracked_ids[i] : 0; }

cothread_t __real_cothread_create(cothread_entrypoint_t entrypoint, void *arg,
                                  size_t stack_size, unsigned int flags);

cothread_t __wrap_cothread_create(cothread_entrypoint_t entrypoint, void *arg,
                                  size_t stack_size, unsigned int flags)
{
#ifdef NET_STACK_PASSTHROUGH
    // Layout probe: same allocations as the plain library, only record the thread ids.
    cothread_t probe = __real_cothread_create(entrypoint, arg, stack_size, flags);
    if (tracked_count < MAX_TRACKED) tracked_ids[tracked_count++] = probe;
    return probe;
#endif
    size_t size = stack_size < NET_STACK ? NET_STACK : (stack_size + 7) & ~(size_t)7;
    uint32_t *stack = memalign(8, size);
    if (stack == NULL)
        return __real_cothread_create(entrypoint, arg, stack_size, flags);
    for (unsigned i = 0; i < size / 4; i++)
        stack[i] = PAINT;
    // A manual stack is not freed when a detached thread ends; only the short-lived
    // Wi-Fi connect thread ends, so that costs one stack once.
    cothread_t thread = cothread_create_manual(entrypoint, arg, stack, size, flags);
    if (thread < 0) {
        free(stack);
        return thread;
    }
    if (tracked_count < MAX_TRACKED) {
        tracked_ids[tracked_count] = thread;
        tracked[tracked_count].base = stack;
        tracked[tracked_count].size = size;
        tracked_count++;
    }
    return thread;
}

// Canary after large aligned allocations (DSWiFi's shared Wifi_MainStruct is the only
// one): hardware crashes zeroed more than 33 KB of the heap right after that struct,
// so an 8 KB painted zone there shows whether something writes past its end, and what.
#define CANARY_BYTES (8 * 1024)
#define CANARY 0x5A5A5A5Au
static uint32_t *canary;
static void *canary_owner;

void *__real_aligned_alloc(size_t alignment, size_t size);

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    if (size < 16 * 1024 || canary != NULL)
        return __real_aligned_alloc(alignment, size);
    size_t body = (size + 31) & ~(size_t)31;
    uint8_t *block = __real_aligned_alloc(alignment, body + CANARY_BYTES);
    if (block == NULL)
        return NULL;
    canary = (uint32_t *)(block + body);
    canary_owner = block;
    for (unsigned i = 0; i < CANARY_BYTES / 4; i++)
        canary[i] = CANARY;
    return block;
}

uintptr_t hq_canary_owner(void) { return (uintptr_t)canary_owner; }

// Number of damaged canary words; the first damaged word index and value.
unsigned hq_canary_check(unsigned *first, uint32_t *value)
{
    unsigned damaged = 0;
    if (canary == NULL)
        return 0;
    for (unsigned i = 0; i < CANARY_BYTES / 4; i++) {
        if (canary[i] != CANARY) {
            if (!damaged) { *first = i; *value = canary[i]; }
            damaged++;
        }
    }
    return damaged;
}

int hq_net_stack_count(void) { return tracked_count; }

// Deepest use of tracked stack i, in bytes.
unsigned hq_net_stack_peak(int i)
{
    if (i < 0 || i >= tracked_count || tracked[i].base == NULL)
        return 0;
    unsigned words = tracked[i].size / 4, used = 0;
    while (used < words && tracked[i].base[used] == PAINT)
        used++;
    return (words - used) * 4;
}
