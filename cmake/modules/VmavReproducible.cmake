# VmavReproducible.cmake — reproducible-build flags.
#
# When VMAV_REPRODUCIBLE=ON, the resulting binary is byte-identical across
# two clean builds with the same SOURCE_DATE_EPOCH. This is required for
# the release-gate `scripts/repro_check.sh`.

include_guard(GLOBAL)

if(NOT VMAV_REPRODUCIBLE)
    return()
endif()

# SOURCE_DATE_EPOCH respected by Clang's __DATE__/__TIME__ since Clang 11.
if(NOT DEFINED ENV{SOURCE_DATE_EPOCH})
    # Derive from VERSION file mtime so config-time changes don't perturb
    # the binary across runs.
    file(TIMESTAMP "${CMAKE_SOURCE_DIR}/VERSION" _sde "%s" UTC)
    set(ENV{SOURCE_DATE_EPOCH} "${_sde}")
endif()

target_compile_options(vmav_compile_options INTERFACE
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}=vmav
    -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=vmav
    -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=vmav
    -fno-ident
    -Wno-builtin-macro-redefined
    "-D__DATE__=\"redacted\""
    "-D__TIME__=\"redacted\""
    "-D__TIMESTAMP__=\"redacted\"")

# Linker flags vary per target. LLD on Linux accepts both --build-id and
# --sort-section; mach-o (Apple ld64) understands neither; lld in PE mode
# (llvm-mingw) accepts --build-id but not --sort-section. Scope per system.
#
# Phase 0 had these flags off entirely because zig cc's lld wrapper rejected
# them; we no longer use zig cc, so the linux gate (which is where
# scripts/repro_check.sh runs in CI) gets the full set.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:--build-id=none"
        "LINKER:--sort-section=name")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:--build-id=none")
endif()
