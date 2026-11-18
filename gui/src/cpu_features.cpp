#include "cpu_features.hpp"

#include <array>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#    define FCG_CPU_X86 1
#    if defined(_MSC_VER)
#        include <intrin.h>
#    else
#        include <cpuid.h>
#    endif
#endif

#if defined(__linux__) && defined(__aarch64__)
#    include <sys/auxv.h>
#endif

#if defined(__APPLE__)
#    include <sys/sysctl.h>
#    include <sys/types.h>
#endif

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

namespace fcg {

namespace {

bool bit(uint32_t reg, int b) { return ((reg >> b) & 1u) != 0; }

std::string trimmed(std::string s) {
    const auto keep = [](char c) { return c != ' ' && c != '\0' && c != '\t'; };
    std::size_t end = s.size();
    while (end > 0 && !keep(s[end - 1])) --end;
    std::size_t start = 0;
    while (start < end && !keep(s[start])) ++start;
    return s.substr(start, end - start);
}

bool amdLike(const CpuFeatures& c) { return c.vendor == "AuthenticAMD" || c.vendor == "HygonGenuine"; }

#if defined(__APPLE__)
// A number sysctl gives, -1 when it has none of that name.
long long sysctlNumber(const char* name) {
    long long v      = 0;
    std::size_t size = sizeof v;
    if (sysctlbyname(name, &v, &size, nullptr, 0) != 0) return -1;
    // A 32-bit answer fills the low half only (little-endian).
    if (size == sizeof(int)) {
        int i = 0;
        std::memcpy(&i, &v, sizeof i);
        return i;
    }
    return v;
}

std::string sysctlText(const char* name) {
    std::array<char, 256> text{};
    std::size_t size = text.size();
    if (sysctlbyname(name, text.data(), &size, nullptr, 0) != 0) return {};
    return trimmed(std::string(text.data(), strnlen(text.data(), text.size())));
}
#endif

#if defined(FCG_CPU_X86)
void cpuid(uint32_t leaf, uint32_t subleaf, std::array<uint32_t, 4>& r) {
#    if defined(_MSC_VER)
    int out[4];
    __cpuidex(out, static_cast<int>(leaf), static_cast<int>(subleaf));
    for (int i = 0; i < 4; ++i) r[i] = static_cast<uint32_t>(out[i]);
#    else
    __cpuid_count(leaf, subleaf, r[0], r[1], r[2], r[3]);
#    endif
}

uint64_t xgetbv0() {
#    if defined(_MSC_VER)
    return _xgetbv(0);
#    else
    uint32_t eax = 0, edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
#    endif
}

CpuidLeaves readLeaves() {
    CpuidLeaves l;
    std::array<uint32_t, 4> r{};
    cpuid(0, 0, r);
    l.max_leaf      = r[0];
    char vendor[13] = {};
    std::memcpy(vendor, &r[1], 4);
    std::memcpy(vendor + 4, &r[3], 4);
    std::memcpy(vendor + 8, &r[2], 4);
    l.vendor = vendor;
    if (l.max_leaf >= 1) {
        cpuid(1, 0, r);
        l.l1_eax = r[0], l.l1_ecx = r[2], l.l1_edx = r[3];
        // XGETBV raises #UD unless the system uses XSAVE (OSXSAVE).
        if (bit(l.l1_ecx, 27)) l.xcr0 = xgetbv0();
    }
    if (l.max_leaf >= 7) {
        cpuid(7, 0, r);
        l.l7_ebx = r[1], l.l7_ecx = r[2], l.l7_edx = r[3];
        if (r[0] >= 1) {
            std::array<uint32_t, 4> s{};
            cpuid(7, 1, s);
            l.l7s1_eax = s[0];
        }
    }
    cpuid(0x80000000u, 0, r);
    l.max_extended = r[0];
    if (l.max_extended >= 0x80000001u) {
        cpuid(0x80000001u, 0, r);
        l.e1_ecx = r[2];
    }
    if (l.max_extended >= 0x80000004u) {
        char brand[49] = {};
        for (uint32_t i = 0; i < 3; ++i) {
            cpuid(0x80000002u + i, 0, r);
            std::memcpy(brand + 16 * i, r.data(), 16);
        }
        l.brand = brand;
    }
    return l;
}
#endif

}  // namespace

int CpuFeatures::x86Level() const {
    if (arch != CpuArch::X86_64 && !x86_emulated) return 0;
    if (!(cx16 && lahf && popcnt && sse3 && sse41 && sse42 && ssse3)) return 1;
    if (!(avx && avx2 && bmi1 && bmi2 && f16c && fma && lzcnt && movbe)) return 2;
    if (!(avx512f && avx512bw && avx512cd && avx512dq && avx512vl)) return 3;
    return 4;
}

bool CpuFeatures::fastPext() const { return bmi2 && !(amdLike(*this) && family >= 0x15 && family <= 0x18); }

int CpuFeatures::zen() const {
    if (vendor != "AuthenticAMD") return 0;
    const auto in = [&](int from, int to) { return model >= from && model <= to; };
    switch (family) {
        case 0x17:
            return model < 0x30 ? 1 : 2;  // Zen and Zen+ (Dali, 0x20, included), then Zen 2
        case 0x19:
            return in(0x10, 0x1F) || in(0x60, 0x7F) || in(0xA0, 0xAF) ? 4 : 3;
        case 0x1A:
            return 5;
        default:
            return 0;
    }
}

CpuFeatures cpuFromLeaves(const CpuidLeaves& l) {
    CpuFeatures c;
    c.arch   = CpuArch::X86_64;
    c.vendor = l.vendor;
    c.brand  = trimmed(l.brand);

    const auto base_family = static_cast<int>((l.l1_eax >> 8) & 0xF);
    const auto base_model  = static_cast<int>((l.l1_eax >> 4) & 0xF);
    c.family               = base_family == 0xF ? base_family + static_cast<int>((l.l1_eax >> 20) & 0xFF) : base_family;
    c.model = base_family == 0x6 || base_family == 0xF ? (static_cast<int>((l.l1_eax >> 16) & 0xF) << 4) + base_model
                                                       : base_model;

    // The registers the system saves: XMM and YMM for AVX, plus the opmasks and the ZMM ones for AVX-512.
    const uint64_t xcr0 = bit(l.l1_ecx, 27) ? l.xcr0 : 0;
    const bool ymm      = (xcr0 & 0x6) == 0x6;
    const bool zmm      = ymm && (xcr0 & 0xE0) == 0xE0;

    if (l.max_leaf >= 1) {
        c.sse3   = bit(l.l1_ecx, 0);
        c.ssse3  = bit(l.l1_ecx, 9);
        c.cx16   = bit(l.l1_ecx, 13);
        c.sse41  = bit(l.l1_ecx, 19);
        c.sse42  = bit(l.l1_ecx, 20);
        c.movbe  = bit(l.l1_ecx, 22);
        c.popcnt = bit(l.l1_ecx, 23);
        c.fma    = bit(l.l1_ecx, 12) && ymm;
        c.avx    = bit(l.l1_ecx, 28) && ymm;
        c.f16c   = bit(l.l1_ecx, 29) && ymm;
    }
    if (l.max_leaf >= 7) {
        c.bmi1            = bit(l.l7_ebx, 3);
        c.avx2            = bit(l.l7_ebx, 5) && ymm;
        c.bmi2            = bit(l.l7_ebx, 8);
        c.avx512f         = bit(l.l7_ebx, 16) && zmm;
        c.avx512dq        = bit(l.l7_ebx, 17) && zmm;
        c.avx512ifma      = bit(l.l7_ebx, 21) && zmm;
        c.avx512cd        = bit(l.l7_ebx, 28) && zmm;
        c.avx512bw        = bit(l.l7_ebx, 30) && zmm;
        c.avx512vl        = bit(l.l7_ebx, 31) && zmm;
        c.avx512vbmi      = bit(l.l7_ecx, 1) && zmm;
        c.avx512vbmi2     = bit(l.l7_ecx, 6) && zmm;
        c.gfni            = bit(l.l7_ecx, 8);
        c.vaes            = bit(l.l7_ecx, 9) && ymm;
        c.vpclmulqdq      = bit(l.l7_ecx, 10) && ymm;
        c.avx512vnni      = bit(l.l7_ecx, 11) && zmm;
        c.avx512bitalg    = bit(l.l7_ecx, 12) && zmm;
        c.avx512vpopcntdq = bit(l.l7_ecx, 14) && zmm;
        c.avxvnni         = bit(l.l7s1_eax, 4) && ymm;
    }
    if (l.max_extended >= 0x80000001u) {
        c.lahf  = bit(l.e1_ecx, 0);
        c.lzcnt = bit(l.e1_ecx, 5);
    }
    return c;
}

CpuFeatures detectCpu() {
    CpuFeatures c;
#if defined(FCG_CPU_X86)
    auto leaves = readLeaves();
#    if defined(__APPLE__)
    // macOS gives a program the AVX-512 registers at its first AVX-512 instruction only: XCR0 does not show them
    // before, sysctl does.
    if (bit(leaves.l7_ebx, 16) && (leaves.xcr0 & 0xE0) != 0xE0 && sysctlNumber("hw.optional.avx512f") == 1)
        leaves.xcr0 |= 0xE0;
#    endif
    c = cpuFromLeaves(leaves);
#    if defined(__APPLE__)
    // This program run by Rosetta on Apple Silicon: the engines had better be ARM64 ones, which run natively.
    if (sysctlNumber("sysctl.proc_translated") == 1) {
        c.arch           = CpuArch::Arm64;
        c.x86_emulated   = true;
        c.dotprod        = sysctlNumber("hw.optional.arm.FEAT_DotProd") != 0;  // every Apple processor has it
        c.i8mm           = sysctlNumber("hw.optional.arm.FEAT_I8MM") == 1;
        const auto brand = sysctlText("machdep.cpu.brand_string");
        if (!brand.empty()) c.brand = brand;
    }
#    endif
#    if defined(_WIN32)
    // An ARM64 PC runs this x86-64 program by emulation; IsWow64Process2 tells the machine (Windows 10 1709 on).
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    if (const auto kernel = GetModuleHandleW(L"kernel32.dll")) {
        if (const auto f = reinterpret_cast<IsWow64Process2Fn>(
                reinterpret_cast<void*>(GetProcAddress(kernel, "IsWow64Process2")))) {
            USHORT process = 0, native = 0;
            if (f(GetCurrentProcess(), &process, &native) && native == 0xAA64 /* IMAGE_FILE_MACHINE_ARM64 */) {
                c.arch         = CpuArch::Arm64;
                c.x86_emulated = true;
                c.dotprod      = IsProcessorFeaturePresent(43) != 0;  // PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE
                c.i8mm         = IsProcessorFeaturePresent(66) != 0;  // PF_ARM_V82_I8MM_INSTRUCTIONS_AVAILABLE
            }
        }
    }
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    c.arch = CpuArch::Arm64;
#    if defined(__APPLE__)
    c.dotprod = sysctlNumber("hw.optional.arm.FEAT_DotProd") != 0;
    c.i8mm    = sysctlNumber("hw.optional.arm.FEAT_I8MM") == 1;
    c.brand   = sysctlText("machdep.cpu.brand_string");
#    elif defined(__linux__)
    const auto hwcap  = getauxval(AT_HWCAP);
    const auto hwcap2 = getauxval(AT_HWCAP2);
    c.dotprod         = (hwcap & (1ul << 20)) != 0;   // HWCAP_ASIMDDP
    c.i8mm            = (hwcap2 & (1ul << 13)) != 0;  // HWCAP2_I8MM
#    elif defined(_WIN32)
    c.dotprod = IsProcessorFeaturePresent(43) != 0;
    c.i8mm    = IsProcessorFeaturePresent(66) != 0;
#    endif
#endif
    return c;
}

}  // namespace fcg
