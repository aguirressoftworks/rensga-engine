// MVP-09: Scalar is chosen by profile. float on PSP; the 20.12 fixed-point slot is reserved (PSX).
#pragma once

#if defined(RENSGA_SCALAR_FIXED)
#error "Fixed-point Scalar (20.12) is reserved for the PSX profile (F9.02) and not implemented yet."
#endif

namespace rensga {
using Scalar = float;
}
