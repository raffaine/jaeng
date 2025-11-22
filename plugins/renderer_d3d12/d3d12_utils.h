#pragma once

#ifndef _DEBUG
#   define JAENG_ASSERT(x) do { if(!(x)) { __debugbreak(); } } while(0)
#   define HR_CHECK(x) do { HRESULT _hr = (x); JAENG_ASSERT(SUCCEEDED(_hr)); } while(0)
#else
#   define JAENG_ASSERT(x)
#   define HR_CHECK(x) (x)
#endif

inline uint64_t AlignUp(uint64_t v, uint64_t align) {
    return (v + (align - 1)) & ~(align - 1);
}
