#include <cstdint>
extern "C" uint64_t bts_test_forward(uint64_t a, double b, uint64_t c, double d,
                                    uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
    return a + static_cast<uint64_t>(b) + c + static_cast<uint64_t>(d) + e + f + g + h;
}
