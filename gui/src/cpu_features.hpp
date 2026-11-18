#pragma once

// What this machine's processor runs, for choosing among the builds an engine publishes: its architecture, its maker
// and the instruction sets engines are built for, read from the processor itself (CPUID and XGETBV on x86, what the
// system says on ARM). An instruction set counts only when the system also saves its registers (AVX, AVX-512).

#include <cstdint>
#include <string>

namespace fcg {

enum class CpuArch { Other, X86_64, Arm64 };

struct CpuFeatures {
    CpuArch arch = CpuArch::Other;  // the machine's, not the program's: ARM64 for an x86-64 program run by Rosetta
    // An ARM64 machine that also runs x86-64 programs (Rosetta, Windows' emulation): the x86 fields below are then
    // what the emulator offers.
    bool x86_emulated = false;
    std::string vendor;  // CPUID's: "AuthenticAMD", "GenuineIntel", "HygonGenuine"; empty on ARM
    std::string brand;   // "AMD Ryzen 9 9950X 16-Core Processor"; empty when the processor does not say
    int family = 0;      // x86: the base family plus the extended one (0x19 for Zen 3 and Zen 4)
    int model  = 0;      // x86: with the extended model

    // x86-64
    bool sse3 = false, ssse3 = false, sse41 = false, sse42 = false, popcnt = false, cx16 = false, lahf = false;
    bool avx = false, avx2 = false, fma = false, f16c = false, bmi1 = false, bmi2 = false, lzcnt = false, movbe = false;
    bool avx512f = false, avx512bw = false, avx512cd = false, avx512dq = false, avx512vl = false;
    bool avx512vnni = false, avx512vbmi = false, avx512vbmi2 = false, avx512ifma = false, avx512bitalg = false,
         avx512vpopcntdq = false, gfni = false, vaes = false, vpclmulqdq = false;
    bool avxvnni = false;  // the VEX-encoded VNNI of Alder Lake and Zen 5
    // ARM64 (its base, with NEON, is always there)
    bool dotprod = false, i8mm = false;

    // The level of the x86-64 psABI (1 to 4) the processor and the system allow; 0 when it runs no x86-64 code.
    [[nodiscard]] int x86Level() const;
    // BMI2's PEXT and PDEP are fast: not on AMD's processors of families 0x15 to 0x18 (to Zen 2, Hygon's too),
    // which run them in microcode, hundreds of times slower.
    [[nodiscard]] bool fastPext() const;
    // AMD's Zen generation (1 to 5; Zen+ is 1), 0 when it is not one.
    [[nodiscard]] int zen() const;
};

// What CPUID and XGETBV answered: what cpuFromLeaves() reads, and what the tests make up.
struct CpuidLeaves {
    uint32_t max_leaf     = 0;  // leaf 0, EAX
    uint32_t max_extended = 0;  // leaf 0x80000000, EAX
    std::string vendor;         // leaf 0, EBX EDX ECX
    uint32_t l1_eax = 0, l1_ecx = 0, l1_edx = 0;
    uint32_t l7_ebx = 0, l7_ecx = 0, l7_edx = 0;  // leaf 7, subleaf 0
    uint32_t l7s1_eax = 0;                        // leaf 7, subleaf 1 (0 when there is none)
    uint32_t e1_ecx   = 0;                        // leaf 0x80000001
    uint64_t xcr0     = 0;                        // XGETBV(0), when CPUID says the system uses XSAVE
    std::string brand;                            // leaves 0x80000002 to 0x80000004
};

// An x86-64 processor from what CPUID and XGETBV answered.
[[nodiscard]] CpuFeatures cpuFromLeaves(const CpuidLeaves& leaves);

// This machine's processor.
[[nodiscard]] CpuFeatures detectCpu();

}  // namespace fcg
