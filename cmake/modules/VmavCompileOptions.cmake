# VmavCompileOptions.cmake — strict C11 + Clang/LLVM compile/link flags.
#
# Exposes an INTERFACE target `vmav::compile_options` that every other target
# links to, ensuring consistent flags project-wide.

include_guard(GLOBAL)

add_library(vmav_compile_options INTERFACE)
add_library(vmav::compile_options ALIAS vmav_compile_options)

target_compile_features(vmav_compile_options INTERFACE c_std_11)

# Strict C11 + warnings-as-errors. Pedantic intentionally allows the few
# Clang extensions we explicitly opt into (e.g. statement expressions in
# macros, __attribute__((cleanup))). Disable any warning here that proves
# noisy in practice; keep the bar high.
target_compile_options(vmav_compile_options INTERFACE
    -std=c11
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wcast-align
    -Wpointer-arith
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wformat=2
    -Wnull-dereference
    -Wdouble-promotion
    -Wvla
    -fno-common
    -fvisibility=hidden
    -fno-strict-aliasing)

# Per-config knobs.
target_compile_options(vmav_compile_options INTERFACE
    $<$<CONFIG:Release>:-O3 -DNDEBUG -ffp-contract=fast>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
    $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>)

# Sanitizers (only in Debug + when VMAV_SANITIZE=ON).
if(VMAV_SANITIZE)
    target_compile_options(vmav_compile_options INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls)
    target_link_options(vmav_compile_options INTERFACE
        -fsanitize=address,undefined)
endif()

# Link-time optimization (off automatically for sanitizer builds).
if(VMAV_LTO AND NOT VMAV_SANITIZE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err LANGUAGES C)
    if(_ipo_ok)
        # Defer to the project to set INTERPROCEDURAL_OPTIMIZATION per-target,
        # but propagate via the interface so consumers pick it up.
        set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO requested but unsupported: ${_ipo_err}")
    endif()
endif()

# Use lld where available (much faster, deterministic, smaller binaries).
include(CheckLinkerFlag)
check_linker_flag(C "-fuse-ld=lld" _has_lld)
if(_has_lld)
    target_link_options(vmav_compile_options INTERFACE -fuse-ld=lld)
endif()

# Dead-strip unused sections to keep the static binary tight.
if(APPLE)
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:-dead_strip")
else()
    target_compile_options(vmav_compile_options INTERFACE
        -ffunction-sections -fdata-sections)
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:--gc-sections")
endif()

# Platform feature macros so POSIX-isms work on glibc/musl alike.
if(UNIX AND NOT APPLE)
    target_compile_definitions(vmav_compile_options INTERFACE
        _GNU_SOURCE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
elseif(APPLE)
    target_compile_definitions(vmav_compile_options INTERFACE
        _DARWIN_C_SOURCE)
elseif(WIN32)
    target_compile_definitions(vmav_compile_options INTERFACE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _CRT_SECURE_NO_WARNINGS)
endif()
