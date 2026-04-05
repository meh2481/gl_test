#include <cstddef>
#include <cstdarg>

extern "C" void* SDL_malloc(size_t);
extern "C" void SDL_free(void*);
extern "C" int SDL_vsnprintf(char* text, size_t maxlen, const char* fmt, va_list ap);

extern "C" [[noreturn]] void my_exit(int status) {
    // SYS_exit (60) only terminates the calling thread; background threads keep
    // the process alive.  SYS_exit_group (231) terminates every thread in the
    // process, equivalent to the C library's exit().
    register long rax __asm__("rax") = 231; // SYS_exit_group
    register long rdi __asm__("rdi") = status;
    __asm__ volatile ("syscall" : : "r"(rax), "r"(rdi) : "rcx", "r11");
    __builtin_unreachable();
}

namespace std {
[[noreturn]] void __throw_bad_alloc() {
    my_exit(1);
}

[[noreturn]] void __throw_bad_array_new_length() {
    my_exit(1);
}

[[noreturn]] void __throw_length_error(const char*) {
    my_exit(1);
}

[[noreturn]] void __throw_logic_error(const char*) {
    my_exit(1);
}

[[noreturn]] void __throw_out_of_range(const char*) {
    my_exit(1);
}

[[noreturn]] void __throw_out_of_range_fmt(const char*, ...) {
    my_exit(1);
}

[[noreturn]] void __throw_runtime_error(const char*) {
    my_exit(1);
}

[[noreturn]] void __throw_bad_function_call() {
    my_exit(1);
}

[[noreturn]] void __throw_invalid_argument(const char*) {
    my_exit(1);
}
}

extern "C" [[noreturn]] void __cxa_throw_bad_array_new_length() {
    my_exit(1);
}

extern "C" {
__attribute__((visibility("hidden"))) void* __dso_handle = nullptr;
}

// These functions are given hidden visibility so they only apply to the application's
// own translation units. External shared libraries (e.g. MangoHud, libstdc++) that are
// loaded at runtime will resolve these symbols from their own dependencies (glibc,
// libstdc++) instead of picking up these stubs via the dynamic linker's global symbol
// preemption. Without hidden visibility, the dynamic linker would expose the executable's
// stub versions to all loaded libraries:
//  - __cxa_atexit: MangoHud's Vulkan layer destructors would be silently dropped,
//    leaving stale Vulkan handles that get accessed on process exit → SIGSEGV.
//  - __cxa_guard_acquire/release: MangoHud runs its own threads; a non-atomic guard
//    implementation causes static-local initialisation races.
extern "C" __attribute__((visibility("hidden"))) int __cxa_guard_acquire(long long* guard) {
    return (*reinterpret_cast<unsigned char*>(guard) == 0) ? 1 : 0;
}

extern "C" __attribute__((visibility("hidden"))) void __cxa_guard_release(long long* guard) {
    *reinterpret_cast<unsigned char*>(guard) = 1;
}

extern "C" __attribute__((visibility("hidden"))) void __cxa_guard_abort(long long*) {
}

extern "C" __attribute__((visibility("hidden"))) int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

extern "C" [[noreturn]] void __cxa_pure_virtual() {
    my_exit(1);
}

extern "C" [[noreturn]] void __cxa_deleted_virtual() {
    my_exit(1);
}

void* operator new(std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    void* ptr = SDL_malloc(size);
    if (!ptr) {
        my_exit(1);
    }
    return ptr;
}

void* operator new[](std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    void* ptr = SDL_malloc(size);
    if (!ptr) {
        my_exit(1);
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    SDL_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    SDL_free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    SDL_free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    SDL_free(ptr);
}

extern "C" __attribute__((visibility("hidden"))) char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n-- && (*d++ = *src++));
    return dest;
}

extern "C" __attribute__((visibility("hidden"))) void __assert_fail(const char* assertion, const char* file, unsigned int line, const char* function) {
    my_exit(1);
}

extern "C" __attribute__((visibility("hidden"))) void __stack_chk_fail(void) {
    my_exit(1);
}

extern "C" __attribute__((visibility("hidden"))) const char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    return last;
}

extern "C" __attribute__((visibility("hidden"))) int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    return SDL_vsnprintf(buf, size, fmt, ap);
}

extern "C" __attribute__((visibility("hidden"))) int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = SDL_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return result;
}

extern "C" __attribute__((visibility("hidden"))) int munmap(void* addr, size_t length) {
    register long rax __asm__("rax") = 11;
    register long rdi __asm__("rdi") = (long)addr;
    register long rsi __asm__("rsi") = length;
    __asm__ volatile ("syscall" : : "r"(rax), "r"(rdi), "r"(rsi) : "rcx", "r11");
    return 0; // dummy
}

struct timespec_shim { long tv_sec; long tv_nsec; };
extern "C" __attribute__((visibility("hidden"))) int clock_gettime(int clockid, struct timespec_shim* ts) {
    register long rax __asm__("rax") = 228; // SYS_clock_gettime
    register long rdi __asm__("rdi") = clockid;
    register long rsi __asm__("rsi") = (long)ts;
    long ret;
    __asm__ volatile ("syscall" : "=r"(rax) : "r"(rax), "r"(rdi), "r"(rsi) : "rcx", "r11", "memory");
    return (int)rax;
}

extern "C" __attribute__((visibility("hidden"))) void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

extern "C" __attribute__((visibility("hidden"))) void* memset(void* dest, int value, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char byteValue = static_cast<unsigned char>(value);
    for (size_t i = 0; i < n; ++i) {
        d[i] = byteValue;
    }
    return dest;
}

extern "C" __attribute__((visibility("hidden"))) void* memmove(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

extern "C" int app_main();

// _start must not be a normal C/C++ function: the kernel jumps here with %rsp
// 16-byte aligned and NO return address pushed.  A C function prologue would
// emit "push %rbp" which shifts %rsp to 8-mod-16, and the subsequent
// "call app_main" would then push another 8 bytes making it 0-mod-16 at
// app_main entry — which sounds right but the compiler's local-variable
// save-area allocation and any sub-calls inside app_main still end up
// misaligned because the C compiler assumed the usual "8-mod-16 on entry"
// calling convention when it emitted app_main's prologue.
//
// The assembly version below:
//  1. Zeroes %rbp to mark the bottom of the call-frame chain.
//  2. Explicitly forces 16-byte alignment so the "call app_main" leaves
//     %rsp exactly 0-mod-16 inside app_main (after the call push).
//  3. Passes the return value of app_main to my_exit so the process exits
//     cleanly instead of executing whatever follows.
__asm__(
    ".text\n"
    ".globl _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "  xorq  %rbp, %rbp\n"        /* mark end of stack-frame chain          */
    "  andq  $-16, %rsp\n"        /* force 16-byte alignment before call     */
    "  call  app_main\n"          /* rsp is 8-mod-16 inside app_main (ABI ✓) */
    "  movq  %rax, %rdi\n"        /* pass return value as exit status        */
    "  call  my_exit\n"
);