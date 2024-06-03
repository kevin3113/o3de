
#ifndef __CSTACK_TRACE_H__
#define __CSTACK_TRACE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)

#include <execinfo.h>
static inline void print_stack_impl(const char *fun, const char *file, int line, const char *info)
{
    void *stack[32];
    char **msg;
    int sz = backtrace(stack, 32);
    msg = backtrace_symbols(stack, sz);
    printf("[bt] #0 thread %d %s:%s:%d info [%s]\n", (int)gettid(), fun, file, line, info);
    for (int i = 1; i < sz; i++) {
        printf("[bt] #%d %s\n", i, msg[i]);
    }
}

#define print_stack() print_stack_impl(__FUNCTION__, __FILE__, __LINE__, "None")

#endif
