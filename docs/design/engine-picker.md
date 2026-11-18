# Engine picker: GitHub releases of the top UCI engines (study, 18 November 2026)

> The facts behind #59, the engine picker (2026-11-18): each engine's GitHub releases, the names of their files for each platform and CPU, the GitHub API and CPU detection. Spot-checked afterwards against the API (Stockfish sf_19, Gaia v4.4.4). Releases change: the picker reads them itself; this is for writing its catalogue.

Facts for a catalogue that a desktop GUI reads to download engines from their GitHub releases, always the latest.
Collected on 2026-11-18 from the public GitHub API (`gh api`, read-only), the engines' release notes and build
scripts (workflows, Makefiles at the release tag), and the rating lists named below. **No binary was downloaded**:
anything said about what is *inside* an executable or an archive comes from the build scripts or the release notes,
and is marked so.

The asset patterns below were checked against each engine's latest release: every pattern matches exactly one asset,
and none matches an asset of the side releases (Triumviratus Position Solver, Berserk HCE).

---

## 1. Selection

### Rating lists used

| List | Date | Top of the list (open engines only, commercial ones in brackets) |
|---|---|---|
| CCRL 40/15 (4 CPU, best versions) | 13 Nov 2026 | Stockfish 19 3650, Triumviratus 8.0 3647, Reckless 0.9.0 3645, PlentyChess 7.0.0 3642, pawnocchio 2.0.1 3642, (Torch v4d 3640), Coda 0.9.4 3638, Integral v8 3638, Cinder 0.6.1 3637, Obsidian 16.0 3636, Alexandria 9.0.0 3635, Stormphrax 8.0.0 3635, Hobbes 3.0 3634, Viridithas 20.0.0 3633, Caissa 2.0 3632, Astra 7.0 3629, Berserk 14 3628 … Gaia 4.3.2 3605 (#31) |
| CCRL Blitz | 14 Nov 2026 | Stockfish 19 3803, Triumviratus 8.1 3796, (Torch v4 3777), Obsidian 16.0 3773, PlentyChess 8.0.0 3771, Coda 0.9.4 3768, Reckless 0.9.0 3765, **Gaia 4.4.1 3763**, Berserk 20250606 / Cinder 0.6.1 3759, (Dragon 3.2), Alexandria 8.0.0 / pawnocchio 2.0 3754, Integral 8 3751, Caissa 2.0 / Viridithas 20 3747, Stormphrax 8.0.0 3744, Hobbes 3.0 3742 |
| CEGT 40/20 best versions | generated 15 Nov 2026 | Stockfish 19.0 3650, **Gaia 4.4.3 3635**, Integral v8 3624, (Torch 4d 3620), PlentyChess 8.0.0 3619, Reckless 0.9.0 3617, Cinder 0.6.1 3615, Alexandria 9.1 3611, pawnocchio 2.0.1 3604, Stormphrax 8.0.0 3603, Obsidian 16.0 3598, Caissa 2.0 3597, Viridithas 20.0.0 3595, Berserk 14.0 3590, Hobbes 3.0 3589 (Triumviratus and Coda not tested there) |
| SPCC (now a Stockfish VLTC table only; the UHO-Top15 list is retired) | 16 Nov 2026 | Stockfish dev builds 3914, Stockfish 19 3905, Triumviratus 8.1 3896, ShashChess 42 3894 |

### Kept (Gaia + Stockfish + 8)

The 8 strongest open engines (CCRL 40/15 order) that publish binaries in GitHub releases **for Linux and Windows,
and macOS** (all have an Apple Silicon build). Engines of the same strength that publish **Windows binaries only** are
in section 5 with their patterns, so they can be added as Windows-only entries.

| # | Engine | Why |
|---|---|---|
| – | Gaia | asked; CEGT #2, CCRL Blitz #8 |
| – | Stockfish | asked; #1 everywhere |
| 1 | Triumviratus | CCRL 40/15 #2, Blitz #2, SPCC: 9 Elo below SF 19 |
| 2 | Reckless | CCRL 40/15 #3, CEGT #6 |
| 3 | PlentyChess | CCRL #4-5, Blitz #5, CEGT #5 |
| 4 | pawnocchio | CCRL #4-5, CEGT #10; widest platform coverage |
| 5 | Coda | CCRL #7-8, Blitz #6 (new engine, July 2026) |
| 6 | Cinder | CCRL #9, CEGT #7 |
| 7 | Stormphrax | CCRL #11-12, CEGT #11 |
| 8 | Viridithas | CCRL #14, CEGT #14 (Hobbes, CCRL #13, is a near tie: section 5.1) |

### Dropped / not kept

| Engine | Reason |
|---|---|
| Torch, Dragon by Komodo, Fritz, Ginkgo | commercial, no GitHub binaries |
| Integral (aronpetko/integral) | **no licence file at all** (no grant to use or redistribute; never had one, checked the commit history) and Windows-only binaries since v4 |
| Ethereal (AndyGrant/Ethereal) | latest release v14.00 (2023-02-08) has **no assets**: NNUE binaries are distributed off GitHub (chess.grantnet.us, previously sold); last push 2024-08 |
| Koivisto | last release v9.0 (2023-01), repo inactive since 2024-07, no digests |
| Seer (connormcmonigle/seer-nnue) | last release v2.8.0 (2023-12-31), Windows only, no digests, CCRL #39 |
| Obsidian, Alexandria, Caissa, Berserk, Astra, Clover | strong and licensed, but **Windows-only** binaries: kept as optional Windows-only entries (section 5.2) |
| Hobbes | near tie with Viridithas; full patterns in section 5.1 if a 9th engine is wanted |
| Not considered | Stockfish derivatives (ShashChess and the like), Lc0 (needs a separate network and a GPU backend choice) |

---

## 2. Summary table

Digest = the API's `digest` field (`sha256:<hex>`) on the assets of the latest release.

| Engine | owner/repo | Licence (SPDX) | Latest tag | Published | Cadence | Linux x64 | Linux arm64 | Win x64 | Win arm64 | macOS arm64 | macOS x64 | Packaging | Network | Digest |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Gaia | jromang/gaiachess | GPL-3.0-or-later | `v4.4.4` | 2026-11-18 | every 1-3 weeks | universal / compat | neon | universal / console / compat | – | apple-silicon | – | bare | embedded | yes |
| Stockfish | official-stockfish/Stockfish | GPL-3.0-or-later | `sf_19` | 2026-09-05 | 1-2 a year, + rolling dev pre-release | universal | universal | universal | universal | universal (lipo) | universal (lipo) | **tar.gz / zip** | embedded | yes |
| Triumviratus | Tors3/Triumviratus | GPL-3.0 | `v8.1` | 2026-11-07 | about monthly since June 2026 | sse41 / avx2-nopext / avx2 / avx512 | – | universal | – | arm | x86 (AVX2) | bare | embedded (also separate) | yes |
| Reckless | codedeliveryservice/Reckless | AGPL-3.0 | `v0.9.0` | 2026-03-01 | stable every 6-12 months, dev pre-releases every 1-2 months | generic / avx2 / avx512 | – | same | – | macos | – | bare | embedded | yes |
| PlentyChess | Yoshie2000/PlentyChess | GPL-3.0 | `b-v8.0.0` | 2026-06-27 | 2-3 stable a year + dev pre-releases | 7 levels | neon | 7 levels | – | neon | – | bare | embedded | yes |
| pawnocchio | JonathanHallstrom/pawnocchio | GPL-3.0 | `v2.0.1` | 2026-06-29 | several a year + rolling pre-release | v1-v4, znver1-5 | 3 levels | v1-v4, znver1-5 | 3 levels | apple_m1 | v1, v2 | bare | embedded | yes |
| Coda | adamtwiss/coda | GPL-3.0-or-later | `v0.9.5` | 2026-10-21 | new (Jul 2026), 5 releases in 4 months | v2 / v3 | aarch64 | **v3 only** | – | aarch64 | – | bare (+ `.sha256`) | embedded | yes |
| Cinder | brunocodutra/cinder | GPL-3.0 | `v0.6.1` | 2026-08-16 | several a year | sse4 / avx2 / avx512 | neon | sse4 / avx2 / avx512 | neon | mac-neon | – | bare | embedded | yes |
| Stormphrax | Ciekce/Stormphrax | GPL-3.0 | `v8.0.0` | 2026-06-27 | about 1 a year | avx2-bmi2 / zen2 / avx512 (**AVX2 minimum**) | – | same | – | apple-m1 | – | bare | embedded | yes |
| Viridithas | cosmobobak/viridithas | **MIT for v20**, AGPL-3.0-only from v21 | `v20.0.0` | 2026-06-27 | 2-3 a year | v3 / v4 (**AVX2 minimum**) | generic / dotprod | v3 / v4 | – | aarch64 | – | bare | embedded | yes |

Typical size of one engine file: Triumviratus 246 MiB (the network is ~250 MB), Gaia 115 MiB, Coda 94, Cinder 88,
Stockfish archive 78, pawnocchio 67, Reckless 62, Viridithas 55, Stormphrax 54, PlentyChess 54 MiB.

All licences above allow downloading and personal use. The GUI downloads from the authors' own releases, so it does
not redistribute anything itself.

---

## 3. CPU requirement vocabulary used below

| Tag | Meaning |
|---|---|
| `x86-64` | any x86-64 CPU (baseline, SSE2) |
| `ssse3`, `sse41-popcnt` | those extensions (sse41-popcnt ≈ x86-64-v2 in practice) |
| `v2` | x86-64-v2: CMPXCHG16B, LAHF/SAHF, POPCNT, SSE3, SSSE3, SSE4.1, SSE4.2 |
| `fma` | AVX + FMA + SSSE3 (only PlentyChess `fma`, i.e. AMD Piledriver-class CPUs without AVX2) |
| `v3` | x86-64-v3: v2 + AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE (Haswell 2013+, any Ryzen) |
| `v3-fastpext` | v3 **and not** an AMD CPU with microcoded PEXT/PDEP (family 0x15-0x18: Excavator, Zen, Zen+, Zen 2, Hygon). Builds named `bmi2`, `pext`, `avx2-bmi2`, Triumviratus `avx2` |
| `zen12` | AMD family 0x17 only (Stormphrax `zen2`) |
| `v4` | x86-64-v4: v3 + AVX-512 F, BW, CD, DQ, VL (Skylake-X/SP and later) |
| `v4-vnni` | v4 + AVX512_VNNI (Cascade Lake and later) |
| `v4-icl` | v4 + AVX512 VBMI, VBMI2, VNNI, BITALG, VPOPCNTDQ, IFMA + GFNI (+ VAES, VPCLMULQDQ): Ice Lake, Tiger Lake, Rocket Lake, Sapphire Rapids and later, AMD Zen 4 / Zen 5. **Not** Skylake-X or Cascade Lake |
| `arm64`, `arm64-dotprod`, `arm64-i8mm` | ARMv8-A; + dot product (ARMv8.2 DotProd, all Apple Silicon); + int8 matrix multiply (Apple M2 and later) |

"avx512" means different things from one engine to the next (v4 for some, v4-icl for others); the tables below give
the real requirement, taken from each build script.

`{ver}` in the patterns stands for `[0-9]+(\.[0-9]+)*`; `{os}` for the OS token given in the row. Patterns are meant
to match the **whole** asset name (`std::regex_match`, or `^…$`). Inside tables, `\|` is only Markdown's escape for
the regex alternation `|`. Variants are listed **best first**; the picker takes the first one the CPU supports and
that exists in the release.

---

## 4. Engines kept

### 4.1 Gaia — `jromang/gaiachess`

- Licence: GPL-3.0-or-later (`Cargo.toml`; GitHub shows GPL-3.0). Latest: **`v4.4.4`, 2026-11-18**. 18 releases in 2026,
  every 1-3 weeks lately. No pre-releases. Tags `vX.Y.Z` (older ones without `v`: `4.2.1`, `4.1.3`).
- Assets of v4.4.4 (all with digest):
  ```
  gaiachess-linux-universal        gaiachess-linux-compat        gaiachess-linux-neon
  gaiachess-windows-universal.exe  gaiachess-windows-console.exe gaiachess-windows-compat.exe
  gaiachess-macos-apple-silicon    gaiachess-android-neon        gaiachess-web.zip
  THIRD-PARTY-LICENSES.txt
  ```
- Matching:

  | Platform | Variant | Pattern | Requires |
  |---|---|---|---|
  | linux-x64 | universal | `gaiachess-linux-universal` | v3 ("Haswell+, Zen+" per notes) |
  | linux-x64 | compat | `gaiachess-linux-compat` | x86-64 |
  | linux-arm64 | neon | `gaiachess-linux-neon` | arm64 |
  | win-x64 | console | `gaiachess-windows-console\.exe` | v3 |
  | win-x64 | universal | `gaiachess-windows-universal\.exe` | v3 |
  | win-x64 | compat | `gaiachess-windows-compat\.exe` | x86-64 |
  | macos-arm64 | apple-silicon | `gaiachess-macos-apple-silicon` | arm64 |

- Packaging: bare executables; network embedded (`gaianet-t7`). SIMD paths chosen at start-up (AVX2 to AVX-512+VNNI,
  PEXT or not); `compat` = portable paths. `gaiachess info` prints the selection; `GAIA_SIMD`, `GAIA_PEXT` cap it.
- Odd: the `universal` binary is also the graphical game (it plays the engine when a GUI speaks UCI to it). `console`
  is the same engine without the board, for GUIs that want a console program (ChessBase); for an engine picker it is
  the cleanest choice on Windows. **Linux binaries need `libasound2`** (sound of the built-in board). No macOS x86-64,
  no Windows arm64. Assets appear over ~30 minutes **after** the release is published (v4.4.4 published 06:38:32,
  Windows assets 07:07:59): a picker that checks in that window sees a latest release without its asset. Naming
  changed at 4.2.3 (4.2.2 had per-CPU files `gaiachess-linux-avx2`, `-znver4`…); stable since.

### 4.2 Stockfish — `official-stockfish/Stockfish`

- Licence: GPL-3.0-or-later (source headers; `Copying.txt`). Latest: **`sf_19`, 2026-09-05** (sf_18 2026-01-31,
  sf_17.1 2025-03-30). Plus one rolling dev pre-release (`stockfish-dev-20261115-5d03b6e5`, same asset names).
  Tag pattern `sf_X` or `sf_X.Y`. The sf_19 release is **immutable** (the only one of all engines here).
- Assets of sf_19 (all with digest):
  ```
  stockfish-linux-x86-64-universal.tar.gz    stockfish-linux-arm64-universal.tar.gz
  stockfish-linux-riscv64-universal.tar.gz   stockfish-macos-universal.tar.gz
  stockfish-windows-x86-64-universal.zip     stockfish-windows-arm64-universal.zip
  stockfish-android-arm64-universal.tar.gz   stockfish-android-armv7-neon.tar.gz
  ```
- Matching (one file per OS/arch since SF 19: no CPU choice):

  | Platform | Pattern | Executable inside the archive |
  |---|---|---|
  | linux-x64 | `stockfish-linux-x86-64-universal\.tar\.gz` | `stockfish/stockfish-linux-x86-64-universal` |
  | linux-arm64 | `stockfish-linux-arm64-universal\.tar\.gz` | `stockfish/stockfish-linux-arm64-universal` |
  | win-x64 | `stockfish-windows-x86-64-universal\.zip` | `stockfish/stockfish-windows-x86-64-universal.exe` |
  | win-arm64 | `stockfish-windows-arm64-universal\.zip` | `stockfish/stockfish-windows-arm64-universal.exe` |
  | macos (both) | `stockfish-macos-universal\.tar\.gz` | `stockfish/stockfish-macos-universal` |

  Rule: `stockfish/` + asset name without `.tar.gz`/`.zip` + `.exe` on Windows (from `upload_binaries.yml` at
  sf_19). The `stockfish/` folder also holds `src/`, `scripts/`, `wiki/`, `Copying.txt`, `AUTHORS`, `README.md`…
- Packaging: **tar.gz (Linux, macOS, Android) and zip (Windows)**; the only kept engine with archives. The tar has the
  exec bit set. Network embedded (once). x86-64 universal = 8 builds in one file chosen at run time (x86-64,
  sse41-popcnt, avx2, bmi2, avxvnni, avx512, vnni512, avx512icl); arm64 universal = armv8 + armv8-dotprod; macOS =
  lipo of arm64 and x86-64-universal.
- Odd: **the naming changed completely with SF 19.** sf_18 had 27 per-CPU archives (`stockfish-ubuntu-x86-64-avx2.tar`,
  `stockfish-windows-x86-64-bmi2.zip`, `stockfish-macos-m1-apple-silicon.tar`, plain `.tar`, `ubuntu` not `linux`).
  Linux binary built on Ubuntu 22.04 with libstdc++/libgcc static but glibc dynamic (Makefile at sf_19), so it needs a
  glibc of that age or newer (not checked on the binary).

### 4.3 Triumviratus — `Tors3/Triumviratus`

- Licence: GPL-3.0. Latest: **`v8.1`, 2026-11-07** (8.0 2026-09-30, 7.0 2026-09-10, 6.0 2026-07-16…: about monthly).
  Author Francesco Torsello. Tags `vX.Y`.
- Assets of v8.1 (all with digest):
  ```
  Triumviratus_8.1_windows_universal.exe
  Triumviratus_8.1_linux_sse41   Triumviratus_8.1_linux_avx2-nopext   Triumviratus_8.1_linux_avx2   Triumviratus_8.1_linux_avx512
  Triumviratus_8.1_macos_arm     Triumviratus_8.1_macos_x86
  nn-consilium-plebis.nnue       SHA256SUMS.txt   SHA256SUMS_linux.txt   SHA256SUMS_macos.txt
  ```
- Matching (tag must match `v{ver}`):

  | Platform | Variant | Pattern | Requires |
  |---|---|---|---|
  | win-x64 | universal | `Triumviratus_{ver}_windows_universal\.exe` | sse41-popcnt (7 builds inside, chosen at start-up up to AVX-512 VBMI2) |
  | linux-x64 | avx512 | `Triumviratus_{ver}_linux_avx512` | v4 ("AVX-512 without VNNI, Skylake-SP") |
  | linux-x64 | avx2 | `Triumviratus_{ver}_linux_avx2` | v3-fastpext |
  | linux-x64 | avx2-nopext | `Triumviratus_{ver}_linux_avx2-nopext` | v3 |
  | linux-x64 | sse41 | `Triumviratus_{ver}_linux_sse41` | sse41-popcnt |
  | macos-arm64 | arm | `Triumviratus_{ver}_macos_arm` | arm64 (`-mcpu=apple-m1`, dotprod) |
  | macos-x64 | x86 | `Triumviratus_{ver}_macos_x86` | v3 (AVX2, Haswell+) |

- Packaging: bare executables, network embedded in each (hence ~259 MB per file); the network is also an asset
  (`nn-consilium-plebis.nnue`, not needed) and lives in `Tors3/Triumviratus-Networks` (release `consilium-plebis`).
  Linux builds statically linked ("runs on any distribution", release notes). `TRIUMV_ISA` forces a build.
- Odd: **side releases that are not pre-releases**: `v8.1-possolver` ("Position Solver", published 37 minutes after
  v8.1) and `v8.0-possolver`, with assets `Triumviratus_8.1_PosSolver_linux_avx2` etc. `/releases/latest` returns `v8.1`
  today, but the picker must check the tag (`^v[0-9.]+$`) and anchor the patterns (`_{ver}_linux_` excludes
  `_PosSolver_`). Dated pre-release builds name files `Triumviratus_8.1_20261028_linux_avx2` (workflow). Up to 8.0 the
  engine contained Stockfish code (NNUE inference port, search on SF 19's base, README); 8.1 claims its own inference
  and search. The author recommends large pages (250 MB network). Not tested by CEGT.

### 4.4 Reckless — `codedeliveryservice/Reckless`

- Licence: AGPL-3.0 (since 0.9.0; earlier releases were under another licence). Latest stable: **`v0.9.0`,
  2026-03-01** (0.8.0 2025-08-30, 0.7.0 2024-08-23). **Dev pre-releases** every month or two in between
  (latest `v0.10.0-dev-7300f044`, 2026-05-01, same asset names). Tags `vX.Y.Z`.
- Assets of v0.9.0 (all with digest):
  ```
  reckless-linux-generic   reckless-linux-avx2   reckless-linux-avx512
  reckless-windows-generic.exe   reckless-windows-avx2.exe   reckless-windows-avx512.exe
  reckless-macos
  ```
- Matching:

  | Platform | Variant | Pattern | Requires |
  |---|---|---|---|
  | linux-x64 | avx512 | `reckless-linux-avx512` | v4-icl (x86-64-v4 + GFNI, VBMI, VBMI2, VNNI, BITALG) |
  | linux-x64 | avx2 | `reckless-linux-avx2` | v3 |
  | linux-x64 | generic | `reckless-linux-generic` | x86-64 |
  | win-x64 | same three | `reckless-windows-(avx512\|avx2\|generic)\.exe` | same |
  | macos-arm64 | macos | `reckless-macos` | arm64 |

- Packaging: bare; network embedded (`include_bytes!`), from `codedeliveryservice/RecklessNetworks` (release
  `networks`, `v54-5478683c.nnue`). PGO builds on GitHub Actions; Linux = glibc dynamic, built on `ubuntu-latest`.
- Odd: release notes say "On macOS, a single universal build is provided", but the workflow builds `reckless-macos`
  on `macos-latest` (Apple Silicon) with `target-cpu=native`: **arm64 only** as far as the build script shows (not
  checked on the binary). No version in asset names. No Linux/Windows arm64.

### 4.5 PlentyChess — `Yoshie2000/PlentyChess`

- Licence: GPL-3.0. Latest stable: **`b-v8.0.0`, 2026-06-27** (7.0.0 2025-09-25, 6.0.0 2025-06-06, 5.0.0 2025-03-23).
  **Dev pre-releases** between (`b-v7.0.37`, `b-7.0.22`…). Tag prefix `b-v`, but not always (`b-7.0.22`).
- Assets of 8.0.0 (all with digest): `PlentyChess-8.0.0-<os>-<level>[.exe]`
  ```
  linux-  generic ssse3 fma avx2 bmi2 avx512 avx512vbmi2 neon
  windows-generic ssse3 fma avx2 bmi2 avx512 avx512vbmi2  (.exe)
  macos-neon   android-neon
  ```
- Matching (`os` = `linux` or `windows`, `.exe` on Windows):

  | Variant | Pattern | Requires (from the makefile) |
  |---|---|---|
  | avx512vbmi2 | `PlentyChess-{ver}-{os}-avx512vbmi2` | v4-icl (`-march=icelake-client`) |
  | avx512vnni (7.x only) | `PlentyChess-{ver}-{os}-avx512vnni` | v4-vnni (not in 8.0.0; flags not checked) |
  | avx512 | `PlentyChess-{ver}-{os}-avx512` | v4 (`-march=skylake-avx512`) |
  | bmi2 | `PlentyChess-{ver}-{os}-bmi2` | v3-fastpext (`-march=haswell`, PEXT used) |
  | avx2 | `PlentyChess-{ver}-{os}-avx2` | v3 (`-march=haswell`) |
  | fma | `PlentyChess-{ver}-{os}-fma` | fma |
  | ssse3 | `PlentyChess-{ver}-{os}-ssse3` | ssse3 |
  | generic | `PlentyChess-{ver}-{os}-generic` | x86-64 |
  | linux-arm64 | `PlentyChess-{ver}-linux-neon` | arm64 |
  | macos-arm64 | `PlentyChess-{ver}-macos-neon` | arm64 |

- Packaging: bare; network embedded (net `0178r` from `Yoshie2000/PlentyNetworks` releases).
- Odd: **the set of levels changes between releases** (7.x had `avx512vnni`, 8.0.0 replaced it with `avx512vbmi2`;
  6.0.0 had an extra `windows-avx512-pgo.exe`). The x86 builds are made by the author locally (the CI only builds the
  neon/android ones), so their linking is unknown; the makefile links **`-lnuma` when `numa.h` is present**, so the
  Linux builds may need `libnuma.so.1` (not verified). Assets were uploaded on 06-19, the release published on 06-27. Author's advice: "start with avx512vbmi2 and take the first one that does not crash".

### 4.6 pawnocchio — `JonathanHallstrom/pawnocchio`

- Licence: GPL-3.0. Latest: **`v2.0.1`, 2026-06-29** (v2.0 06-27, v1.9.2 2026-01-15, v1.8.1 2025-07-25). Zig engine.
  Rolling pre-releases `pre-release-builds` (its workflow deletes and recreates the tag each time; names
  `pawnocchio-3.0-dev-ae35c6a-…`) and `tool-dev-builds`. Tags `vX.Y[.Z]` (one side tag `v1.9-hce`, pre-release).
- Assets of v2.0.1 (27, all with digest): `pawnocchio-2.0.1-<os>-<arch>[_level][.exe]`
  ```
  linux-x86_64  linux-x86_64_v2  _v3  _v4  _znver1 … _znver5
  windows-x86_64.exe  windows-x86_64_v2.exe  _v3  _v4  _znver1 … _znver5 (.exe)
  linux-aarch64  linux-aarch64-dotprod  linux-aarch64-modern
  windows-aarch64.exe  windows-aarch64-dotprod.exe  windows-aarch64-modern.exe
  macos-aarch64-apple_m1  macos-x86_64  macos-x86_64_v2
  ```
- Matching (from `build/release.zig`):

  | Platform | Variants (best first) | Pattern | Requires |
  |---|---|---|---|
  | linux/win x64 | `_znver5`…`_znver1` | `pawnocchio-{ver}-{os}-x86_64_znver[1-5]` (`.exe`) | the matching AMD Zen generation (use only when family/model says so) |
  | linux/win x64 | `_v4`, `_v3`, `_v2`, none | `pawnocchio-{ver}-{os}-x86_64(_v[234])?` (`.exe`) | v4 / v3 / v2 / x86-64 |
  | linux/win arm64 | `-modern`, `-dotprod`, none | `pawnocchio-{ver}-{os}-aarch64(-modern\|-dotprod)?` (`.exe`) | arm64-i8mm (+dotprod) / arm64-dotprod / arm64 |
  | macos-arm64 | apple_m1 | `pawnocchio-{ver}-macos-aarch64-apple_m1` | arm64 |
  | macos-x64 | `_v2`, none | `pawnocchio-{ver}-macos-x86_64(_v2)?` | v2 / x86-64 |

- Packaging: bare; network embedded (submodule `pawnocchio-nets`, `pp_new2.nnue`). All cross-compiled by Zig on Linux;
  **Linux builds static**, macOS dynamic (system libraries), Windows link the C runtime.
- Odd: the only engine with Windows arm64 *and* macOS x86-64 *and* Linux arm64. `{os}` is `linux`/`windows`/`macos`,
  arch `x86_64` with an underscore (Coda and Viridithas use `x86-64`).

### 4.7 Coda — `adamtwiss/coda`

- Licence: GPL-3.0-or-later (`Cargo.toml`). Latest: **`v0.9.5`, 2026-10-21** (0.9.4 08-22, 0.9.3 07-26, 0.9.2 07-16,
  0.9.1 07-14; 0.9.0 on 07-06 was a pre-release). Rust. Author Adam Twiss. Tags `vX.Y.Z`; **network pre-releases** `v0.9-nets`,
  `v0.9.1-nets`… in the same repo.
- Assets of v0.9.5 (all with digest), each with a `.sha256` side file:
  ```
  coda-0.9.5-linux-x86-64-v2   coda-0.9.5-linux-x86-64-v3   coda-0.9.5-linux-aarch64
  coda-0.9.5-windows-x86-64-v3.exe   coda-0.9.5-macos-aarch64
  (+ <name>.sha256 for each)   THIRD_PARTY_LICENSES.md
  ```
- Matching:

  | Platform | Variant | Pattern | Requires |
  |---|---|---|---|
  | linux-x64 | v3 | `coda-{ver}-linux-x86-64-v3` | v3 |
  | linux-x64 | v2 | `coda-{ver}-linux-x86-64-v2` | v2 |
  | linux-arm64 | aarch64 | `coda-{ver}-linux-aarch64` | arm64 |
  | win-x64 | v3 | `coda-{ver}-windows-x86-64-v3\.exe` | **v3 (no Windows build for older CPUs)** |
  | macos-arm64 | aarch64 | `coda-{ver}-macos-aarch64` | arm64 (`target-cpu=apple-m1`) |

- Packaging: bare; network embedded (`net-0F08CC1B.nnue`, from the repo's `v0.9-nets` pre-release). NNUE kernels
  (AVX2, AVX-512, VNNI, PEXT, NEON) chosen at run time inside each binary; v2/v3 only sets general code generation.
  **Linux builds static musl**, Windows static CRT (from `release.yml`).
- Odd: anchored patterns are needed to skip the `.sha256` side files. Young project (4 months), very active.

### 4.8 Cinder — `brunocodutra/cinder`

- Licence: GPL-3.0 (`Cargo.toml` "GPL-3.0", i.e. -only). Latest: **`v0.6.1`, 2026-08-16** (0.6.0 08-16, 0.5.2 07-12,
  0.4.1 2025-12-05). Rust. Tags `vX.Y.Z`.
- Assets of v0.6.1 (all with digest):
  ```
  cinder-v0.6.1-linux-sse4   cinder-v0.6.1-linux-avx2   cinder-v0.6.1-linux-avx512   cinder-v0.6.1-linux-neon
  cinder-v0.6.1-windows-sse4.exe   cinder-v0.6.1-windows-avx2.exe   cinder-v0.6.1-windows-avx512.exe   cinder-v0.6.1-windows-neon.exe
  cinder-v0.6.1-mac-neon
  ```
- Matching (from the Makefile):

  | Platform | Variant | Pattern | Requires |
  |---|---|---|---|
  | linux/win x64 | avx512 | `cinder-v{ver}-{os}-avx512` (`.exe`) | v4-icl (x86-64-v4 + GFNI, IFMA, BITALG, VBMI, VBMI2, VNNI, VPOPCNTDQ) |
  | linux/win x64 | avx2 | `cinder-v{ver}-{os}-avx2` (`.exe`) | v3 |
  | linux/win x64 | sse4 | `cinder-v{ver}-{os}-sse4` (`.exe`) | v2 |
  | linux-arm64 | neon | `cinder-v{ver}-linux-neon` | arm64-dotprod |
  | win-arm64 | neon | `cinder-v{ver}-windows-neon\.exe` | arm64-dotprod |
  | macos-arm64 | neon | `cinder-v{ver}-mac-neon` | arm64 |

- Packaging: bare; network embedded (in the repository, `lib/nnue/nnue.bin.zst`). Linux x86 = glibc dynamic
  (`x86_64-unknown-linux-gnu` on `ubuntu-latest`), Linux arm64 = static musl.
- Odd: `v` inside the asset name, macOS token is **`mac`** (not `macos`), `neon` means dotprod. Content type
  `binary/octet-stream` (do not rely on `content_type`).

### 4.9 Stormphrax — `Ciekce/Stormphrax`

- Licence: GPL-3.0. Latest: **`v8.0.0`, 2026-06-27** (7.0.0 2025-06-24, 6.0.0 2024-10-29): about one a year.
  Tags `vX.Y.Z`. No CI in the repository: builds made by the author (Apple build by a contributor, @yl25946).
- Assets of 8.0.0 (all with digest):
  ```
  stormphrax-8.0.0-avx2-bmi2   stormphrax-8.0.0-avx512   stormphrax-8.0.0-zen2
  stormphrax-8.0.0-avx2-bmi2.exe   stormphrax-8.0.0-avx512.exe   stormphrax-8.0.0-zen2.exe
  stormphrax-8.0.0-apple-m1
  ```
- Matching (no OS token: no extension = Linux, `.exe` = Windows, `apple-m1` = macOS):

  | Platform | Variant | Pattern | Requires (`build.mk`) |
  |---|---|---|---|
  | linux/win x64 | avx512 | `stormphrax-{ver}-avx512` (`.exe`) | v4-icl (`-march=icelake-client`; notes: "requires VNNI and VBMI2") |
  | linux/win x64 | avx2-bmi2 | `stormphrax-{ver}-avx2-bmi2` (`.exe`) | v3-fastpext (`-march=haswell`) |
  | linux/win x64 | zen2 | `stormphrax-{ver}-zen2` (`.exe`) | zen12 (`-march=bdver4`; "Ryzen 3xxx or earlier") |
  | macos-arm64 | apple-m1 | `stormphrax-{ver}-apple-m1` | arm64 |

- Packaging: bare; network embedded (`undertown`, from `Ciekce/stormphrax-nets` releases). libnuma only when built
  with `USE_LIBNUMA=on` (unknown for the release builds).
- Odd: **no build below AVX2** (an Intel CPU before Haswell gets nothing); no OS token in names; no Linux arm64.

### 4.10 Viridithas — `cosmobobak/viridithas`

- Licence: **MIT for v20.0.0** (the LICENSE file at that tag), **AGPL-3.0-only from v21** (relicensed 2026-07-06;
  GitHub's repo-level licence already says AGPL-3.0). Latest: **`v20.0.0`, 2026-06-27** (19.0.1 2026-01-04, 18.0.0
  2025-08-25, 17.0.0 2025-04-10). Rust. Tags `vX.Y.Z`.
- Assets of v20 (all with digest):
  ```
  viridithas-20-linux-x86-64-v3   viridithas-20-linux-x86-64-v4
  viridithas-20-win-x86-64-v3.exe   viridithas-20-win-x86-64-v4.exe
  viridithas-20-linux-aarch64-generic   viridithas-20-linux-aarch64-dotprod   viridithas-20-macos-aarch64
  ```
- Matching (written to accept both the v18-v20 names and the names of the current CI workflow, see Odd):

  | Platform | Variant | Pattern | Requires (v20 Makefile) |
  |---|---|---|---|
  | linux-x64 | v4…v1 | `viridithas(-{ver})?-linux-x86[-_]64-v[1-4]` | v4 = v4-icl (x86-64-v4 + GFNI, VBMI, VBMI2, VNNI, BITALG); v3; v2; v1 |
  | win-x64 | v4…v1 | `viridithas(-{ver})?-win(dows)?-x86[-_]64-v[1-4]\.exe` | same |
  | linux-arm64 | dotprod | `viridithas(-{ver})?(-linux)?-aarch64-dotprod` | arm64-dotprod |
  | linux-arm64 | generic | `viridithas(-{ver})?(-linux)?-aarch64(-generic)?` | arm64 |
  | macos-arm64 | aarch64 | `viridithas(-{ver})?-macos-aarch64` | arm64 |

- Packaging: bare; network embedded (from `cosmobobak/viridithas-networks` releases). Built with `+crt-static`
  (static, also on Linux), PGO.
- Odd: **v20 has no v1/v2 build** (19.0.1 had v2-v4, 17 had v1-v4): AVX2 minimum now. Version in names is `20`
  (not `20.0.0`), Windows token `win`. The arm64 and macOS files were added **3.5 weeks after** the release
  (2026-07-21). The CI workflow added since (`release.yml`, July 2026) names files `viridithas-linux-x86_64-v3`,
  `viridithas-windows-x86_64-v3.exe`, `viridithas-aarch64-dotprod` (no version, underscore, `windows`), so **v21 may
  use different names**: hence the tolerant patterns above.

---

## 5. Runner-up and Windows-only engines

### 5.1 Hobbes — `kelseyde/hobbes-chess-engine` (cross-platform, near tie with Viridithas)

- MIT. Latest **`4.0`, 2026-11-13** (3.0 07-22, 2.1 05-26, 2.0 05-25, 1.0 03-05). Rust, author Dan Kelsey. Tags `X.Y`.
- Assets (digest yes): `hobbes-linux-avx2`, `hobbes-linux-avx512`, `hobbes-windows-avx2.exe`,
  `hobbes-windows-avx512.exe`, `hobbes-macos`. Patterns: `hobbes-(linux|windows)-(avx512|avx2)(\.exe)?`,
  `hobbes-macos` (arm64, `apple-m1`). avx2 = v3, avx512 = v4 (Windows).
- Network embedded (`hobbes-48`, from `kelseyde/hobbes-networks`). Linux glibc dynamic.
- Odd: **`hobbes-linux-avx512` is byte-identical to `hobbes-linux-avx2`** (same digest in 2.1, 3.0 and 4.0: the CI
  pins v3 flags for both Linux jobs). No version in names.

### 5.2 Windows-only engines (no Linux or macOS binaries in their releases)

| Engine | Repo, licence | Latest | Assets (exact) | Pattern | Requires | Digest | Notes |
|---|---|---|---|---|---|---|---|
| Obsidian | gab8192/Obsidian, GPL-3.0 | `v16.0` 2025-05-21 | `Obsidian160-avx512.exe`, `Obsidian160-avx2-pext.exe`, `Obsidian160-avx2.exe` | `Obsidian[0-9]+-(avx512\|avx2-pext\|avx2)\.exe` | avx512 = AVX512F+BW; avx2-pext = v3-fastpext; avx2 = AVX2+FMA+BMI1 | **no** (uploaded before June 2025) | version without dot (`160`); no release for 18 months; CCRL Blitz #4 |
| Alexandria | PGG106/Alexandria, GPL-3.0 | `v9.1.0` 2026-10-04 | `Alexandria-9.1.0-avx512.exe`, `-bmi2.exe`, `-avx2.exe` (+ 2 Android files) | `Alexandria-{ver}-(avx512\|bmi2\|avx2)\.exe` | avx512 = AVX512F+BW (+PEXT); bmi2 = v3-fastpext; avx2 = AVX2+FMA+BMI1 | yes | `android-nightly` pre-release; the two Android files are identical |
| Caissa | Witek902/Caissa, MIT | `2.0` 2026-09-19 | `caissa-2.0-x64-{avx512icl,avx512,avx2-bmi2,avx2,sse4-popcnt,sse2}.exe` | `caissa-{ver}-x64-(avx512icl\|avx512\|avx2-bmi2\|avx2\|sse4-popcnt\|sse2)\.exe` | v4-icl / v4 / v3-fastpext / v3 / sse41-popcnt / x86-64 | yes | regular (4 in 12 months) |
| Berserk | jhonnold/berserk, GPL-3.0 | `14` 2026-05-24 | `berserk-14-{avx512-pext,avx2-pext,avx2,sse41,x86-64}.exe`, `berserk-9b84c340af7e.nn`, `berserk.jpg` | `berserk-[0-9]+(\.[0-9]+)?-(avx512-pext\|avx2-pext\|avx2\|sse41\|x86-64)\.exe` | all need POPCNT; `pext` = BMI2 (avoid on Zen 1/2); avx512 = AVX512F+BW | yes | network embedded and also attached; mingw cross-builds without PGO; **side release `4.7.0` (HCE, published after 14)**: require major ≥ 13 |
| Astra | h1me01/Astra, GPL-3.0 | `7.0` 2026-05-26 | `astra-7.0-{avx512-vnni,avx512,avx2-bmi2,avx2}.exe` | `astra-{ver}-(avx512-vnni\|avx512\|avx2-bmi2\|avx2)\.exe` | v4-vnni / v4 / v3-fastpext / v3 (from names) | yes | 1 release in the last 12 months |
| Clover | lucametehau/CloverEngine, GPL-3.0 | `v9.1` 2025-09-14 | `Clover.9.1-avx512.exe`, `Clover.9.1-avx2.exe`, `Clover.9.1-old.exe` | `Clover\.{ver}-(avx512\|avx2\|old)\.exe` | avx2 = `core-avx2`; old = `core2` (SSSE3); **avx512 built with `-march=native`** on the author's PC + AVX512F/BW/DQ (exact need unknown) | yes | author paused development |

---

## 6. Pitfalls the picker should handle

1. **`/releases/latest` can point at a side release.** It returns the newest non-draft, non-prerelease release
   (by the commit's `created_at`, unless the maintainer sets "latest" by hand). Triumviratus (`v8.1-possolver`) and
   Berserk (`4.7.0` HCE) publish ordinary releases next to the engine; today both are correctly *not* latest, but
   nothing guarantees it. Give each catalogue entry a **tag pattern**; if `latest` does not match it (or has none of
   the expected assets), list `/releases?per_page=20` and take the first non-draft, non-prerelease release that does.
2. **Anchor every asset pattern** (whole-name match): Coda's `.sha256` side files, Triumviratus' `_PosSolver_` files.
3. **The set of variants changes between releases** (PlentyChess `avx512vnni` → `avx512vbmi2`; Viridithas dropped
   v1/v2; Stockfish went from 27 per-CPU archives to 8 universal ones; Gaia 4.2.2 → 4.2.3). Keep an ordered list of
   known variant tokens with their CPU requirement, take the best one present, and treat "no variant matches" as
   "keep the installed version and report the change", not as an error that removes the engine.
4. **Names may change** (Viridithas' CI names differ from v20's), so prefer tolerant patterns where a change is
   announced by the build scripts.
5. **Assets arrive after the release is published** (Gaia: +30 min; Triumviratus: Windows first, Linux +23 min, macOS
   +35 min; Viridithas arm64: +3.5 weeks). If the wanted asset is missing, retry later. Use only assets with
   `state == "uploaded"`.
6. **Releases are mutable** except Stockfish's (`immutable: true`). An asset can be replaced under the same name:
   cache by asset `id` + `digest`, not by tag.
7. **Digest**: present on every asset of the latest release of all kept engines. It is `null` for assets uploaded
   before 2025-06-03 (Obsidian 16.0, Seer, Koivisto); GitHub did not backfill. Triumviratus also publishes
   `SHA256SUMS*.txt`, Coda a `.sha256` per file.
8. **`content_type` is unreliable** (`binary/octet-stream`, `application/x-msdos-program`, `application/octet-stream`
   for `.exe`): decide archive vs executable by the name.
9. **Linux runtime dependencies**: static builds (Triumviratus, pawnocchio, Coda, Viridithas, Cinder arm64) run
   anywhere; glibc-dynamic builds from `ubuntu-latest`/22.04 runners (Stockfish, Reckless, Cinder x86, Hobbes) need a
   recent glibc; PlentyChess may need `libnuma`; Gaia needs `libasound2`. From build scripts, not checked on the
   binaries. A robust picker test-runs the file (`uci` → `uciok` within a few seconds) and falls back to the next
   variant on failure (SIGILL, loader error, timeout).
10. **Executable bit / macOS**: `chmod +x` bare files on Linux and macOS (Stockfish's tar keeps the bit). A file the
    app downloads itself gets no `com.apple.quarantine` attribute unless the app opts in (LSFileQuarantineEnabled),
    so Gatekeeper is not involved; arm64 binaries must carry at least an ad-hoc signature, which the toolchains used
    here add (general macOS behaviour, not tested here or per binary).
11. **Download sizes**: up to ~259 MB per Triumviratus file; show `size` before downloading.

---

## 7. GitHub REST API for the picker

**Endpoints**

| Purpose | Call |
|---|---|
| Latest stable release | `GET https://api.github.com/repos/{owner}/{repo}/releases/latest` (404 when the repo has none) |
| Fallback, pre-release channel, side releases | `GET /repos/{owner}/{repo}/releases?per_page=20` (`per_page` max 100). Roughly newest first, but not strictly (Berserk lists `14` before the later `4.7.0`): sort by `published_at` or by the version in the tag yourself |
| A given tag | `GET /repos/{owner}/{repo}/releases/tags/{tag}` |
| Download | the asset's **`browser_download_url`** (`https://github.com/{owner}/{repo}/releases/download/{tag}/{name}`): a `302` to a signed, short-lived `release-assets.githubusercontent.com` URL. Follow redirects. **Not counted in the API rate limit** (checked: no `x-ratelimit-*` headers on it). The API alternative `GET /repos/{owner}/{repo}/releases/assets/{id}` with `Accept: application/octet-stream` costs an API call. |

**Release fields to use**: `tag_name`, `name`, `prerelease`, `draft`, `published_at`, `created_at`, `immutable`,
`html_url`, `body` (release notes, Markdown), `assets[]`.

**Asset fields** (schema of the docs, all seen in the responses): `id`, `name`, `label`, `state` (`uploaded` |
`open`), `content_type`, `size` (bytes), `digest` (`"sha256:<64 hex>"` or `null`), `download_count`, `created_at`,
`updated_at`, `browser_download_url`, `url` (API URL), `uploader`, `node_id`.

**Headers**

- `Accept: application/vnd.github+json` (recommended by the docs).
- `User-Agent:` **required**; "requests with no User-Agent are rejected"; GitHub asks for the application's name (or
  a GitHub user name), e.g. `RookStudio/0.7`.
- `X-GitHub-Api-Version: 2022-11-28` (the default when the header is absent; supported until 2028-03-10) or
  `2026-03-10` (the newest; its breaking changes do not touch the releases endpoints).

**Rate limits**

- Unauthenticated: **60 requests per hour per IP address** (checked: `x-ratelimit-limit: 60`, resource `core`).
  Authenticated (a token the user pastes): 5,000 per hour.
- Headers on every response: `x-ratelimit-limit`, `-remaining`, `-used`, `-reset` (UTC epoch seconds), `-resource`.
- Exceeded: `403` or `429` with `x-ratelimit-remaining: 0`; wait until `x-ratelimit-reset` (or `retry-after` when
  present, for secondary limits). Secondary limits (100 concurrent requests, 900 points/min) do not matter here.
- Conditional requests (`If-None-Match: <etag>` → `304`) are free **only when authenticated**; unauthenticated 304s
  still count. Unauthenticated responses carry `cache-control: public, max-age=60` and an `etag`.
- Budget: one call per engine per check (10-17 calls), so check at most every few hours or on demand, cache the
  answer, and degrade to the cached catalogue on 403/429. Users behind a shared NAT share the 60.

**Suggested flow per engine**

```
GET /releases/latest
  → if 404, tag not matching the entry's tag pattern, or no asset matching: GET /releases?per_page=20 and take the
    first release with draft=false, prerelease=false (or prerelease=true for a "dev" channel), matching tag and asset
variants = entry.variants[os-arch] filtered by CPU support, best first
asset    = first variant whose pattern matches an asset with state="uploaded"
download browser_download_url → check size, then sha256 against digest (if not null)
extract (Stockfish only: tar.gz / zip, exe at stockfish/<asset stem>[.exe]) → chmod +x → test "uci"/"uciok"
cache (engine, tag, asset id, digest)
```

---

## 8. Detecting the CPU at run time (short)

**CPUID bits** (leaf.subleaf, register, bit):

| Feature | Where | Feature | Where |
|---|---|---|---|
| SSE3 | 1, ECX 0 | BMI1 | 7.0, EBX 3 |
| SSSE3 | 1, ECX 9 | AVX2 | 7.0, EBX 5 |
| FMA | 1, ECX 12 | BMI2 | 7.0, EBX 8 |
| CMPXCHG16B | 1, ECX 13 | AVX512F | 7.0, EBX 16 |
| SSE4.1 | 1, ECX 19 | AVX512DQ | 7.0, EBX 17 |
| SSE4.2 | 1, ECX 20 | AVX512IFMA | 7.0, EBX 21 |
| MOVBE | 1, ECX 22 | AVX512CD | 7.0, EBX 28 |
| POPCNT | 1, ECX 23 | AVX512BW | 7.0, EBX 30 |
| OSXSAVE | 1, ECX 27 | AVX512VL | 7.0, EBX 31 |
| AVX | 1, ECX 28 | AVX512VBMI | 7.0, ECX 1 |
| F16C | 1, ECX 29 | AVX512VBMI2 | 7.0, ECX 6 |
| LAHF/SAHF | 0x80000001, ECX 0 | GFNI | 7.0, ECX 8 |
| LZCNT (ABM) | 0x80000001, ECX 5 | AVX512VNNI | 7.0, ECX 11 |
| AVX-VNNI (VEX) | 7.1, EAX 4 | AVX512BITALG / VPOPCNTDQ | 7.0, ECX 12 / 14 |

**OS support**: if OSXSAVE, read `XGETBV(0)`: AVX family usable when `(XCR0 & 0x06) == 0x06`; AVX-512 when
`(XCR0 & 0xE6) == 0xE6` (opmask + ZMM state). **Levels**: v2 = CMPXCHG16B, LAHF/SAHF, POPCNT, SSE3, SSSE3, SSE4.1,
SSE4.2; v3 = v2 + AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE + AVX OS state; v4 = v3 + AVX512 F, BW, CD, DQ, VL +
AVX-512 OS state (the same rules as LLVM's `compiler-rt/lib/builtins/cpu_model/x86.c`). **Slow PEXT**: vendor
`AuthenticAMD` or `HygonGenuine` with family (base 0xF + extended) 0x15-0x18; Zen 3 and later (0x19, 0x1A) are fast.

**Per OS**

- **Linux**: CPUID + XGETBV as above (the kernel enables the states eagerly). Simplest with GCC/Clang:
  `__builtin_cpu_supports("x86-64-v3")`, `("x86-64-v4")`, `("avx2")`, `("bmi2")`, `("avx512f")`, `("avx512vnni")`,
  `("avx512vbmi2")`, `("avxvnni")` (all compiled and ran here with GCC 16.2 and Clang 23.1; the `x86-64-vN` names
  need a recent compiler). `/proc/cpuinfo` flags as a cross-check. arm64: `getauxval(AT_HWCAP) & HWCAP_ASIMDDP`
  (dotprod), `getauxval(AT_HWCAP2) & HWCAP2_I8MM`.
- **Windows**: the same CPUID/XGETBV (`<cpuid.h>` and the builtins with MinGW; `__cpuidex`/`_xgetbv` with MSVC).
  `IsProcessorFeaturePresent` only knows PF_SSSE3 (36) … PF_AVX2 (40), PF_AVX512F (41): nothing for BMI2, VNNI or
  VBMI2, so it is not enough. An x64 GUI on an ARM64 PC: `IsWow64Process2` returns the native machine
  (`IMAGE_FILE_MACHINE_ARM64`) → prefer arm64 engines (Stockfish, pawnocchio, Cinder have them);
  `PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE` (43), `PF_ARM_V82_I8MM_INSTRUCTIONS_AVAILABLE` (66).
- **macOS**: Intel: CPUID works, but **macOS enables the AVX-512 state lazily**, so XCR0 bits 5-7 read 0 until first
  use: take AVX-512 from CPUID or `sysctlbyname("hw.optional.avx512f")` (LLVM's `__builtin_cpu_supports` already
  trusts CPUID on Darwin; Go additionally refuses AVX-512 before Darwin 21.3 / macOS 12.2 because of a kernel bug
  corrupting mask registers). Apple Silicon: `hw.optional.arm64` = 1; `sysctl.proc_translated` = 1 means the GUI
  itself runs under Rosetta, still pick arm64 engines; `hw.optional.arm.FEAT_DotProd` (all M-series),
  `hw.optional.arm.FEAT_I8MM` (M2+). Rosetta reportedly translates AVX/AVX2 since macOS 15 (not AVX-512); native arm64
  builds exist for every kept engine anyway.

---

## 9. What could not be verified

- Anything inside the binaries: static or dynamic linking, required glibc, libnuma (PlentyChess), the real
  architecture of `reckless-macos` (arm64 per workflow, "universal" per notes), code signatures. Taken from build
  scripts and release notes only.
- Stockfish's archive layout (`stockfish/<asset stem>[.exe]`) comes from `upload_binaries.yml` at `sf_19`, not from
  opening an archive.
- Build flags of releases built by hand (PlentyChess x86, Stormphrax, Viridithas v20 x86, Obsidian, Alexandria,
  Caissa, Astra, Clover): taken from the makefile at the release tag, assuming the author used it as-is. PlentyChess
  7.x `avx512vnni` flags and Astra's flags were not checked.
- Whether `/releases/latest` ever pointed at Triumviratus' `-possolver` or Berserk's `4.7.0` when they came out (the
  API does not show the "make latest" setting).
- Rosetta AVX2 support (press and community reports only).
- CEGT's 40/20 page shows "generated 15 Nov 2026"; the CCRL dates are the lists' own.
