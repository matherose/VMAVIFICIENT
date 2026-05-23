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

# Linker flags vary per target — every ELF/PE platform has a
# "embed something non-deterministic" knob that has to be turned off
# explicitly:
#
#   * Linux LLD          — `--build-id` defaults to a content+timestamp
#                          hash; `--sort-section` randomizes layout.
#   * lld in PE mode     — `--build-id` is the only Linux-style knob;
#     (llvm-mingw)         section ordering is already deterministic
#                          because the PE format has no equivalent of
#                          ELF's `SHF_LINK_ORDER` reordering.
#   * Apple ld64         — no flag needed. Default LC_UUID is a hash
#                          of the linker output bits; stripping it with
#                          `-no_uuid` removes the load command entirely
#                          and dyld refuses to load the binary. Given
#                          deterministic content (SOURCE_DATE_EPOCH +
#                          our prefix-maps above), the content-derived
#                          UUID is itself deterministic.
#
# Phase 0 had every flag off because zig cc's lld wrapper rejected them;
# we no longer use zig cc, so the supported set is on per-platform.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:--build-id=none"
        "LINKER:--sort-section=name")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:--build-id=none")
endif()
