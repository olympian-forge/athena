#   Copyright (c) 2026 Ike
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

# 3.19 for string(JSON ...) -- used below to resolve the latest ONNX
# Runtime release from GitHub's API instead of pinning a version.
cmake_minimum_required(VERSION 3.19)

# Must be set before project() to take effect (CMP0091 controls whether
# CMAKE_MSVC_RUNTIME_LIBRARY is honored at all).
cmake_policy(SET CMP0091 NEW)

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ATHENA_VERSION_RAW)
string(STRIP "${ATHENA_VERSION_RAW}" ATHENA_VERSION)

project(athena VERSION ${ATHENA_VERSION} LANGUAGES CXX)

# Athena requires C++20 <format>, which is unavailable (or badly broken)
# before these compiler versions. Fail configure immediately with a clear
# message instead of a cryptic "<format>: No such file or directory" deep
# in a compile log -- found the hard way when a friend's older g++ choked
# on this mid-demo.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR
            "Athena requires GCC 13 or newer for C++20 <format> support "
            "(found GCC ${CMAKE_CXX_COMPILER_VERSION}). Please upgrade g++.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "17.0")
        message(FATAL_ERROR
            "Athena requires Clang 17 or newer for C++20 <format> support "
            "(found Clang ${CMAKE_CXX_COMPILER_VERSION}). Please upgrade clang++. "
            "(AppleClang uses a different version scheme and is not checked here.)")
    endif()
elseif(MSVC)
    if(MSVC_VERSION LESS 1929)
        message(FATAL_ERROR
            "Athena requires MSVC 19.29 or newer (Visual Studio 2019 16.11+, "
            "or VS 2022) for C++20 <format> support (found MSVC ${MSVC_VERSION}). "
            "Please upgrade Visual Studio.")
    endif()
endif()

# Statically link the MSVC C++ runtime so a built athena.exe doesn't need
# the Visual C++ Redistributable installed on whatever machine it's copied
# to (see docs/build-system-improvements.md) -- Arena/other GUI machines
# can't be assumed to have any dev tooling at all.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Default to Debug build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build" FORCE)
endif()

message(STATUS "Building in ${CMAKE_BUILD_TYPE} mode")

option(ENABLE_AUTOTUNE "Enable the AutoTuner feature and standalone test" ON)

# Global C++20 standard configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ONNX Runtime Integration
include(FetchContent)

