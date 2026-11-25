cmake_minimum_required(VERSION 3.24)

# macOS 13.3 (Ventura) and later, the first whose C++ library formats floating-point numbers (std::format). Before
# project(), where CMake reads it; nothing elsewhere. A universal binary: -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "The oldest macOS version the programs run on")

project(fastchess_gui VERSION 0.7.0 LANGUAGES C CXX)

# A preview's version carries a suffix (0.7.0-preview.1). Built for a tag, it is read from the tag, whose number must
# be the project's: a tag v0.7.1-preview.1 on a 0.7.0 tree stops the build.
set(FCG_VERSION_SUFFIX "" CACHE STRING "Appended to the version the program shows, as -preview.1")
if(FCG_VERSION_SUFFIX STREQUAL "" AND "$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
    if("$ENV{GITHUB_REF_NAME}" MATCHES "^v${PROJECT_VERSION}(-.+)$")
        set(FCG_VERSION_SUFFIX "${CMAKE_MATCH_1}")
    elseif(NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "v${PROJECT_VERSION}")
        message(FATAL_ERROR "The tag $ENV{GITHUB_REF_NAME} does not name version ${PROJECT_VERSION}")
    endif()
endif()

option(FCG_BUILD_GUI "Build the fastchess-gui desktop application" ON)
option(FCG_BUILD_TESTS "Build the fastchess-gui unit tests" ON)
option(FCG_STATIC_RUNTIME "Link libstdc++ and libgcc statically on Linux (release binaries)" OFF)
option(FCG_FUZZ "Build the libFuzzer targets in fuzz/ (clang), everything with AddressSanitizer and UBSan" OFF)
option(FCG_CCACHE "Compile through ccache when it is installed" ON)
option(FCG_STUDY_BENCH "Build fcg-study-bench, the study store measured on large files (never in the CI)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ccache, when installed: a file compiled once with the same flags, in this build folder or another, is not compiled
# again. The paths below the sources are made relative to them (CCACHE_BASEDIR), so that the build folders of other
# worktrees of the repository hit the same cache. Not for a build whose compilations must all be seen (CodeQL,
# Coverity): -DFCG_CCACHE=OFF.
if(FCG_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(FCG_CCACHE_PROGRAM ccache)
    if(FCG_CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_BASEDIR=${CMAKE_CURRENT_SOURCE_DIR}
                                      ${FCG_CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_C_COMPILER_LAUNCHER})
        # SDL's precompiled header would make every one of its files uncacheable (gcc): none.
        set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
        message(STATUS "Compiling through ${FCG_CCACHE_PROGRAM}")
    endif()
endif()

if(FCG_STATIC_RUNTIME AND NOT WIN32 AND NOT APPLE)
    add_link_options(-static-libstdc++ -static-libgcc)
    # The release, built on a newer glibc, runs on glibc 2.38 (cmake/glibc_floor.h): the dependencies' sources too.
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/cmake/glibc_floor.h>")
endif()

# Fuzzing: every library instrumented for libFuzzer's coverage and the sanitizers, the fuzz targets (at the end)
# linked with libFuzzer's main.
if(FCG_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "FCG_FUZZ needs clang (libFuzzer)")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Small release binaries: drop unused functions and strip symbols.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-ffunction-sections -fdata-sections)
    if(APPLE)
        # Apple's linker says it its own way: -dead_strip, and -S -x for the debug and local symbols.
        add_link_options(-Wl,-dead_strip -Wl,-S -Wl,-x)
    else()
        add_link_options(-Wl,--gc-sections -s)
    endif()
endif()

# ---------------------------------------------------------------------------
# fastchess core (everything from app/Makefile except main.cpp)
# ---------------------------------------------------------------------------

set(FC_APP ${CMAKE_CURRENT_SOURCE_DIR}/app)

set(FC_CORE_SOURCES
    ${FC_APP}/src/cli/cli.cpp
    ${FC_APP}/src/cli/sanitize.cpp
    ${FC_APP}/src/core/events/events.cpp
    ${FC_APP}/src/core/globals/globals.cpp
    ${FC_APP}/src/core/logger/logger.cpp
    ${FC_APP}/src/core/printing/printing.cpp
    ${FC_APP}/src/core/time/time.cpp
    ${FC_APP}/src/engine/uci_engine.cpp
    ${FC_APP}/src/game/book/epd_reader.cpp
    ${FC_APP}/src/game/book/opening_book.cpp
    ${FC_APP}/src/game/book/pgn_reader.cpp
    ${FC_APP}/src/game/pgn/pgn_builder.cpp
    ${FC_APP}/src/game/pgn/openings_data.cpp
    ${FC_APP}/src/game/timecontrol/timecontrol.cpp
    ${FC_APP}/src/matchmaking/syzygy.cpp
    ${FC_APP}/src/matchmaking/elo/elo_pentanomial.cpp
    ${FC_APP}/src/matchmaking/elo/elo_wdl.cpp
    ${FC_APP}/src/matchmaking/match/match.cpp
    ${FC_APP}/src/matchmaking/sprt/sprt.cpp
    ${FC_APP}/src/matchmaking/tournament/base/tournament.cpp
    ${FC_APP}/src/matchmaking/tournament/roundrobin/roundrobin.cpp
    ${FC_APP}/src/matchmaking/tournament/tournament_manager.cpp
    ${FC_APP}/third_party/pyrrhic/tbprobe.cpp
    ${FC_APP}/third_party/fmt/src/format.cc
)

add_library(fastchess_core STATIC ${FC_CORE_SOURCES})
# Pyrrhic reads the 32-bit words of the DTZ tables where they lie, unaligned: what x86-64 and ARM64 do without a
# fault, and what UBSan's alignment check reports when the Live view or the tests probe them. app/ stays as fastchess
# has it: the check is turned off for that file alone.
if(CMAKE_CXX_FLAGS MATCHES "sanitize=[^ ]*undefined" OR FCG_FUZZ)
    set_source_files_properties(${FC_APP}/third_party/pyrrhic/tbprobe.cpp PROPERTIES COMPILE_OPTIONS
                                                                                     -fno-sanitize=alignment)
endif()
set_target_properties(fastchess_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(fastchess_core PUBLIC
    ${FC_APP}/src
    ${FC_APP}/third_party
    ${FC_APP}/third_party/fmt/include
)
target_compile_options(fastchess_core PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(fastchess_core PUBLIC Threads::Threads)

if(NOT WIN32)
    # Same probes as app/configuration/spawn-check.mk, with -Werror as there: on macOS a function newer than the
    # oldest version supported is only a warning, and the program would not start on that version.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS -Werror)
    check_cxx_source_compiles([[
        #include <spawn.h>
        int main() { posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
                     return posix_spawn_file_actions_addchdir(&fa, "/"); }]] FC_HAVE_ADDCHDIR)
    check_cxx_source_compiles([[
        #include <spawn.h>
        int main() { posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
                     return posix_spawn_file_actions_addchdir_np(&fa, "/"); }]] FC_HAVE_ADDCHDIR_NP)
    unset(CMAKE_REQUIRED_FLAGS)
    if(FC_HAVE_ADDCHDIR)
        target_compile_definitions(fastchess_core PUBLIC HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    elseif(FC_HAVE_ADDCHDIR_NP)
        target_compile_definitions(fastchess_core PUBLIC HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    endif()
endif()

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE FC_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(FC_GIT_SHA)
        target_compile_definitions(fastchess_core PRIVATE GIT_SHA="${FC_GIT_SHA}")
    endif()
endif()

# The upstream command line tool, built from the same core library.
add_executable(fastchess ${FC_APP}/src/main.cpp)
target_link_libraries(fastchess PRIVATE fastchess_core)
set_target_properties(fastchess PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(WIN32)
    target_link_options(fastchess PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# Tournament layer: schedules, journal, results, runner (no UI)
# ---------------------------------------------------------------------------

file(GLOB FCG_TOURNAMENT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tournament/src/tour/*.cpp)
add_library(fcg_tournament STATIC ${FCG_TOURNAMENT_SOURCES})
target_compile_features(fcg_tournament PUBLIC cxx_std_20)
target_include_directories(fcg_tournament PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tournament/src)
target_compile_options(fcg_tournament PRIVATE -Wall -Wextra)
target_link_libraries(fcg_tournament PUBLIC fastchess_core)
# The program's name where the tournament layer writes it (engine logs, a rating list's header, the book adapter's
# messages), set by each repository: tournament/src stays the same in fastchess-gui's.
set(FCG_PRODUCT_NAME "RookStudio")
target_compile_definitions(fcg_tournament PRIVATE FCG_PRODUCT_NAME="${FCG_PRODUCT_NAME}")

if(FCG_BUILD_TESTS)
    # fastchess' own mock engine, so the integration tests play real (random) games.
    add_executable(fcg_random_mover ${CMAKE_CURRENT_SOURCE_DIR}/tournament/tests/mock/random_mover.cpp)
    target_include_directories(fcg_random_mover PRIVATE ${FC_APP}/third_party)
    set_target_properties(fcg_random_mover PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(WIN32)
        target_link_options(fcg_random_mover PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    file(GLOB FCG_TOURNAMENT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tournament/tests/*.cpp)
    add_executable(fcg-tournament-tests ${FCG_TOURNAMENT_TEST_SOURCES})
    target_link_libraries(fcg-tournament-tests PRIVATE fcg_tournament)
    add_dependencies(fcg-tournament-tests fcg_random_mover)
    target_compile_definitions(fcg-tournament-tests PRIVATE FCG_RANDOM_MOVER="$<TARGET_FILE_NAME:fcg_random_mover>"
                                                              FCG_PRODUCT_NAME="${FCG_PRODUCT_NAME}")
    if(WIN32)
        target_link_options(fcg-tournament-tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---------------------------------------------------------------------------
# What the study layer's imports decompress: zstd (decompression only) and zlib (inflate only)
# ---------------------------------------------------------------------------

include(FetchContent)
FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.7
    SOURCE_SUBDIR fcg-no-cmake
    GIT_SHALLOW TRUE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.2
    SOURCE_SUBDIR fcg-no-cmake
    GIT_SHALLOW TRUE)
# The LZMA SDK's decoders and 7z reader (public domain), from 7-Zip's sources: .7z archives, Lumbra's Gigabase.
FetchContent_Declare(lzma
    GIT_REPOSITORY https://github.com/ip7z/7zip.git
    GIT_TAG 24.09
    SOURCE_SUBDIR fcg-no-cmake
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zstd zlib lzma)

# The decoder alone, in C, without its assembly (one source set on every system) or threads.
file(GLOB FCG_ZSTD_SOURCES ${zstd_SOURCE_DIR}/lib/common/*.c ${zstd_SOURCE_DIR}/lib/decompress/*.c)
add_library(fcg_zstd STATIC ${FCG_ZSTD_SOURCES})
target_include_directories(fcg_zstd SYSTEM PUBLIC ${zstd_SOURCE_DIR}/lib)
target_compile_definitions(fcg_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0 XXH_NAMESPACE=FCG_ZSTD_)
add_library(fcg_zlib STATIC
    ${zlib_SOURCE_DIR}/inflate.c ${zlib_SOURCE_DIR}/inftrees.c ${zlib_SOURCE_DIR}/inffast.c
    ${zlib_SOURCE_DIR}/adler32.c ${zlib_SOURCE_DIR}/crc32.c ${zlib_SOURCE_DIR}/zutil.c)
target_include_directories(fcg_zlib SYSTEM PUBLIC ${zlib_SOURCE_DIR})
target_compile_definitions(fcg_zlib PRIVATE Z_PREFIX)
target_compile_definitions(fcg_zlib INTERFACE Z_PREFIX)
add_library(fcg_lzma STATIC
    ${lzma_SOURCE_DIR}/C/7zArcIn.c ${lzma_SOURCE_DIR}/C/7zBuf.c ${lzma_SOURCE_DIR}/C/7zCrc.c
    ${lzma_SOURCE_DIR}/C/7zCrcOpt.c ${lzma_SOURCE_DIR}/C/7zStream.c ${lzma_SOURCE_DIR}/C/CpuArch.c
    ${lzma_SOURCE_DIR}/C/LzmaDec.c ${lzma_SOURCE_DIR}/C/Lzma2Dec.c ${lzma_SOURCE_DIR}/C/7zDec.c
    ${lzma_SOURCE_DIR}/C/Bcj2.c ${lzma_SOURCE_DIR}/C/Bra.c ${lzma_SOURCE_DIR}/C/Bra86.c ${lzma_SOURCE_DIR}/C/BraIA64.c
    ${lzma_SOURCE_DIR}/C/Delta.c)
target_include_directories(fcg_lzma SYSTEM PUBLIC ${lzma_SOURCE_DIR}/C)
# The SDK reads an archive's numbers where they lie, unaligned (7zArcIn.c), as it means to on x86-64 and ARM64: like
# pyrrhic's, its sources are left out of UBSan's alignment check.
if(CMAKE_C_FLAGS MATCHES "sanitize=[^ ]*undefined" OR FCG_FUZZ)
    target_compile_options(fcg_lzma PRIVATE -fno-sanitize=alignment)
endif()

# ---------------------------------------------------------------------------
# Study layer: games and their PGN, databases (no UI)
# ---------------------------------------------------------------------------

file(GLOB FCG_STUDY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/study/src/study/*.cpp)
add_library(fcg_study STATIC ${FCG_STUDY_SOURCES})
target_compile_features(fcg_study PUBLIC cxx_std_20)
target_include_directories(fcg_study PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/study/src)
target_compile_options(fcg_study PRIVATE -Wall -Wextra)
target_link_libraries(fcg_study PUBLIC fcg_tournament PRIVATE fcg_zstd fcg_zlib fcg_lzma)

if(FCG_BUILD_TESTS)
    file(GLOB FCG_STUDY_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/study/tests/*.cpp)
    add_executable(fcg-study-tests ${FCG_STUDY_TEST_SOURCES})
    target_include_directories(fcg-study-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tournament/tests)
    target_link_libraries(fcg-study-tests PRIVATE fcg_study)
    # Its files (compressed games) read from the sources, or from FCG_SOURCE_DIR (the CI's tests on another machine).
    target_compile_definitions(fcg-study-tests PRIVATE FCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(WIN32)
        target_link_options(fcg-study-tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(FCG_STUDY_BENCH)
    add_executable(fcg-study-bench ${CMAKE_CURRENT_SOURCE_DIR}/study/bench/main.cpp)
    target_link_libraries(fcg-study-bench PRIVATE fcg_study)
    target_compile_options(fcg-study-bench PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Commentary: the assistant's facts, comments and review, ported from the Rust prototype (no UI)
# ---------------------------------------------------------------------------

file(GLOB FCG_COMMENTARY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/commentary/src/commentary/*.cpp)
# The opening names (Lichess's chess-openings, CC0), embedded as a byte array.
set(FCG_OPENINGS_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/lichess_openings.cpp)
add_custom_command(
    OUTPUT ${FCG_OPENINGS_CPP}
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/commentary/data/openings.tsv -DOUT=${FCG_OPENINGS_CPP}
            -DSYMBOL=lichess_openings -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/commentary/data/openings.tsv ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding the opening names")
add_library(fcg_commentary STATIC ${FCG_COMMENTARY_SOURCES} ${FCG_OPENINGS_CPP})
target_compile_features(fcg_commentary PUBLIC cxx_std_20)
target_include_directories(fcg_commentary PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/commentary/src)
target_compile_options(fcg_commentary PRIVATE -Wall -Wextra)
target_link_libraries(fcg_commentary PUBLIC fcg_tournament fcg_study)

# ---------------------------------------------------------------------------
# Assistant: the language model runtime (llama.cpp, MIT), run in a child process of the program
# ---------------------------------------------------------------------------

option(FCG_ASSISTANT "Build the assistant's language model runtime (llama.cpp)" ON)
option(FCG_ASSISTANT_VULKAN "The assistant's Vulkan backend (the Vulkan SDK's headers and loader, glslc, SPIRV-Headers)" OFF)
option(FCG_ASSISTANT_EVAL "Build fcg-assistant-eval: a model measured on commentary cases, as the program uses it" OFF)
if(FCG_ASSISTANT)
    # The release the prototype's commentary model was made and measured with (llama-cpp-2 0.1.161 vendors it), as
    # shared libraries whose backends ggml loads at run time: the program links no GPU library, and starts where one is
    # missing. The CPU backend in every variant x86-64 processors need, the best one chosen at run time.
    FetchContent_Declare(llama
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG v0.6.0
        GIT_SHALLOW TRUE)
    set(FCG_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS ON)
    set(GGML_BACKEND_DL ON CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    # ggml's own thread pool: no OpenMP runtime to ship (libgomp on Windows; Apple's clang has none).
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    # The GPU backend: Vulkan alone, on every platform (MoltenVK on macOS), a module loaded where the loader is.
    set(GGML_VULKAN ${FCG_ASSISTANT_VULKAN} CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(GGML_CPU_ALL_VARIANTS ON CACHE BOOL "" FORCE)
    endif()
    foreach(option LLAMA_BUILD_TESTS LLAMA_BUILD_TOOLS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_SERVER LLAMA_BUILD_APP
                   LLAMA_BUILD_COMMON LLAMA_OPENSSL LLAMA_SUBPROCESS)
        set(${option} OFF CACHE BOOL "" FORCE)
    endforeach()
    if(FCG_ASSISTANT_VULKAN AND CMAKE_CROSSCOMPILING AND MINGW)
        # Cross-built for Windows, the Vulkan SDK's parts for the target are made here: its headers and SPIRV-Headers,
        # fetched; an import library for vulkan-1.dll, the loader every GPU driver installs, made from the headers'
        # prototypes (only the functions ggml calls become imports); and the host's glslc.
        FetchContent_Declare(vulkan_headers
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG vulkan-sdk-1.4.363.0
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR fcg-no-cmake)
        FetchContent_Declare(SPIRV-Headers
            GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
            GIT_TAG vulkan-sdk-1.4.363.0
            GIT_SHALLOW TRUE
            OVERRIDE_FIND_PACKAGE)
        FetchContent_MakeAvailable(vulkan_headers SPIRV-Headers)
        file(STRINGS ${vulkan_headers_SOURCE_DIR}/include/vulkan/vulkan_core.h prototypes
             REGEX "VKAPI_CALL vk[A-Za-z0-9_]+\\(")
        set(exports "LIBRARY vulkan-1.dll\nEXPORTS\n")
        foreach(prototype IN LISTS prototypes)
            string(REGEX MATCH "vk[A-Za-z0-9_]+\\(" name "${prototype}")
            string(REPLACE "(" "" name "${name}")
            string(APPEND exports "    ${name}\n")
        endforeach()
        file(WRITE ${CMAKE_BINARY_DIR}/vulkan-1.def "${exports}")
        find_program(FCG_DLLTOOL NAMES x86_64-w64-mingw32-dlltool dlltool REQUIRED)
        execute_process(COMMAND ${FCG_DLLTOOL} -d ${CMAKE_BINARY_DIR}/vulkan-1.def
                                -l ${CMAKE_BINARY_DIR}/libvulkan-1.dll.a -D vulkan-1.dll
                        RESULT_VARIABLE dlltool_result)
        if(NOT dlltool_result EQUAL 0)
            message(FATAL_ERROR "dlltool could not make the import library of vulkan-1.dll")
        endif()
        find_program(FCG_GLSLC glslc NO_CMAKE_FIND_ROOT_PATH REQUIRED)
        set(Vulkan_INCLUDE_DIR ${vulkan_headers_SOURCE_DIR}/include CACHE PATH "" FORCE)
        set(Vulkan_LIBRARY ${CMAKE_BINARY_DIR}/libvulkan-1.dll.a CACHE FILEPATH "" FORCE)
        set(Vulkan_GLSLC_EXECUTABLE ${FCG_GLSLC} CACHE FILEPATH "" FORCE)
    endif()
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    FetchContent_MakeAvailable(llama)
    set(BUILD_SHARED_LIBS ${FCG_BUILD_SHARED_LIBS})
    # Its libraries beside the programs (llama.cpp puts them in bin/): where ggml looks for its backends, and where
    # Windows finds a program's DLLs; elsewhere the programs' run path ($ORIGIN) does.
    function(fcg_beside_programs dir)
        get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(type ${target} TYPE)
            if(type MATCHES "SHARED_LIBRARY|MODULE_LIBRARY")
                set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
                                                           LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
                # One file each, shipped beside the program: no versioned names and links to them.
                set_property(TARGET ${target} PROPERTY VERSION)
                set_property(TARGET ${target} PROPERTY SOVERSION)
                # Only their C interfaces cross between them and the programs: each has its runtime of its own, as
                # the programs have, and needs no MinGW DLL beside it.
                if(WIN32)
                    # The static runtime's symbols kept inside each DLL: exported by one, they clash in the next.
                    target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++
                                                          -Wl,--exclude-libs,ALL)
                    # ggml looks for its backends by the names llama.cpp's own Windows builds give them
                    # (ggml-cpu-haswell.dll), without MinGW's "lib".
                    set_target_properties(${target} PROPERTIES PREFIX "")
                endif()
            endif()
        endforeach()
        get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
        foreach(sub IN LISTS subdirs)
            fcg_beside_programs(${sub})
        endforeach()
    endfunction()
    fcg_beside_programs(${llama_SOURCE_DIR})
    if(FCG_ASSISTANT_VULKAN AND CMAKE_CROSSCOMPILING AND MINGW)
        # ggml finds SPIRV-Headers but takes its headers from the SDK's include folder: given to it here.
        target_include_directories(ggml-vulkan PRIVATE ${spirv-headers_SOURCE_DIR}/include)
    endif()
    if(MINGW)
        # Older MinGW-w64 headers lack the thread power throttling state ggml's CPU backend sets: it is given to it.
        include(CheckCSourceCompiles)
        check_c_source_compiles("#include <windows.h>
            int main(void) { THREAD_POWER_THROTTLING_STATE t; t.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
                             return (int) t.Version; }" FCG_HAVE_THREAD_POWER_THROTTLING)
        if(NOT FCG_HAVE_THREAD_POWER_THROTTLING)
            get_property(ggml_targets DIRECTORY ${llama_SOURCE_DIR}/ggml/src PROPERTY BUILDSYSTEM_TARGETS)
            foreach(target IN LISTS ggml_targets)
                if(target MATCHES "^ggml-cpu")
                    target_compile_options(${target} PRIVATE
                        -include ${CMAKE_CURRENT_SOURCE_DIR}/assistant/compat/thread_power_throttling.h)
                endif()
            endforeach()
        endif()
        # MinGW-w64 13's headers check printf's formats against msvcrt's, which knows no %zu, though the calls go
        # to MinGW's own printf, which does: the warnings of ggml's messages are left out (Mbed TLS's too, below).
        target_compile_options(ggml-base PRIVATE -Wno-format)
    endif()

    # llama.cpp's heaviest sources (its model and vocabulary, ggml's Vulkan backend) take gigabytes each to compile
    # with gcc 15: no more of them at once than the memory holds. A private repository's CI runner, two cores and 7 GB,
    # ran out of it on four, and its runner service was stopped with the build.
    if(CMAKE_GENERATOR MATCHES "Ninja")
        cmake_host_system_information(RESULT FCG_MEMORY_MB QUERY TOTAL_PHYSICAL_MEMORY)
        math(EXPR FCG_HEAVY_JOBS "${FCG_MEMORY_MB} / 4000")
        if(FCG_HEAVY_JOBS LESS 1)
            set(FCG_HEAVY_JOBS 1)
        endif()
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS fcg_heavy=${FCG_HEAVY_JOBS})
        foreach(heavy IN ITEMS llama ggml-vulkan)
            if(TARGET ${heavy})
                set_property(TARGET ${heavy} PROPERTY JOB_POOL_COMPILE fcg_heavy)
            endif()
        endforeach()
        message(STATUS "llama.cpp's heaviest sources: ${FCG_HEAVY_JOBS} at once (${FCG_MEMORY_MB} MB of memory)")
    endif()

    # llama.cpp's Jinja engine, which renders a model's chat template as the fine-tuning library did, compiled here
    # with the JSON value it reads its context from: its common library would bring an HTTP client and more.
    file(GLOB FCG_JINJA_SOURCES ${llama_SOURCE_DIR}/common/jinja/*.cpp)
    add_library(fcg_jinja STATIC ${FCG_JINJA_SOURCES} ${llama_SOURCE_DIR}/common/json.cpp
                                 ${llama_SOURCE_DIR}/common/unicode.cpp)
    target_compile_features(fcg_jinja PUBLIC cxx_std_17)
    # SYSTEM: what gcc finds in its headers (static functions a source including them leaves unused) is llama.cpp's.
    target_include_directories(fcg_jinja SYSTEM PUBLIC ${llama_SOURCE_DIR}/common
                                                PRIVATE ${llama_SOURCE_DIR}/vendor)
    target_link_libraries(fcg_jinja PUBLIC ggml)
    set_target_properties(fcg_jinja PROPERTIES POSITION_INDEPENDENT_CODE ON)

    file(GLOB FCG_ASSISTANT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assistant/src/assistant/*.cpp)
    add_library(fcg_assistant STATIC ${FCG_ASSISTANT_SOURCES})
    target_compile_features(fcg_assistant PUBLIC cxx_std_20)
    target_include_directories(fcg_assistant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/assistant/src)
    target_compile_options(fcg_assistant PRIVATE -Wall -Wextra)
    target_link_libraries(fcg_assistant PUBLIC fcg_commentary PRIVATE llama fcg_jinja)
    target_compile_definitions(fcg_assistant PUBLIC FCG_ASSISTANT=1)
    if(FCG_ASSISTANT_VULKAN)
        # The device is chosen before ggml starts, through the Vulkan loader: its headers.
        find_package(Vulkan REQUIRED)
        target_link_libraries(fcg_assistant PRIVATE Vulkan::Headers)
        target_compile_definitions(fcg_assistant PRIVATE FCG_ASSISTANT_VULKAN=1)
    endif()

    if(FCG_ASSISTANT_EVAL)
        add_executable(fcg-assistant-eval ${CMAKE_CURRENT_SOURCE_DIR}/assistant/tools/eval.cpp)
        target_link_libraries(fcg-assistant-eval PRIVATE fcg_assistant fcg_study)
        target_compile_options(fcg-assistant-eval PRIVATE -Wall -Wextra)
        if(WIN32)
            target_link_options(fcg-assistant-eval PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
endif()

if(FCG_BUILD_TESTS)
    file(GLOB FCG_COMMENTARY_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/commentary/tests/*.cpp)
    add_executable(fcg-commentary-tests ${FCG_COMMENTARY_TEST_SOURCES})
    target_include_directories(fcg-commentary-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tournament/tests)
    # fcg_study reads the compressed reference files.
    target_link_libraries(fcg-commentary-tests PRIVATE fcg_commentary fcg_study)
    target_compile_definitions(fcg-commentary-tests PRIVATE FCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(WIN32)
        target_link_options(fcg-commentary-tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

if(FCG_BUILD_GUI)
    include(FetchContent)

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    # Only windows, OpenGL, events, dialogs, the 2D renderer and the audio are used. The renderer draws the window where
    # OpenGL 3.2 is missing (virtual machines, remote desktops): its software driver, its OpenGL 2.1 and ES 2 ones, and
    # Direct3D 11 on Windows, the one a remote desktop session has. The audio plays the move sounds, started only when
    # they are on; its Linux backends (PipeWire, PulseAudio, ALSA) are loaded when used, found when their headers are.
    foreach(sdl_off SDL_GPU SDL_CAMERA SDL_HAPTIC SDL_SENSOR SDL_POWER SDL_VULKAN SDL_HIDAPI SDL_RENDER_D3D
                    SDL_RENDER_D3D12 SDL_RENDER_METAL SDL_DISKAUDIO SDL_DUMMYAUDIO)
        set(${sdl_off} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SDL_RENDER ON CACHE BOOL "" FORCE)
    set(SDL_AUDIO ON CACHE BOOL "" FORCE)

    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9-docking
        SOURCE_SUBDIR fcg-no-cmake
        GIT_SHALLOW TRUE)
    FetchContent_Declare(implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG v1.0
        SOURCE_SUBDIR fcg-no-cmake
        GIT_SHALLOW TRUE)
    FetchContent_Declare(nanosvg
        GIT_REPOSITORY https://github.com/memononen/nanosvg.git
        GIT_TAG 239e102ec2c691f2902e20ace2ed36ee4a35cfe6
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
        SOURCE_SUBDIR fcg-no-cmake)

    # Fonts: Inter for the text (SIL OFL 1.1), Lucide for the icons (ISC).
    FetchContent_Declare(inter
        URL https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
        URL_HASH SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_Declare(lucide
        URL https://registry.npmjs.org/lucide-static/-/lucide-static-1.59.0.tgz
        URL_HASH SHA256=39e2f835692c193233ac7bcdf34eb0cc2da0f839fd9f3674c34d2751bccf37f0
        SOURCE_SUBDIR fcg-no-cmake)

    # The guide's Markdown, parsed in the guide panel: md4c (MIT), its parser compiled in.
    FetchContent_Declare(md4c
        URL https://github.com/mity/md4c/archive/refs/tags/release-0.5.2.tar.gz
        URL_HASH SHA256=55d0111d48fb11883aaee91465e642b8b640775a4d6993c2d0e7a8092758ef21
        SOURCE_SUBDIR fcg-no-cmake)

    # SOURCE_SUBDIR points nowhere for the header/source-only dependencies,
    # so MakeAvailable only downloads them.
    FetchContent_MakeAvailable(sdl3 imgui implot nanosvg stb inter lucide md4c)
    # SDL's sources are not ours: the warnings a newer compiler finds in them (clang's unused-but-set-global) are left
    # out of the build's output.
    if(NOT MSVC)
        foreach(sdl_target SDL3-static SDL_uclibc)
            if(TARGET ${sdl_target})
                target_compile_options(${sdl_target} PRIVATE -w)
            endif()
        endforeach()
    endif()

    # The patches below are run by FetchContent once after the download, and again on the same sources whenever their
    # command changes, never otherwise: the command has the script's hash, so that a script changed is run again, and
    # the configuration runs again when one changes. Each script can run on sources it patched already.
    foreach(patch mbedtls libssh2)
        set(script ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_${patch}.cmake)
        file(SHA256 ${script} hash)
        set(FCG_PATCH_${patch} ${CMAKE_COMMAND} -DFCG_PATCH_SHA256=${hash} -P ${script})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${script})
    endforeach()

    # TLS for the lichess broadcasts: Mbed TLS (long term support branch), static, its libraries only.
    FetchContent_Declare(mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
        URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
        PATCH_COMMAND ${FCG_PATCH_mbedtls})
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
    # Thread-safe: the lichess relay and the SSH tunnel use it at the same time.
    set(TF_PSA_CRYPTO_USER_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tf_psa_crypto_user_config.h
        CACHE FILEPATH "" FORCE)
    set(LINK_WITH_PTHREAD ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mbedtls)
    if(MINGW)
        # Its %zu, which MinGW's printf knows though MinGW-w64 13's headers check it against msvcrt's (see ggml's).
        target_compile_options(mbedtls PRIVATE -Wno-format)
    endif()

    # SSH for the web page's tunnel: libssh2 (BSD-3-Clause), compiled here with a cryptography backend of our own on
    # Mbed TLS 4's PSA API (gui/libssh2-psa): its released versions do not build with Mbed TLS 4. A commit of its
    # master branch, the latest when this was written. Patched: a channel's data packets are appended to the one it
    # holds for the channel, so that what it keeps is about the bytes received, not a block of memory for each packet
    # however small; a forwarded connection's window, which bounds what each viewer makes it hold, is 128 KiB instead
    # of 2 MiB; a session whose connection is over can be freed whole, though a packet was left half sent (see
    # cmake/patch_libssh2.cmake). The tunnel does not compile with libssh2's sources not patched so
    # (FETCHCONTENT_SOURCE_DIR_LIBSSH2 included).
    FetchContent_Declare(libssh2
        URL https://github.com/libssh2/libssh2/archive/e75fa9a539d21e82d4920c126662e36984e97d61.tar.gz
        URL_HASH SHA256=3d4fd0016ec56945f911fd223a43d048622f80c4510cdf3e73de6e47a31c3b1b
        PATCH_COMMAND ${FCG_PATCH_libssh2}
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_MakeAvailable(libssh2)

    # QR codes of the web page's public address: Nayuki's QR Code generator (MIT), its C++ version compiled in.
    FetchContent_Declare(qrcodegen
        URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
        URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_MakeAvailable(qrcodegen)

    # The translations: .po files read by tinygettext, UTF-8 only (no iconv).
    FetchContent_Declare(tinygettext
        GIT_REPOSITORY https://github.com/tinygettext/tinygettext.git
        GIT_TAG 0ebef71ef7aa07ec6d026164fbe9472e6f82a583)
    set(TINYGETTEXT_UTF8_ONLY ON CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tinygettext)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
    )
    set_target_properties(imgui_lib PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    # ImPlot instantiates every plot function for every numeric type unless told otherwise.
    target_compile_definitions(imgui_lib PUBLIC "IMPLOT_CUSTOM_NUMERIC_TYPES=(float)(double)" IMGUI_DISABLE_DEMO_WINDOWS)
    target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static)
    if(WIN32)
        target_link_libraries(imgui_lib PUBLIC opengl32)
    endif()

    # The 3D board: gui/src/board3d, a path tracer in OpenGL 3.3, and its pieces baked by tools/board3d/bake.cpp. Its
    # viewer (tools/board3d/viewer.cpp) reads the shaders from the sources, for working on them.
    file(GLOB FCG_BOARD3D_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/board3d/*.cpp)
    option(FCG_BOARD3D_VIEWER "Build fcg-board3d, the 3D board's viewer" OFF)
    if(FCG_BOARD3D_VIEWER)
        add_executable(fcg-board3d ${CMAKE_CURRENT_SOURCE_DIR}/tools/board3d/viewer.cpp ${FCG_BOARD3D_SOURCES})
        target_compile_features(fcg-board3d PRIVATE cxx_std_20)
        target_include_directories(fcg-board3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/src)
        target_include_directories(fcg-board3d SYSTEM PRIVATE ${stb_SOURCE_DIR})
        target_link_libraries(fcg-board3d PRIVATE imgui_lib)
        target_compile_definitions(fcg-board3d PRIVATE FCG_BOARD3D_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_options(fcg-board3d PRIVATE -Wall -Wextra)
    endif()

    add_library(fcg_md4c STATIC ${md4c_SOURCE_DIR}/src/md4c.c)
    target_include_directories(fcg_md4c SYSTEM PUBLIC ${md4c_SOURCE_DIR}/src)

    # libssh2's sources, but its own cryptography backends: ours instead of its mbedtls.c (see libssh2_config.h).
    # The other backends' files compile to nothing without their define.
    set(FCG_LIBSSH2_SOURCES)
    foreach(file agent bcrypt_pbkdf chacha channel cipher-chachapoly comp crypt global hostkey keepalive kex knownhost
                 mac misc packet pem poly1305 publickey scp session sftp transport userauth userauth_kbd_packet version)
        list(APPEND FCG_LIBSSH2_SOURCES ${libssh2_SOURCE_DIR}/src/${file}.c)
    endforeach()
    # Ed25519 signatures for the user's keys (the remote machines), which PSA does not have: gui/libssh2-psa/ed25519.c.
    add_library(fcg_libssh2 STATIC ${FCG_LIBSSH2_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa/mbedtls_psa.c
                ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa/ed25519.c)
    target_include_directories(fcg_libssh2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa ${libssh2_SOURCE_DIR}/src)
    target_include_directories(fcg_libssh2 SYSTEM PUBLIC ${libssh2_SOURCE_DIR}/include)
    # LIBSSH2DEBUG for libssh2_trace(): the tunnel reads libssh2's errors as they come, the first one often the only
    # one to tell why a call failed (a host key's signature, say). Nothing is traced but the errors.
    target_compile_definitions(fcg_libssh2 PRIVATE HAVE_CONFIG_H LIBSSH2_MBEDTLS LIBSSH2_MBEDTLS_H LIBSSH2DEBUG)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa/mbedtls_psa.c
                                ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa/ed25519.c PROPERTIES
                                COMPILE_OPTIONS "-Wall;-Wextra")
    target_link_libraries(fcg_libssh2 PRIVATE tfpsacrypto)
    if(WIN32)
        target_link_libraries(fcg_libssh2 PUBLIC ws2_32)
    endif()

    # Piece artwork, embedded as string literals: the piece sets of gui/assets/pieces (their authors and licenses in
    # gui/assets/ATTRIBUTION.md), the default one first.
    set(FCG_PIECE_SETS cburnett chessnut celtic fantasy)
    set(FCG_ASSET_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/piece_svgs.cpp)
    set(FCG_PIECE_SVGS)
    foreach(set ${FCG_PIECE_SETS})
        file(GLOB svgs ${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/pieces/${set}/*.svg)
        list(APPEND FCG_PIECE_SVGS ${svgs})
    endforeach()
    string(REPLACE ";" "|" FCG_PIECE_SET_LIST "${FCG_PIECE_SETS}")
    add_custom_command(
        OUTPUT ${FCG_ASSET_CPP}
        COMMAND ${CMAKE_COMMAND} -DOUT=${FCG_ASSET_CPP}
                -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/pieces
                "-DSETS=${FCG_PIECE_SET_LIST}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_pieces.cmake
        DEPENDS ${FCG_PIECE_SVGS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_pieces.cmake
        COMMENT "Embedding piece SVGs"
        VERBATIM)

    # The fonts, embedded as byte arrays.
    set(FCG_FONT_CPPS)
    foreach(font IN ITEMS "inter_regular|${inter_SOURCE_DIR}/extras/ttf/Inter-Regular.ttf"
                          "inter_semibold|${inter_SOURCE_DIR}/extras/ttf/Inter-SemiBold.ttf"
                          "inter_italic|${inter_SOURCE_DIR}/extras/ttf/Inter-Italic.ttf"
                          "lucide_icons|${lucide_SOURCE_DIR}/font/lucide.ttf")
        string(REPLACE "|" ";" font "${font}")
        list(GET font 0 symbol)
        list(GET font 1 ttf)
        set(cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/${symbol}.cpp)
        add_custom_command(
            OUTPUT ${cpp}
            COMMAND ${CMAKE_COMMAND} -DIN=${ttf} -DOUT=${cpp} -DSYMBOL=${symbol}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            DEPENDS ${ttf} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            COMMENT "Embedding ${symbol}")
        list(APPEND FCG_FONT_CPPS ${cpp})
    endforeach()

    # The 3D board's shaders and pieces, embedded as byte arrays.
    set(FCG_BOARD3D_CPPS)
    foreach(asset IN ITEMS "board3d_trace|gui/src/board3d/shaders/trace.frag"
                           "board3d_prepare|gui/src/board3d/shaders/prepare.frag"
                           "board3d_atrous|gui/src/board3d/shaders/atrous.frag"
                           "board3d_display|gui/src/board3d/shaders/display.frag"
                           "board3d_pieces|gui/assets/board3d/pieces.bin")
        string(REPLACE "|" ";" asset "${asset}")
        list(GET asset 0 symbol)
        list(GET asset 1 file)
        set(file ${CMAKE_CURRENT_SOURCE_DIR}/${file})
        set(cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/${symbol}.cpp)
        add_custom_command(
            OUTPUT ${cpp}
            COMMAND ${CMAKE_COMMAND} -DIN=${file} -DOUT=${cpp} -DSYMBOL=${symbol}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            DEPENDS ${file} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            COMMENT "Embedding ${symbol}")
        list(APPEND FCG_BOARD3D_CPPS ${cpp})
    endforeach()

    # The translations, embedded with their language codes (gui/i18n/<code>.po).
    file(GLOB FCG_PO_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/i18n/*.po)
    set(FCG_PO_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/translations.cpp)
    string(REPLACE ";" "|" FCG_PO_LIST "${FCG_PO_FILES}")
    add_custom_command(
        OUTPUT ${FCG_PO_CPP}
        COMMAND ${CMAKE_COMMAND} -DOUT=${FCG_PO_CPP} "-DFILES=${FCG_PO_LIST}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_po.cmake
        DEPENDS ${FCG_PO_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_po.cmake
        COMMENT "Embedding translations"
        VERBATIM)

    # The user guide, its translations and its pictures (docs/guide.md, docs/guide.<code>.md), embedded under their
    # names: the guide panel shows them, for the version that runs, without a network.
    file(GLOB FCG_GUIDES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/docs/guide*.md)
    set(FCG_GUIDE_PICTURES screenshots/appearance.png)
    set(FCG_DOC_LIST "")
    set(FCG_DOC_DEPENDS "")
    foreach(guide IN LISTS FCG_GUIDES)
        get_filename_component(name ${guide} NAME)
        list(APPEND FCG_DOC_LIST "${name}=${guide}")
        list(APPEND FCG_DOC_DEPENDS ${guide})
    endforeach()
    foreach(picture IN LISTS FCG_GUIDE_PICTURES)
        list(APPEND FCG_DOC_LIST "${picture}=${CMAKE_CURRENT_SOURCE_DIR}/docs/${picture}")
        list(APPEND FCG_DOC_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/docs/${picture})
    endforeach()
    string(REPLACE ";" "|" FCG_DOC_LIST "${FCG_DOC_LIST}")
    set(FCG_DOC_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/docs.cpp)
    add_custom_command(
        OUTPUT ${FCG_DOC_CPP}
        COMMAND ${CMAKE_COMMAND} -DOUT=${FCG_DOC_CPP} "-DFILES=${FCG_DOC_LIST}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_docs.cmake
        DEPENDS ${FCG_DOC_DEPENDS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_docs.cmake
        COMMENT "Embedding the guide"
        VERBATIM)

    # The engines the engine picker offers (gui/assets/engines.json), embedded: the copy the program reads when it
    # has not read a newer one.
    set(FCG_ENGINES_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/engine_catalogue.cpp)
    add_custom_command(
        OUTPUT ${FCG_ENGINES_CPP}
        COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/engines.json -DOUT=${FCG_ENGINES_CPP}
                -DSYMBOL=engine_catalogue -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/engines.json ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
        COMMENT "Embedding the engine catalogue"
        VERBATIM)

    file(GLOB FCG_GUI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/*.cpp)
    list(REMOVE_ITEM FCG_GUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/main.cpp)

    # Everything except main(), so the unit tests can link it too.
    add_library(fcg_lib STATIC ${FCG_GUI_SOURCES} ${FCG_BOARD3D_SOURCES} ${FCG_ASSET_CPP} ${FCG_FONT_CPPS}
                ${FCG_BOARD3D_CPPS} ${FCG_PO_CPP} ${FCG_DOC_CPP} ${FCG_ENGINES_CPP}
                ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
    target_compile_features(fcg_lib PUBLIC cxx_std_20)
    target_include_directories(fcg_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gui/src)
    # Header-only libraries compiled in our files: their warnings are not ours to fix.
    target_include_directories(fcg_lib SYSTEM PUBLIC
        ${nanosvg_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${qrcodegen_SOURCE_DIR}/cpp
    )
    target_compile_options(fcg_lib PRIVATE -Wall -Wextra)
    target_compile_definitions(fcg_lib PUBLIC FCG_VERSION="${PROJECT_VERSION}${FCG_VERSION_SUFFIX}")
    # The commentary library too, without the assistant's runtime: the Board page's reference games read it.
    target_link_libraries(fcg_lib PUBLIC fcg_study fcg_commentary fcg_tournament imgui_lib PRIVATE mbedtls fcg_libssh2
                                         tinygettext fcg_md4c)
    if(FCG_ASSISTANT)
        # The assistant's child process is this program too (--assistant), and the window talks with it.
        target_link_libraries(fcg_lib PUBLIC fcg_assistant)
    endif()
    if(WIN32)
        target_link_libraries(fcg_lib PUBLIC ws2_32)
    endif()

    set(FCG_PLATFORM_SOURCES "")
    if(WIN32)
        enable_language(RC)
        set(FCG_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/win/fastchess-gui.rc)
    elseif(APPLE)
        # The application's icon (written by the test "logo: the macOS icon is the logo drawn"), in the bundle.
        set(FCG_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/mac/fastchess-gui.icns)
        set_source_files_properties(${FCG_PLATFORM_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()

    # The program, rookstudio (RookStudio.app on macOS); its target keeps fastchess-gui's name, for its releases to
    # merge.
    add_executable(fastchess-gui WIN32 MACOSX_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/main.cpp
                   ${FCG_PLATFORM_SOURCES})
    if(APPLE)
        set_target_properties(fastchess-gui PROPERTIES OUTPUT_NAME RookStudio)
    else()
        set_target_properties(fastchess-gui PROPERTIES OUTPUT_NAME rookstudio)
    endif()
    target_link_libraries(fastchess-gui PRIVATE fcg_lib)
    if(WIN32)
        target_link_options(fastchess-gui PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    if(APPLE)
        set_target_properties(fastchess-gui PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/gui/mac/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME RookStudio
            MACOSX_BUNDLE_GUI_IDENTIFIER io.github.jromang.rookstudio
            MACOSX_BUNDLE_ICON_FILE fastchess-gui.icns
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    endif()

    if(FCG_BUILD_TESTS)
        file(GLOB FCG_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/tests/*.cpp)
        add_executable(fastchess-gui-tests ${FCG_TEST_SOURCES})
        # Mbed TLS too: the SSH tests play the server's part with its cryptography. And the libssh2 backend's
        # functions, which they call directly.
        target_link_libraries(fastchess-gui-tests PRIVATE fcg_lib mbedtls fcg_libssh2)
        target_include_directories(fastchess-gui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa)
        target_include_directories(fastchess-gui-tests SYSTEM PRIVATE ${libssh2_SOURCE_DIR}/src)
        # A WinBoard engine for the tests of the adapter.
        add_executable(fcg_wb_mover ${CMAKE_CURRENT_SOURCE_DIR}/gui/tests/mock/wb_mover.cpp)
        set_target_properties(fcg_wb_mover PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        if(WIN32)
            target_link_options(fcg_wb_mover PRIVATE -static -static-libgcc -static-libstdc++)
        endif()

        if(FCG_ASSISTANT)
            # A language model that answers at once, for the tests of the assistant's process.
            add_executable(fcg_mock_assistant ${CMAKE_CURRENT_SOURCE_DIR}/gui/tests/mock/assistant.cpp)
            target_link_libraries(fcg_mock_assistant PRIVATE fcg_assistant)
            if(WIN32)
                target_link_options(fcg_mock_assistant PRIVATE -static -static-libgcc -static-libstdc++)
            endif()
            add_dependencies(fastchess-gui-tests fcg_mock_assistant)
            target_compile_definitions(fastchess-gui-tests
                                       PRIVATE FCG_MOCK_ASSISTANT="$<TARGET_FILE_NAME:fcg_mock_assistant>")
        endif()
        # The program itself too: the tests start tournament processes (--run ... --control stdio).
        add_dependencies(fastchess-gui-tests fcg_random_mover fcg_wb_mover fastchess-gui)
        target_compile_definitions(fastchess-gui-tests PRIVATE FCG_RANDOM_MOVER="$<TARGET_FILE_NAME:fcg_random_mover>"
                                                              FCG_WB_MOVER="$<TARGET_FILE_NAME:fcg_wb_mover>"
                                                              FCG_GUI_PROGRAM="$<TARGET_FILE:fastchess-gui>"
                                                              FCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        if(WIN32)
            target_link_options(fastchess-gui-tests PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()

    if(FCG_FUZZ)
        # What others send the program: a web request, the tunnel server's text, a TLCS datagram, a PGN book (to
        # Polyglot), an EPD book or suite; and what fastchess' own readers take of them through the program: a PGN
        # book, an engine's info line; and a game's PGN as the study layer reads and writes it. Each target fuzz_<name>
        # from fuzz/<name>.cpp, its seeds in fuzz/corpus/<name>.
        foreach(name web_request tunnel_text tlcs_message pgn_book epd_book app_pgn engine_info pgn_reader)
            add_executable(fuzz_${name} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/${name}.cpp
                                        ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/version.cpp)
            target_link_libraries(fuzz_${name} PRIVATE fcg_lib)
            target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        endforeach()
    endif()
endif()

# cmake --build build --target check: the test programs built, then their test cases run side by side, each in a
# process of its own (tools/run_tests.py; its options there for a part of them only).
if(FCG_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(FCG_TEST_PROGRAMS fcg-tournament-tests fcg-study-tests fcg-commentary-tests)
        if(FCG_BUILD_GUI)
            list(PREPEND FCG_TEST_PROGRAMS fastchess-gui-tests)
        endif()
        set(FCG_TEST_FILES)
        foreach(program IN LISTS FCG_TEST_PROGRAMS)
            list(APPEND FCG_TEST_FILES $<TARGET_FILE:${program}>)
        endforeach()
        add_custom_target(check
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_tests.py ${FCG_TEST_FILES}
            DEPENDS ${FCG_TEST_PROGRAMS}
            USES_TERMINAL
            COMMENT "Running the tests")
    endif()
endif()
