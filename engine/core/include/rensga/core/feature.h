// F5.12: profile feature gating. The cooker writes console_target.gen.h with ENGINE_PROFILE_<NAME> and one
// ENGINE_FEATURE_<FEATURE> 1 define per feature the ConsoleTarget has ("features" in ConsoleTarget.json).
//
// A core or api declaration the profile may lack is written
//     RENSGA_REQUIRES_FEATURE(OSK, "No on-screen keyboard on this profile; read the text from a UI field instead.")
//     static bool Open(...);
// When the project build sees a console_target.gen.h without ENGINE_FEATURE_OSK, the macro adds
// __attribute__((error(hint))), so a script that calls the function fails to compile and the compiler prints the hint.
// Translation units that do not see the generated header (the engine itself, the editor, host tools and tests) treat every
// feature as present. FEATURE names: LIGHTING SKINNING FOG OSK SAVEDIALOG BATTERY CPUCLOCK TEXTURESWIZZLE.
#pragma once

#if defined(__has_include)
#if __has_include("console_target.gen.h")
#include "console_target.gen.h"
#define RENSGA_HAS_TARGET_HEADER 1
#endif
#endif

// RENSGA_FEATURE_PRESENT_<F> is 1 when the feature is present or when no generated header is visible.
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_LIGHTING)
#define RENSGA_FEATURE_PRESENT_LIGHTING 1
#else
#define RENSGA_FEATURE_PRESENT_LIGHTING 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_SKINNING)
#define RENSGA_FEATURE_PRESENT_SKINNING 1
#else
#define RENSGA_FEATURE_PRESENT_SKINNING 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_FOG)
#define RENSGA_FEATURE_PRESENT_FOG 1
#else
#define RENSGA_FEATURE_PRESENT_FOG 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_OSK)
#define RENSGA_FEATURE_PRESENT_OSK 1
#else
#define RENSGA_FEATURE_PRESENT_OSK 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_SAVEDIALOG)
#define RENSGA_FEATURE_PRESENT_SAVEDIALOG 1
#else
#define RENSGA_FEATURE_PRESENT_SAVEDIALOG 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_BATTERY)
#define RENSGA_FEATURE_PRESENT_BATTERY 1
#else
#define RENSGA_FEATURE_PRESENT_BATTERY 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_CPUCLOCK)
#define RENSGA_FEATURE_PRESENT_CPUCLOCK 1
#else
#define RENSGA_FEATURE_PRESENT_CPUCLOCK 0
#endif
#if !defined(RENSGA_HAS_TARGET_HEADER) || defined(ENGINE_FEATURE_TEXTURESWIZZLE)
#define RENSGA_FEATURE_PRESENT_TEXTURESWIZZLE 1
#else
#define RENSGA_FEATURE_PRESENT_TEXTURESWIZZLE 0
#endif

#define RENSGA_FEATURE_CAT2_(a, b) a##b
#define RENSGA_FEATURE_CAT_(a, b) RENSGA_FEATURE_CAT2_(a, b)
#define RENSGA_FEATURE_ATTR_1(hint)
#if defined(__GNUC__) || defined(__clang__)
#define RENSGA_FEATURE_ATTR_0(hint) __attribute__((error(hint)))
#else
#define RENSGA_FEATURE_ATTR_0(hint) [[deprecated(hint)]]
#endif
#define RENSGA_REQUIRES_FEATURE(F, hint) \
  RENSGA_FEATURE_CAT_(RENSGA_FEATURE_ATTR_, RENSGA_FEATURE_CAT_(RENSGA_FEATURE_PRESENT_, F))(hint)
