#include <cstdint>

#include "architecture.h"
#include "sdkconfig.h"

#if defined(ARCH_ESP32)

extern "C" int esp_rom_printf(const char *format, ...);
extern "C" void __real_r_assert_err(const char *condition, const char *file, int line);

#if defined(CONFIG_IDF_TARGET_ESP32)
extern "C" void *ld_sco_env[];
extern "C" uint8_t sw_to_hw[];
#endif

extern "C" void __wrap_r_assert_err(const char *condition, const char *file, int line)
{
    const uintptr_t callerRaw = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t callerPc = (callerRaw & 0x3fffffffU) | 0x40000000U;
    esp_rom_printf("BT_CTRL_ASSERT caller=0x%08x file=%s line=%d cond=%s\n", static_cast<unsigned>(callerPc),
                   file != nullptr ? file : "?", line, condition != nullptr ? condition : "?");
#if defined(CONFIG_IDF_TARGET_ESP32)
    // ROM r_ld_sco_audio_isr asserts at ld_acl.c:7098 when its selected SCO context is null. Capture the fixed
    // controller tables only on that fatal path; this adds no steady-state work and does not alter controller state.
    if (line == 7098) {
        esp_rom_printf("BT_CTRL_SCO_CONTEXT env=%08x,%08x,%08x sw_to_hw14=%u\n",
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(ld_sco_env[0])),
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(ld_sco_env[1])),
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(ld_sco_env[2])), static_cast<unsigned>(sw_to_hw[14]));
    }
#endif
    __real_r_assert_err(condition, file, line);
}

#endif
