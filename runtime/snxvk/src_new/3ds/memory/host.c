/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* syscall, MAP_NORESERVE */
#endif
#include "3ds/memory/host.h"
#include "3ds/memory/ctr_memory.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sys/syscall.h>
#endif
#endif

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

/* the fault handler's: the space it watches and the pc it reports */
static struct {
    unsigned char* base;
    const uint32_t* pc;
    bool (*resolve)(void* context, uint32_t va);
    void* context;
#ifdef _WIN32
    void* handler;
#else
    struct sigaction segv, bus; /* the handlers before, which a fault elsewhere goes on to */
    stack_t stack, before_stack; /* the guest thread's alternate signal stack, and the one it had */
#endif
    /* Windows 10 1803's placeholder calls, looked up (an older Windows has none, and no flat space) */
#ifdef _WIN32
    PVOID(WINAPI* alloc2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
    PVOID(WINAPI* map3)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
    BOOL(WINAPI* unmap2)(HANDLE, PVOID, ULONG);
    int looked;
#endif
} g_host;

#ifdef _WIN32
static bool calls(void) {
    if (!g_host.looked) {
        HMODULE k = GetModuleHandleW(L"kernelbase.dll");
        g_host.looked = 1;
        if (k) {
            *(FARPROC*)&g_host.alloc2 = GetProcAddress(k, "VirtualAlloc2");
            *(FARPROC*)&g_host.map3 = GetProcAddress(k, "MapViewOfFile3");
            *(FARPROC*)&g_host.unmap2 = GetProcAddress(k, "UnmapViewOfFile2");
        }
    }
    return g_host.alloc2 && g_host.map3 && g_host.unmap2;
}
static DWORD protection(unsigned permissions) {
    return permissions & SNX_CTR_WRITE ? PAGE_READWRITE : permissions ? PAGE_READONLY : PAGE_NOACCESS;
}
bool ctr_host_flat(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize == 4096 && calls();
}
bool ctr_host_memory_new(CtrHostMemory* m, uint64_t size) {
    memset(m, 0, sizeof *m);
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, (DWORD)(size >> 32),
                                        (DWORD)size, NULL);
    if (!section) return false;
    m->view = MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)size);
    if (!m->view) {
        CloseHandle(section);
        return false;
    }
    m->handle = (intptr_t)section;
    m->size = size;
    return true;
}
void ctr_host_memory_free(CtrHostMemory* m) {
    if (m->view) UnmapViewOfFile(m->view);
    if (m->handle) CloseHandle((HANDLE)m->handle);
    memset(m, 0, sizeof *m);
}
unsigned char* ctr_host_reserve(void) {
    if (!calls()) return NULL;
    return g_host.alloc2(GetCurrentProcess(), NULL, (SIZE_T)(CTR_HOST_SPACE + CTR_HOST_GUARD),
                         MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
}
void ctr_host_release(unsigned char* base) {
    /* every page is a placeholder or a view of its own: views go first, then the placeholders together */
    for (uint64_t at = 0; at < CTR_HOST_SPACE; at += 4096) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(base + at, &info, sizeof info) && info.Type == MEM_MAPPED)
            g_host.unmap2(GetCurrentProcess(), base + at, MEM_PRESERVE_PLACEHOLDER);
    }
    VirtualFree(base, 0, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS);
    VirtualFree(base, 0, MEM_RELEASE);
}
/* Each guest page is a placeholder of its own when it is first mapped (split out of the reservation's), and a view
   of the section's page while it is mapped: no range ever spans two views, which Windows would refuse to protect or
   unmap whole */
