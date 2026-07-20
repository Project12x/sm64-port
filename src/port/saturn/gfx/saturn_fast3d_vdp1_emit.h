#ifndef SM64_SATURN_FAST3D_VDP1_EMIT_H
#define SM64_SATURN_FAST3D_VDP1_EMIT_H

#include "saturn_fast3d_frontend.h"
#include "saturn_vdp1_backend.h"

/* Walks frontend->resolved[] in depth-bucket order (far-to-near) and
 * writes real VDP1 degenerate-quad polygon commands via backend. This is
 * the only Yaul-dependent half of the Fast3D lowering pipeline --
 * saturn_fast3d_frontend.c itself stays Yaul-free so it remains
 * host-testable (see the design spec's review finding on this exact
 * compile-boundary problem). */
void sm64_saturn_fast3d_vdp1_emit(sm64_saturn_fast3d_frontend_t *frontend,
                                  sm64_saturn_vdp1_backend_t *backend);

#endif
