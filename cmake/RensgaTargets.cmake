# Shared target helpers. Warnings are errors for every Rensga target (not third_party).

# Common defaults for any Rensga-owned target.
function(rensga_apply_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  # PSP newlib: uint32_t is long, so %u/%x on u32 trips -Wformat; keep it a warning there.
  if(RENSGA_TARGET STREQUAL "psp")
    target_compile_options(${target} PRIVATE -Wno-error=format)
  endif()
endfunction()

# Runtime targets: no exceptions, no RTTI (RULES section 3).
# NOTE: a TU that derives from a polymorphic class declared in engine/core
# (ILogSink, IPlatform, Behaviour, ...) must also be compiled with -fno-rtti,
# otherwise the typeinfo symbol of the base is missing at link time.
# Use rensga_runtime_target() on such targets (the unit tests do).
function(rensga_runtime_target target)
  rensga_apply_defaults(${target})
  target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
endfunction()

# Host-only tools: exceptions and RTTI allowed.
function(rensga_host_target target)
  rensga_apply_defaults(${target})
endfunction()

# Link a self-registering module (target module, cooker plugin) so its static
# registrar survives. Usage: rensga_link_whole_archive(app PRIVATE rensga_target_psp)
function(rensga_link_whole_archive target visibility lib)
  if(TARGET ${lib})
    target_link_libraries(${target} ${visibility} "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lib}>")
  endif()
endfunction()
