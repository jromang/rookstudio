// What a processor runs, from what CPUID and XGETBV answer.

#include <doctest/doctest.hpp>

#include <cstdint>
#include <string>

#include "cpu_features.hpp"

using namespace fcg;

namespace {

constexpr uint32_t bit(int n) { return uint32_t{1} << n; }

// Leaf 1's EAX for a family and a model, as the processor encodes them (an extended family past 0xF).
uint32_t familyModel(int family, int model) {
    const int base     = family >= 0xF ? 0xF : family;
    const int extended = family >= 0xF ? family - 0xF : 0;
    return (static_cast<uint32_t>(extended) << 20) | (static_cast<uint32_t>(model >> 4) << 16) |
           (static_cast<uint32_t>(base) << 8) | (static_cast<uint32_t>(model & 0xF) << 4);
}

enum class Set { Core2, V2, V3, V4, V4Vnni, IceLake };

// A processor of `vendor`, `family` and `model` with the instruction sets of `set`, and a system that saves their
// registers.
CpuidLeaves processor(const char* vendor, int family, int model, Set set) {
    CpuidLeaves l;
    l.max_leaf     = 0x10;
    l.max_extended = 0x80000008u;
    l.vendor       = vendor;
    l.l1_eax       = familyModel(family, model);
    // SSE3, SSSE3, CX16, SSE4.1; LAHF.
    l.l1_ecx = bit(0) | bit(9) | bit(13) | bit(19);
    l.e1_ecx = bit(0);
    if (set == Set::Core2) return l;
    l.l1_ecx |= bit(20) | bit(23);  // SSE4.2, POPCNT
    if (set == Set::V2) return l;
    // FMA, MOVBE, OSXSAVE, AVX, F16C; BMI1, AVX2, BMI2; LZCNT; the system saves XMM and YMM.
    l.l1_ecx |= bit(12) | bit(22) | bit(27) | bit(28) | bit(29);
    l.l7_ebx |= bit(3) | bit(5) | bit(8);
    l.e1_ecx |= bit(5);
    l.xcr0 = 0x7;
    if (set == Set::V3) return l;
    // AVX-512 F, DQ, CD, BW, VL; the opmasks and the ZMM registers saved.
    l.l7_ebx |= bit(16) | bit(17) | bit(28) | bit(30) | bit(31);
    l.xcr0 = 0xE7;
    if (set == Set::V4) return l;
    l.l7_ecx |= bit(11);  // VNNI
    if (set == Set::V4Vnni) return l;
    // IFMA; VBMI, VBMI2, GFNI, VAES, VPCLMULQDQ, BITALG, VPOPCNTDQ.
    l.l7_ebx |= bit(21);
    l.l7_ecx |= bit(1) | bit(6) | bit(8) | bit(9) | bit(10) | bit(12) | bit(14);
    return l;
}

}  // namespace

TEST_CASE("processor: the x86-64 level, from the instruction sets and what the system saves") {
    CHECK(cpuFromLeaves(processor("GenuineIntel", 6, 0x17, Set::Core2)).x86Level() == 1);  // Penryn: no POPCNT
    CHECK(cpuFromLeaves(processor("GenuineIntel", 6, 0x2A, Set::V2)).x86Level() == 2);
    CHECK(cpuFromLeaves(processor("GenuineIntel", 6, 0x3C, Set::V3)).x86Level() == 3);  // Haswell
    CHECK(cpuFromLeaves(processor("GenuineIntel", 6, 0x55, Set::V4)).x86Level() == 4);  // Skylake-X

    // AVX-512 on the processor, but a system that does not save the ZMM registers: AVX2 at most.
    auto zmm_off      = processor("GenuineIntel", 6, 0x55, Set::V4);
    zmm_off.xcr0      = 0x7;
    const auto no_zmm = cpuFromLeaves(zmm_off);
    CHECK(no_zmm.x86Level() == 3);
    CHECK_FALSE(no_zmm.avx512f);
    CHECK(no_zmm.avx2);

    // A system that does not use XSAVE at all: no AVX, whatever XCR0 would say.
    auto no_xsave = processor("GenuineIntel", 6, 0x3C, Set::V3);
    no_xsave.l1_ecx &= ~bit(27);
    const auto old_system = cpuFromLeaves(no_xsave);
    CHECK(old_system.x86Level() == 2);
    CHECK_FALSE(old_system.avx);
    CHECK_FALSE(old_system.avx2);
    CHECK(old_system.bmi2);  // not a register set: there all the same

    const auto icl = cpuFromLeaves(processor("GenuineIntel", 6, 0x7E, Set::IceLake));
    CHECK(icl.avx512vnni);
    CHECK(icl.avx512vbmi2);
    CHECK(icl.gfni);
    CHECK(icl.avx512vpopcntdq);
}

