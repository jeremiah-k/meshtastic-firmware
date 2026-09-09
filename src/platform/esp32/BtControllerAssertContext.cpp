#include <cstdint>

#include "architecture.h"

#if defined(ARCH_ESP32)

extern "C" int esp_rom_printf(const char *format, ...);
extern "C" void __real_r_assert_err(const char *condition, const char *file, int line);

extern "C" void __wrap_r_assert_err(const char *condition, const char *file, int line)
{
    const uintptr_t callerRaw = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t callerPc = (callerRaw & 0x3fffffffU) | 0x40000000U;
    esp_rom_printf("BT_CTRL_ASSERT caller=0x%08x file=%s line=%d cond=%s\n", static_cast<unsigned>(callerPc),
                   file != nullptr ? file : "?", line, condition != nullptr ? condition : "?");
    __real_r_assert_err(condition, file, line);
}

#endif