if(WIN32)
    # DirectML is vendor-agnostic (NVIDIA or AMD), so no GPU detection is
    # needed on Windows -- only "which is the latest package version".
    # NuGet v3 flat-container index lists all published versions ascending;
    # the last one is latest.
    set(ONNXRUNTIME_NUGET_INDEX "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/index.json")
    file(DOWNLOAD "${ONNXRUNTIME_NUGET_INDEX}" "${CMAKE_BINARY_DIR}/onnxruntime_nuget_versions.json"
         STATUS ONNXRUNTIME_NUGET_STATUS)
    list(GET ONNXRUNTIME_NUGET_STATUS 0 ONNXRUNTIME_NUGET_STATUS_CODE)
    if(NOT ONNXRUNTIME_NUGET_STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to query latest ONNX Runtime DirectML version from NuGet: ${ONNXRUNTIME_NUGET_STATUS}")
    endif()

    file(READ "${CMAKE_BINARY_DIR}/onnxruntime_nuget_versions.json" ONNXRUNTIME_NUGET_JSON)
    string(JSON ONNXRUNTIME_NUGET_VERSION_COUNT LENGTH "${ONNXRUNTIME_NUGET_JSON}" versions)
    math(EXPR ONNXRUNTIME_NUGET_VERSION_LAST "${ONNXRUNTIME_NUGET_VERSION_COUNT} - 1")
    string(JSON ONNXRUNTIME_VERSION GET "${ONNXRUNTIME_NUGET_JSON}" versions ${ONNXRUNTIME_NUGET_VERSION_LAST})

    message(STATUS "ONNX Runtime DirectML: latest NuGet version is ${ONNXRUNTIME_VERSION}")
    set(ONNXRUNTIME_URL "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${ONNXRUNTIME_VERSION}")
    set(ONNXRUNTIME_DOWNLOAD_NAME onnxruntime_dml.zip)
else()
    # --- Platform / GPU vendor detection (Linux) ---
    set(ATHENA_IS_WSL FALSE)
    if(EXISTS "/proc/version")
        file(READ "/proc/version" ATHENA_PROC_VERSION)
        string(TOLOWER "${ATHENA_PROC_VERSION}" ATHENA_PROC_VERSION_LOWER)
        if(ATHENA_PROC_VERSION_LOWER MATCHES "microsoft" OR ATHENA_PROC_VERSION_LOWER MATCHES "wsl")
            set(ATHENA_IS_WSL TRUE)
        endif()
    endif()

    # lspci over nvidia-smi/rocm-smi: detects hardware presence without
    # requiring the vendor's own driver/tooling to already be installed.
    set(ATHENA_GPU_VENDOR "UNKNOWN")
    find_program(ATHENA_LSPCI_EXECUTABLE lspci)
    if(ATHENA_LSPCI_EXECUTABLE)
        execute_process(
            COMMAND ${ATHENA_LSPCI_EXECUTABLE}
            OUTPUT_VARIABLE ATHENA_LSPCI_OUTPUT
            ERROR_QUIET
        )
        string(TOLOWER "${ATHENA_LSPCI_OUTPUT}" ATHENA_LSPCI_OUTPUT_LOWER)
        set(ATHENA_GPU_VENDOR "NONE")
        if(ATHENA_LSPCI_OUTPUT_LOWER MATCHES "nvidia")
            # NVIDIA takes priority if a machine has both -- enabling GPU
            # support beats blocking on a secondary AMD device.
            set(ATHENA_GPU_VENDOR "NVIDIA")
        elseif(ATHENA_LSPCI_OUTPUT_LOWER MATCHES "amd" OR ATHENA_LSPCI_OUTPUT_LOWER MATCHES "\\[amd/ati\\]" OR ATHENA_LSPCI_OUTPUT_LOWER MATCHES "advanced micro devices")
            set(ATHENA_GPU_VENDOR "AMD")
        endif()
    else()
        message(WARNING "lspci not found -- cannot detect GPU vendor. Assuming NVIDIA (today's default) rather than disabling GPU support; install pciutils for real detection.")
        set(ATHENA_GPU_VENDOR "NVIDIA")
    endif()

    if(ATHENA_GPU_VENDOR STREQUAL "AMD" AND ATHENA_IS_WSL)
        message(FATAL_ERROR
            "AMD GPU detected under WSL. AMD GPU compute passthrough is not "
            "meaningfully supported under WSL2 for this build path. Options: "
            "run natively on Linux (not under WSL), use an NVIDIA GPU, or "
            "build CPU-only by working around this check.")
    elseif(ATHENA_GPU_VENDOR STREQUAL "AMD")
        message(FATAL_ERROR
            "AMD GPU detected on native Linux. Microsoft does not publish a "
            "prebuilt ROCm-enabled ONNX Runtime binary for any version -- a "
            "ROCm driver/SDK being installed locally doesn't change this, "
            "since the ONNX Runtime library itself must be compiled with "
            "--use_rocm, and no such prebuilt exists to fetch. Building ONNX "
            "Runtime from source with ROCm support is real but out of scope "
            "for this build script. Use an NVIDIA GPU, or build CPU-only by "
            "working around this check.")
    endif()

    # --- Resolve latest ONNX Runtime release + matching asset from GitHub ---
    set(ONNXRUNTIME_RELEASES_API "https://api.github.com/repos/microsoft/onnxruntime/releases/latest")
    file(DOWNLOAD "${ONNXRUNTIME_RELEASES_API}" "${CMAKE_BINARY_DIR}/onnxruntime_release.json"
         STATUS ONNXRUNTIME_RELEASE_STATUS
         HTTPHEADER "Accept: application/vnd.github+json")
    list(GET ONNXRUNTIME_RELEASE_STATUS 0 ONNXRUNTIME_RELEASE_STATUS_CODE)
    if(NOT ONNXRUNTIME_RELEASE_STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to query latest ONNX Runtime release from GitHub: ${ONNXRUNTIME_RELEASE_STATUS}")
    endif()

    file(READ "${CMAKE_BINARY_DIR}/onnxruntime_release.json" ONNXRUNTIME_RELEASE_JSON)
    string(JSON ONNXRUNTIME_VERSION_TAG GET "${ONNXRUNTIME_RELEASE_JSON}" tag_name)
    string(REGEX REPLACE "^v" "" ONNXRUNTIME_VERSION "${ONNXRUNTIME_VERSION_TAG}")
    message(STATUS "ONNX Runtime: latest GitHub release is ${ONNXRUNTIME_VERSION_TAG} (GPU vendor: ${ATHENA_GPU_VENDOR}, WSL: ${ATHENA_IS_WSL})")

    string(JSON ONNXRUNTIME_ASSET_COUNT LENGTH "${ONNXRUNTIME_RELEASE_JSON}" assets)
    if(ONNXRUNTIME_ASSET_COUNT GREATER 0)
        math(EXPR ONNXRUNTIME_ASSET_LAST "${ONNXRUNTIME_ASSET_COUNT} - 1")
    else()
        set(ONNXRUNTIME_ASSET_LAST -1)
    endif()

    # Substring patterns, not full filenames: the exact naming has already
    # changed once upstream (1.20.1's "-gpu-" became "-gpu_cuda12-" /
    # "-gpu_cuda13-" by 1.27.1). Matching by keyword against the *actual*
    # returned asset list -- and using its browser_download_url verbatim --
    # survives future renames that a constructed filename string wouldn't.
    if(ATHENA_GPU_VENDOR STREQUAL "NVIDIA")
        set(ONNXRUNTIME_ASSET_PATTERNS "linux-x64-gpu_cuda13" "linux-x64-gpu_cuda12" "linux-x64-gpu-")
        set(ONNXRUNTIME_ASSET_EXCLUDE "")
    else()
        set(ONNXRUNTIME_ASSET_PATTERNS "linux-x64-")
        set(ONNXRUNTIME_ASSET_EXCLUDE "gpu")
    endif()

    set(ONNXRUNTIME_URL "")
    set(ONNXRUNTIME_MATCHED_ASSET "")
    foreach(candidate_pattern IN LISTS ONNXRUNTIME_ASSET_PATTERNS)
        if(ONNXRUNTIME_URL OR ONNXRUNTIME_ASSET_LAST LESS 0)
            break()
        endif()
        foreach(asset_index RANGE 0 ${ONNXRUNTIME_ASSET_LAST})
            string(JSON asset_name GET "${ONNXRUNTIME_RELEASE_JSON}" assets ${asset_index} name)
            string(FIND "${asset_name}" "${candidate_pattern}" match_pos)
            if(match_pos EQUAL -1)
                continue()
            endif()
            if(ONNXRUNTIME_ASSET_EXCLUDE AND asset_name MATCHES "${ONNXRUNTIME_ASSET_EXCLUDE}")
                continue()
            endif()
            string(JSON asset_url GET "${ONNXRUNTIME_RELEASE_JSON}" assets ${asset_index} browser_download_url)
            set(ONNXRUNTIME_URL "${asset_url}")
            set(ONNXRUNTIME_MATCHED_ASSET "${asset_name}")
            break()
        endforeach()
    endforeach()

    if(NOT ONNXRUNTIME_URL)
        message(FATAL_ERROR
            "Could not find an ONNX Runtime Linux asset matching any of: "
            "${ONNXRUNTIME_ASSET_PATTERNS} in release ${ONNXRUNTIME_VERSION_TAG}. "
            "Assets may have been renamed again -- check "
            "https://github.com/microsoft/onnxruntime/releases/tag/${ONNXRUNTIME_VERSION_TAG}")
    endif()
    message(STATUS "ONNX Runtime: selected asset ${ONNXRUNTIME_MATCHED_ASSET}")
    set(ONNXRUNTIME_DOWNLOAD_NAME onnxruntime.tgz)
endif()

FetchContent_Declare(
  onnxruntime
  URL ${ONNXRUNTIME_URL}
  DOWNLOAD_NAME ${ONNXRUNTIME_DOWNLOAD_NAME}
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(onnxruntime)

# Common Warning Flags
if(MSVC)
    set(COMMON_WARNING_FLAGS
        /W4
        /WX
    )
else()
    set(COMMON_WARNING_FLAGS
        -Wall
        -Wextra
        -Werror
        -pedantic
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
endif()

# Output directories mirroring the original Make layout
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_CURRENT_SOURCE_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_CURRENT_SOURCE_DIR}/bin/lib)
endforeach()

# Mode-specific compilation flags (only for GCC/Clang)
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto -march=native")
endif()

# 1. Main Static Library target: athena_core
add_library(athena_core STATIC
    src/chess/attacks.cpp
    src/chess/board.cpp
    src/chrono/chrono.cpp
    src/engine/engine.cpp
    src/chess/fen.cpp
    src/logger/logger.cpp
    src/modifier/modifier.cpp
    src/chess/move.cpp
    src/chess/moves.cpp
    src/pgn/pgn.cpp
    src/utils/utils.cpp
    src/abi/c_api.cpp
    src/mcts/mcts.cpp
    src/nn/nn.cpp
    src/uci/uci.cpp
    src/selfplay/selfplay.cpp
    src/hardware/hardware.cpp
    src/tuner/tuning_parameters.cpp
)

if (ENABLE_AUTOTUNE)
    target_sources(athena_core PRIVATE src/tuner/auto_tuner.cpp src/tuner/utilization.cpp)
    target_compile_definitions(athena_core PRIVATE ENABLE_AUTOTUNE_FLAG)
    target_compile_definitions(athena_core PUBLIC ENABLE_AUTOTUNE_FLAG)
endif()

# Platform-specific hardware detection (see include/hardware/platform.h) --
# macos.cpp lands the same way once split out of hardware.cpp.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(athena_core PRIVATE src/hardware/linux.cpp src/hardware/posix.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(athena_core PRIVATE src/hardware/windows.cpp)
    target_link_libraries(athena_core PRIVATE dxgi)
endif()

target_compile_definitions(athena_core PRIVATE
    ATHENA_VERSION="${ATHENA_VERSION}"
)

# MSVC deprecates std::getenv and other C-library calls in favor of _s
# variants; our usage is safe (immediate read into a local), so silence
# the proprietary C4996 nags that /WX would otherwise make fatal.
if(MSVC)
    target_compile_definitions(athena_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_options(athena_core PRIVATE ${COMMON_WARNING_FLAGS})
target_include_directories(athena_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<BOOL:${WIN32}>:${onnxruntime_SOURCE_DIR}/build/native/include>
    $<$<NOT:$<BOOL:${WIN32}>>:${onnxruntime_SOURCE_DIR}/include>
)

if(WIN32)
    target_link_directories(athena_core PUBLIC ${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native)
else()
    target_link_directories(athena_core PUBLIC ${onnxruntime_SOURCE_DIR}/lib)
endif()

# CMAKE_DL_LIBS: utilization.cpp dlopen()s NVML for GPU usage measurement
target_link_libraries(athena_core PUBLIC onnxruntime ${CMAKE_DL_LIBS})

# 2. Main Executable target: athena (was mainh)
add_executable(athena main.cpp)
target_link_libraries(athena PRIVATE athena_core)

# 2.5 Perft executable: standalone rules-core correctness/speed check
# (see perft.cpp), driven by tools/perft.py. Always built alongside
# athena, in both Debug and Release, same as the main executable --
# there's no reason to gate a correctness-verification tool behind a
# build-type or an opt-in flag.
add_executable(perft perft.cpp)
target_link_libraries(perft PRIVATE athena_core)
target_compile_options(perft PRIVATE ${COMMON_WARNING_FLAGS})

if(WIN32)
    add_custom_command(TARGET athena POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.dll"
            $<TARGET_FILE_DIR:athena>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/onnx"
            "$<TARGET_FILE_DIR:athena>/onnx"
        COMMENT "Copying onnxruntime.dll and onnx models to binary directory"
    )
endif()

# 3. Tests and Coverage target
option(ENABLE_TESTING "Enable unit tests and coverage targets" OFF)
if(ENABLE_TESTING)
    find_package(GTest REQUIRED)
    find_package(Threads REQUIRED)

    add_executable(tests
        tests/unit/fen.test.cpp
        tests/unit/board.test.cpp
        tests/unit/engine.test.cpp
        tests/unit/utils.test.cpp
        tests/unit/move.test.cpp
        tests/unit/moves.test.cpp
        tests/unit/logger.test.cpp
        tests/unit/logger_internal.test.cpp
        tests/unit/chrono.test.cpp
        tests/unit/modifier.test.cpp
        tests/unit/pgn.test.cpp
        tests/unit/c_api.test.cpp
        tests/unit/mcts.test.cpp
        tests/unit/mcts_fallback.test.cpp
        tests/unit/nn.test.cpp
        tests/unit/uci.test.cpp
        tests/unit/selfplay.test.cpp
        tests/unit/hardware.test.cpp
        tests/unit/tuning_parameters.test.cpp
        tests/unit/utilization.test.cpp
        tests/unit/auto_tuner.test.cpp
        $<$<PLATFORM_ID:Linux>:${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/linux.test.cpp>
    )

    target_link_libraries(tests PRIVATE
        athena_core
        GTest::gtest_main
        GTest::gtest
        Threads::Threads
    )

    target_compile_options(tests PRIVATE ${COMMON_WARNING_FLAGS})

    # No RUNTIME_OUTPUT_DIRECTORY override here: CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG>
    # is set above for every config, and the per-config property takes precedence
    # over the plain one -- so an override here would be silently ignored and only
    # mislead about where bin/tests actually lands.

    # Coverage instrumentation is gcov-only, so it is skipped under MSVC:
    # -fprofile-arcs and friends are not MSVC flags and the build would fail
    # to configure. Windows builds compile and run the same unit tests without
    # coverage; the 100% gate is enforced on Linux, which builds every
    # non-Windows code path.
    #
    # -fprofile-update=atomic: Athena runs parallel MCTS search and self-play
    # worker threads, and gcov's default counter updates are not thread safe.
    # Without this, concurrent increments are lost and the profile comes out
    # self-contradictory -- a guarded line reporting fewer executions than the
    # line it guards -- which surfaces as lines that genuinely ran being
    # reported as never executed.
    if(NOT MSVC)
        set(ATHENA_COVERAGE_FLAGS -fprofile-arcs -ftest-coverage -fprofile-update=atomic)

        target_compile_options(athena_core PRIVATE ${ATHENA_COVERAGE_FLAGS})
        target_link_options(athena_core PRIVATE ${ATHENA_COVERAGE_FLAGS})

        target_compile_options(athena PRIVATE ${ATHENA_COVERAGE_FLAGS})
        target_link_options(athena PRIVATE ${ATHENA_COVERAGE_FLAGS})

        # perft links athena_core too, so it needs the same gcov instrumentation
        # flags at link time or __gcov_init/__gcov_exit/__gcov_merge_add are
        # left undefined (athena_core's object files were compiled with them).
        target_compile_options(perft PRIVATE ${ATHENA_COVERAGE_FLAGS})
        target_link_options(perft PRIVATE ${ATHENA_COVERAGE_FLAGS})

        target_compile_options(tests PRIVATE ${ATHENA_COVERAGE_FLAGS})
        target_link_options(tests PRIVATE ${ATHENA_COVERAGE_FLAGS})

        # 4. Coverage Custom Target (always runs generate_coverage.sh)
        add_custom_target(coverage
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build/generate_coverage.sh
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Running tests, generating coverage report and enforcing 100% policy..."
        )
    endif()
endif()

# 5. Installation Rules
include(GNUInstallDirs)

install(TARGETS athena
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/athena
)

# Generate and install pkg-config file
set(PREFIX ${CMAKE_INSTALL_PREFIX})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build/athena.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/athena.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/athena.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
