/* Signed 64-bit division for freestanding i386 (no libgcc). */
#include <kernel.h>

typedef long long s64;

s64 __divdi3(s64 a, s64 b) {
    int neg = ((a < 0) ^ (b < 0));
    u64 ua = (a < 0) ? -(u64)a : (u64)a;
    u64 ub = (b < 0) ? -(u64)b : (u64)b;
    u64 q = 0, rem = 0;
    for (int i = 63; i >= 0; i--) {
        rem <<= 1;
        rem |= (ua >> i) & 1;
        if (rem >= ub) { rem -= ub; q |= (1ULL << i); }
    }
    return neg ? -(s64)q : (s64)q;
}