bool ctr_host_map(const CtrHostMemory* m, unsigned char* base, uint32_t va, uint64_t offset, uint32_t size,
                  unsigned permissions) {
    for (uint32_t at = 0; at < size; at += 4096) {
        unsigned char* page = base + va + at;
        VirtualFree(page, 4096, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER); /* fails once it is split: harmless */
        if (!g_host.map3((HANDLE)m->handle, GetCurrentProcess(), page, offset + at, 4096, MEM_REPLACE_PLACEHOLDER,
                         protection(permissions), NULL, 0)) {
            if (at) ctr_host_unmap(base, va, at);
            return false;
        }
    }
    return true;
}
bool ctr_host_protect(unsigned char* base, uint32_t va, uint32_t size, unsigned permissions) {
    DWORD old;
    for (uint32_t at = 0; at < size; at += 4096)
        if (!VirtualProtect(base + va + at, 4096, protection(permissions), &old)) return false;
    return true;
}
bool ctr_host_unmap(unsigned char* base, uint32_t va, uint32_t size) {
    bool ok = true;
    for (uint32_t at = 0; at < size; at += 4096)
        ok = g_host.unmap2(GetCurrentProcess(), base + va + at, MEM_PRESERVE_PLACEHOLDER) && ok;
    return ok;
}
static LONG CALLBACK fault(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 || !g_host.base)
        return EXCEPTION_CONTINUE_SEARCH;
    unsigned char* at = (unsigned char*)r->ExceptionInformation[1];
    if (at < g_host.base || at >= g_host.base + CTR_HOST_SPACE + CTR_HOST_GUARD) return EXCEPTION_CONTINUE_SEARCH;
    if (at < g_host.base + CTR_HOST_SPACE && g_host.resolve && g_host.resolve(g_host.context, (uint32_t)(at - g_host.base)))
        return EXCEPTION_CONTINUE_EXECUTION;
    fprintf(stderr, "3ds: guest data abort: %s 0x%08llx in the block at 0x%08x\n",
            r->ExceptionInformation[0] ? "write to" : "read of", (unsigned long long)(at - g_host.base),
            g_host.pc ? (unsigned)*g_host.pc : 0u);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 1);
    return EXCEPTION_CONTINUE_SEARCH;
}
void ctr_host_faults(unsigned char* base, const uint32_t* pc, bool (*resolve)(void* context, uint32_t va),
                     void* context) {
    if (base && !g_host.handler) g_host.handler = AddVectoredExceptionHandler(1, fault);
    if (!base && g_host.handler) {
        RemoveVectoredExceptionHandler(g_host.handler);
        g_host.handler = NULL;
    }
    g_host.base = base;
    g_host.pc = pc;
    g_host.resolve = base ? resolve : NULL;
    g_host.context = context;
}
void ctr_host_say(const char* text, size_t size) {
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)size, &written, NULL);
}
#else
static int protection(unsigned permissions) {
    return (permissions & (SNX_CTR_READ | SNX_CTR_EXEC) ? PROT_READ : 0) | (permissions & SNX_CTR_WRITE ? PROT_WRITE : 0);
}
bool ctr_host_flat(void) { return sysconf(_SC_PAGESIZE) == 4096; }
bool ctr_host_memory_new(CtrHostMemory* m, uint64_t size) {
    memset(m, 0, sizeof *m);
    int fd = -1;
#if defined(__linux__) || defined(__ANDROID__)
    /* the system call itself: Android's C library has memfd_create only from Android 11 (API 30), as code_loader.c
       says */
    fd = (int)syscall(SYS_memfd_create, "snx-ctr-ram", 1u /* MFD_CLOEXEC */);
#else
    char name[64];
    snprintf(name, sizeof name, "/snx-ctr-%ld-%p", (long)getpid(), (void*)m);
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) shm_unlink(name);
#endif
    if (fd < 0) return false;
    void* view = MAP_FAILED;
    if (!ftruncate(fd, (off_t)size)) view = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        close(fd);
        return false;
    }
    m->handle = fd;
    m->view = view;
    m->size = size;
    return true;
}
void ctr_host_memory_free(CtrHostMemory* m) {
    if (m->view) munmap(m->view, (size_t)m->size);
    if (m->view) close((int)m->handle);
    memset(m, 0, sizeof *m);
}
unsigned char* ctr_host_reserve(void) {
    void* base = mmap(NULL, (size_t)(CTR_HOST_SPACE + CTR_HOST_GUARD), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    return base == MAP_FAILED ? NULL : base;
}
void ctr_host_release(unsigned char* base) { munmap(base, (size_t)(CTR_HOST_SPACE + CTR_HOST_GUARD)); }
bool ctr_host_map(const CtrHostMemory* m, unsigned char* base, uint32_t va, uint64_t offset, uint32_t size,
                  unsigned permissions) {
    return mmap(base + va, size, protection(permissions), MAP_SHARED | MAP_FIXED, (int)m->handle, (off_t)offset) !=
           MAP_FAILED;
}
bool ctr_host_protect(unsigned char* base, uint32_t va, uint32_t size, unsigned permissions) {
    return !mprotect(base + va, size, protection(permissions));
}
bool ctr_host_unmap(unsigned char* base, uint32_t va, uint32_t size) {
    return mmap(base + va, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) !=
           MAP_FAILED;
}
/* hexadecimal for a signal handler, which may not call stdio */
static void hex(char* out, uint64_t value, unsigned digits) {
    for (unsigned i = 0; i < digits; ++i) out[i] = "0123456789abcdef"[(value >> ((digits - 1 - i) * 4)) & 15];
}
static void fault(int signal, siginfo_t* info, void* context) {
    unsigned char* at = info->si_addr;
    /* a tracked page (SIGSEGV on Linux and FreeBSD, SIGBUS where a protection fault is one): resolved, the access is
       made again on return */
    if (g_host.base && at >= g_host.base && at < g_host.base + CTR_HOST_SPACE && g_host.resolve &&
        g_host.resolve(g_host.context, (uint32_t)(at - g_host.base)))
        return;
    if (g_host.base && at >= g_host.base && at < g_host.base + CTR_HOST_SPACE + CTR_HOST_GUARD) {
        char line[] = "3ds: guest data abort at 0x000000000 in the block at 0x00000000\n";
        hex(line + 27, (uint64_t)(at - g_host.base), 9);
        hex(line + 55, g_host.pc ? *g_host.pc : 0, 8);
        ssize_t written = write(2, line, sizeof line - 1);
        (void)written;
        _exit(1);
    }
    /* someone else's fault: the handler before, or the default (the signal again, without this handler) */
    const struct sigaction* before = signal == SIGBUS ? &g_host.bus : &g_host.segv;
    if (before->sa_flags & SA_SIGINFO && before->sa_sigaction)
        before->sa_sigaction(signal, info, context);
    else if (before->sa_handler != SIG_DFL && before->sa_handler != SIG_IGN && before->sa_handler)
        before->sa_handler(signal);
    else {
        sigaction(signal, before, NULL);
        raise(signal);
    }
}
/* the alternate stack a resolution runs on: it may submit to the GPU and wait for it, which the few kilobytes of a
   usual signal stack would not hold */
#define FAULT_STACK (1u << 20)
void ctr_host_faults(unsigned char* base, const uint32_t* pc, bool (*resolve)(void* context, uint32_t va),
                     void* context) {
    if (base && !g_host.base) {
        void* stack = mmap(NULL, FAULT_STACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stack != MAP_FAILED) {
            g_host.stack = (stack_t){.ss_sp = stack, .ss_size = FAULT_STACK};
            if (sigaltstack(&g_host.stack, &g_host.before_stack)) {
                munmap(stack, FAULT_STACK);
                g_host.stack.ss_sp = NULL;
            }
        }
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.sa_sigaction = fault;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, &g_host.segv);
        sigaction(SIGBUS, &action, &g_host.bus);
    } else if (!base && g_host.base) {
        sigaction(SIGSEGV, &g_host.segv, NULL);
        sigaction(SIGBUS, &g_host.bus, NULL);
        if (g_host.stack.ss_sp) {
            sigaltstack(&g_host.before_stack, NULL);
            munmap(g_host.stack.ss_sp, FAULT_STACK);
            g_host.stack.ss_sp = NULL;
        }
    }
    g_host.base = base;
    g_host.pc = pc;
    g_host.resolve = base ? resolve : NULL;
    g_host.context = context;
}
void ctr_host_say(const char* text, size_t size) {
    ssize_t written = write(2, text, size);
    (void)written;
}
#endif
