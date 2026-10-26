#pragma once

#include <atomic>

namespace arch {

// IO Write memory barrier
inline void io_wmb() {
#if defined(__x86_64) || defined(__x86_64__)
    // a write memory barrier shouldn't be needed in x86_64 as it is TSO:
    //   - Stores are buffered and committed in order 
#elif defined(__ARM__)
    asm volatile("dmb oshst" : : : "memory")
#else
    #error "unsupported arch"
#endif
}

// IO Read memory barrier
inline void io_rmb() {
#if defined(__x86_64) || defined(__x86_64__)
    // a write memory barrier shouldn't be needed in x86_64 as it is TSO:
    //   - Stores are buffered and committed in order 
#elif defined(__ARM__)
    asm volatile("dmb oshld" : : : "memory")
#else
    #error "unsupported arch"
#endif
}

// Write memory barrier
inline void wmb() {
    std::atomic_thread_fence(std::memory_order_release);
}

// Read memory barrier
inline void rmb() {
    std::atomic_thread_fence(std::memory_order_acquire);
}

}  // namespace arch
