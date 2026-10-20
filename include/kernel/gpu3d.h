#ifndef JACKBOT_GPU3D_H
#define JACKBOT_GPU3D_H

#include <stdint.h>

/* JackBOT software 3D pipeline. Fixed-point Q16.16, no FPU/SSE used. */

#define G3D_MAX_VERTS 64
#define G3D_MAX_FACES 96

struct g3d_v3 { int32_t x, y, z; };
struct g3d_face { uint8_t a, b, c; uint8_t cr, cg, cb; };
struct g3d_mesh {
    struct g3d_v3 *v;
    struct g3d_face *f;
    int nv, nf;
};

int g3d_init(int view_w, int view_h);
void g3d_shutdown(void);
const struct g3d_mesh *g3d_cube(int32_t half);
void g3d_render(const struct g3d_mesh *m, int ox, int oy,
                uint16_t ang_x, uint16_t ang_y);

#endif
