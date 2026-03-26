#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstddef>

namespace std {
[[noreturn]] void __throw_bad_alloc() {
    std::abort();
}

[[noreturn]] void __throw_bad_array_new_length() {
    std::abort();
}

[[noreturn]] void __throw_length_error(const char*) {
    std::abort();
}

[[noreturn]] void __throw_logic_error(const char*) {
    std::abort();
}

[[noreturn]] void __throw_out_of_range(const char*) {
    std::abort();
}

[[noreturn]] void __throw_out_of_range_fmt(const char*, ...) {
    std::abort();
}

[[noreturn]] void __throw_runtime_error(const char*) {
    std::abort();
}

[[noreturn]] void __throw_bad_function_call() {
    std::abort();
}

[[noreturn]] void __throw_invalid_argument(const char*) {
    std::abort();
}
}

extern "C" [[noreturn]] void __cxa_throw_bad_array_new_length() {
    std::abort();
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
    std::abort();
}

extern "C" [[noreturn]] void __cxa_deleted_virtual() {
    std::abort();
}

void* operator new(std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    void* ptr = SDL_malloc(size);
    if (!ptr) {
        std::abort();
    }
    return ptr;
}

void* operator new[](std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    void* ptr = SDL_malloc(size);
    if (!ptr) {
        std::abort();
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