TEST_CASE("processor: the family and the model, AMD's Zen generations, a slow PEXT") {
    const auto zen5 = cpuFromLeaves(processor("AuthenticAMD", 0x1A, 0x44, Set::IceLake));  // Ryzen 9 9950X
    CHECK(zen5.family == 0x1A);
    CHECK(zen5.model == 0x44);
    CHECK(zen5.zen() == 5);
    CHECK(zen5.fastPext());

    CHECK(cpuFromLeaves(processor("AuthenticAMD", 0x19, 0x61, Set::IceLake)).zen() == 4);  // Raphael
    CHECK(cpuFromLeaves(processor("AuthenticAMD", 0x19, 0x11, Set::IceLake)).zen() == 4);  // Genoa
    CHECK(cpuFromLeaves(processor("AuthenticAMD", 0x19, 0x21, Set::V3)).zen() == 3);       // Vermeer
    CHECK(cpuFromLeaves(processor("AuthenticAMD", 0x19, 0x50, Set::V3)).zen() == 3);       // Cezanne
    const auto zen2 = cpuFromLeaves(processor("AuthenticAMD", 0x17, 0x71, Set::V3));       // Matisse
    CHECK(zen2.zen() == 2);
    CHECK_FALSE(zen2.fastPext());
    CHECK(cpuFromLeaves(processor("AuthenticAMD", 0x17, 0x08, Set::V3)).zen() == 1);        // Pinnacle Ridge, Zen+
    CHECK(cpuFromLeaves(processor("AuthenticAMD", 0x17, 0x20, Set::V3)).zen() == 1);        // Dali
    CHECK_FALSE(cpuFromLeaves(processor("AuthenticAMD", 0x15, 0x60, Set::V3)).fastPext());  // Excavator

    const auto hygon = cpuFromLeaves(processor("HygonGenuine", 0x18, 0x00, Set::V3));
    CHECK(hygon.zen() == 0);
    CHECK_FALSE(hygon.fastPext());

    const auto haswell = cpuFromLeaves(processor("GenuineIntel", 6, 0x3C, Set::V3));
    CHECK(haswell.family == 6);
    CHECK(haswell.model == 0x3C);
    CHECK(haswell.zen() == 0);
    CHECK(haswell.fastPext());
}

TEST_CASE("processor: its name, without the spaces CPUID pads it with") {
    auto l  = processor("AuthenticAMD", 0x1A, 0x44, Set::IceLake);
    l.brand = std::string("  AMD Ryzen 9 9950X 16-Core Processor   ") + std::string(6, '\0');
    CHECK(cpuFromLeaves(l).brand == "AMD Ryzen 9 9950X 16-Core Processor");
}

TEST_CASE("processor: this machine's") {
    const auto cpu = detectCpu();
#if defined(__x86_64__) || defined(_M_X64)
    // An x86-64 machine, or an ARM64 one whose emulator runs these tests.
    CHECK((cpu.arch == CpuArch::X86_64 || cpu.x86_emulated));
    CHECK(cpu.x86Level() >= 1);
    CHECK_FALSE(cpu.vendor.empty());
#elif defined(__aarch64__) || defined(_M_ARM64)
    CHECK(cpu.arch == CpuArch::Arm64);
    CHECK(cpu.x86Level() == 0);
#endif
}
