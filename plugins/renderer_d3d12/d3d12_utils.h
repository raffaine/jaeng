#pragma once

inline uint64_t AlignUp(uint64_t v, uint64_t align) {
    return (v + (align - 1)) & ~(align - 1);
}
