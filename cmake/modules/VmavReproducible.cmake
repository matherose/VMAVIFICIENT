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

if(NOT APPLE AND NOT WIN32)
    target_link_options(vmav_compile_options INTERFACE
        "LINKER:--build-id=none"
        "LINKER:--sort-section=name")
endif()
