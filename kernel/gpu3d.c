#include <stdint.h>
#include <stddef.h>
#include <kernel/gpu3d.h>
#include <kernel/fb.h>
#include <kernel/heap.h>

#define ONE   65536
#define FOCAL 300
#define DIST  (4 * ONE)
#define NEAR  ONE

#define QM(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 16))

static int32_t qsin_t[256], qcos_t[256];
static int table_ready;

static int VW, VH;
static int32_t *zbuf;

static int32_t qsin(uint16_t a) { return qsin_t[(a >> 8) & 255]; }
static int32_t qcos(uint16_t a) { return qcos_t[(a >> 8) & 255]; }

static void build_table(void)
{
    const int32_t PI = 205887;
    for (int i = 0; i < 256; i++) {
        int q = (i >> 6) & 3, p = i & 63;
        int32_t t = (int32_t)(((int64_t)PI * p) / 128);
        int32_t t2 = QM(t, t), t3 = QM(t2, t);
        int32_t t4 = QM(t2, t2), t5 = QM(t3, t2);
        int32_t t6 = QM(t3, t3), t7 = QM(t5, t2);
        int32_t s = t - QM(t3, 10923) + QM(t5, 546) - QM(t7, 13);
        int32_t c = ONE - QM(t2, 32768) + QM(t4, 2731) - QM(t6, 91);
        switch (q) {
        case 0:
            qsin_t[i] = s;  qcos_t[i] = c;  break;
        case 1:
            qsin_t[i] = c;  qcos_t[i] = -s; break;
        case 2:
            qsin_t[i] = -s; qcos_t[i] = -c; break;
        default:
            qsin_t[i] = -c; qcos_t[i] = s;  break;
        }
    }
    table_ready = 1;
}

int g3d_init(int view_w, int view_h)
{
    if (!table_ready)
        build_table();
    VW = view_w;
    VH = view_h;
    if (zbuf)
        return 1;
    zbuf = kmalloc((size_t)view_w * view_h * 4);
    return zbuf != NULL;
}

void g3d_shutdown(void)
{
    if (zbuf) {
        kfree(zbuf);
        zbuf = NULL;
    }
}

static struct g3d_v3 cube_v[8];
static struct g3d_face cube_f[12];
static struct g3d_mesh cube_m;
static int cube_ready;

const struct g3d_mesh *g3d_cube(int32_t half)
{
    static const int8_t sp[8][3] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}
    };
    for (int i = 0; i < 8; i++) {
        cube_v[i].x = sp[i][0] * half;
        cube_v[i].y = sp[i][1] * half;
        cube_v[i].z = sp[i][2] * half;
    }
    if (!cube_ready) {
        struct g3d_face fc[12] = {
            {0, 2, 1, 220, 70, 70},   {0, 3, 2, 220, 70, 70},
            {4, 5, 6, 70, 200, 100},  {4, 6, 7, 70, 200, 100},
            {0, 4, 7, 70, 120, 230},  {0, 7, 3, 70, 120, 230},
            {1, 2, 6, 230, 200, 70},  {1, 6, 5, 230, 200, 70},
            {0, 1, 5, 70, 210, 210},  {0, 5, 4, 70, 210, 210},
            {3, 7, 6, 200, 90, 210},  {3, 6, 2, 200, 90, 210}
        };
        for (int i = 0; i < 12; i++)
            cube_f[i] = fc[i];
        cube_m.v = cube_v;
        cube_m.f = cube_f;
        cube_m.nv = 8;
        cube_m.nf = 12;
        cube_ready = 1;
    }
    return &cube_m;
}

struct spoint {
    int x, y;
    int32_t z;
    int32_t wx, wy, wz;
    int clip;
};

static int64_t isqrt64(int64_t v)
{
    if (v <= 0)
        return 0;
    int64_t x = v, y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + v / x) >> 1;
    }
    return x;
}

static uint32_t shade_color(uint8_t r, uint8_t g, uint8_t b, int32_t k)
{
    int rr = (int)(((int32_t)r * k) >> 16);
    int gg = (int)(((int32_t)g * k) >> 16);
    int bb = (int)(((int32_t)b * k) >> 16);
    if (rr > 255) rr = 255;
    if (rr < 0) rr = 0;
    if (gg > 255) gg = 255;
    if (gg < 0) gg = 0;
    if (bb > 255) bb = 255;
    if (bb < 0) bb = 0;
    return fb_rgb((uint8_t)rr, (uint8_t)gg, (uint8_t)bb);
}

static int lerpi(int a, int b, int num, int den)
{
    if (den == 0)
        return a;
    return a + (int)(((int64_t)(b - a) * num) / den);
}

static int32_t lerpz(int32_t a, int32_t b, int num, int den)
{
    if (den == 0)
        return a;
    return a + (int32_t)(((int64_t)(b - a) * num) / den);
}

static void raster(int ox, int oy, struct spoint a, struct spoint b,
                   struct spoint c, uint32_t color)
{
    struct spoint t;
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (b.y > c.y) { t = b; b = c; c = t; }
    if (a.y > b.y) { t = a; a = b; b = t; }

    int total = c.y - a.y;
    if (total <= 0)
        return;

    int ys = a.y < 0 ? 0 : a.y;
    int ye = c.y > VH - 1 ? VH - 1 : c.y;
    int screen_w = (int)fb_width();
    for (int y = ys; y <= ye; y++) {
        uint32_t *row = fb_back_row(oy + y);
        if (!row)
            continue;
        int upper = (y < b.y) || (b.y == a.y);
        int x_long = lerpi(a.x, c.x, y - a.y, total);
        int32_t z_long = lerpz(a.z, c.z, y - a.y, total);
        int x_short;
        int32_t z_short;
        if (upper) {
            int d = b.y - a.y;
            if (d <= 0) { x_short = x_long; z_short = z_long; }
            else {
                x_short = lerpi(a.x, b.x, y - a.y, d);
                z_short = lerpz(a.z, b.z, y - a.y, d);
            }
        } else {
            int d = c.y - b.y;
            if (d <= 0) { x_short = x_long; z_short = z_long; }
            else {
                x_short = lerpi(b.x, c.x, y - b.y, d);
                z_short = lerpz(b.z, c.z, y - b.y, d);
            }
        }
        int xl = x_long, xr = x_short;
        int32_t zl = z_long, zr = z_short;
        if (xl > xr) {
            int ti = xl; xl = xr; xr = ti;
            int32_t tz = zl; zl = zr; zr = tz;
        }
        int xs = xl < 0 ? 0 : xl;
        int xe = xr > VW - 1 ? VW - 1 : xr;
        if (xe < xs)
            continue;
        int span = xr - xl;
        if (span <= 0)
            span = 1;
        int32_t z = zl + (int32_t)(((int64_t)(zr - zl) * (xs - xl)) / span);
        int32_t dz = (zr - zl) / span;
        for (int x = xs; x <= xe; x++) {
            int32_t *zp = &zbuf[y * VW + x];
            if (z < *zp) {
                *zp = z;
                /* Direct row write: no per-pixel bounds check, no per-pixel
                   dirty-rect merge. The whole viewport is marked dirty once
                   at the end of g3d_render. */
                int ax = ox + x;
                if ((unsigned)ax < (unsigned)screen_w)
                    row[ax] = color;
            }
            z += dz;
        }
    }
}

void g3d_render(const struct g3d_mesh *m, int ox, int oy,
                uint16_t ang_x, uint16_t ang_y)
{
    if (!zbuf || !m)
        return;

    int n = m->nv > G3D_MAX_VERTS ? G3D_MAX_VERTS : m->nv;
    for (int i = 0; i < VW * VH; i++)
        zbuf[i] = 0x7fffffff;

    int32_t sx = qsin(ang_x), cx = qcos(ang_x);
    int32_t sy = qsin(ang_y), cy = qcos(ang_y);

    static struct spoint tv[G3D_MAX_VERTS];
    for (int i = 0; i < n; i++) {
        int32_t x = m->v[i].x, y = m->v[i].y, z = m->v[i].z;

        int32_t x1 = QM(x, cy) + QM(z, sy);
        int32_t z1 = -QM(x, sy) + QM(z, cy);
        int32_t y1 = QM(y, cx) - QM(z1, sx);
        int32_t z2 = QM(y, sx) + QM(z1, cx);
        int32_t zc = z2 + DIST;

        tv[i].z = zc;
        tv[i].wx = x1;
        tv[i].wy = y1;
        tv[i].wz = z2;
        tv[i].clip = zc <= NEAR;
        int32_t sxp = (int32_t)(((int64_t)x1 * FOCAL) / zc);
        int32_t syp = (int32_t)(((int64_t)y1 * FOCAL) / zc);
        tv[i].x = VW / 2 + sxp;
        tv[i].y = VH / 2 - syp;
    }

    /* Light direction in camera space, unit vector Q16 (points to light). */
    const int32_t LX = -29491;   /* -0.45 */
    const int32_t LY = -36045;   /* -0.55 */
    const int32_t LZ = -45875;   /* -0.70 */

    for (int fi = 0; fi < m->nf; fi++) {
        const struct g3d_face *fc = &m->f[fi];
        struct spoint p0 = tv[fc->a], p1 = tv[fc->b], p2 = tv[fc->c];
        if (p0.clip || p1.clip || p2.clip)
            continue;

        int64_t ux = (int64_t)p1.wx - p0.wx;
        int64_t uy = (int64_t)p1.wy - p0.wy;
        int64_t uz = (int64_t)p1.wz - p0.wz;
        int64_t vx = (int64_t)p2.wx - p0.wx;
        int64_t vy = (int64_t)p2.wy - p0.wy;
        int64_t vz = (int64_t)p2.wz - p0.wz;

        /* Cross product edges (Q32), then downscale to Q16 to avoid overflow. */
        int64_t nxp = (uy * vz - uz * vy) >> 16;
        int64_t nyp = (uz * vx - ux * vz) >> 16;
        int64_t nzp = (ux * vy - uy * vx) >> 16;

        /* Backface cull: camera looks toward +z, front faces have nz < 0. */
        if (nzp >= 0)
            continue;

        int64_t nl = isqrt64(nxp * nxp + nyp * nyp + nzp * nzp);
        if (nl <= 0)
            continue;
        int64_t nux = nxp * ONE / nl;
        int64_t nuy = nyp * ONE / nl;
        int64_t nuz = nzp * ONE / nl;

        int64_t dot = (nux * LX + nuy * LY + nuz * LZ) / ONE;
        if (dot < 0) dot = 0;
        if (dot > ONE) dot = ONE;

        int64_t k = (int64_t)(35 * ONE) + 65 * dot;
        k /= 100;
        if (k < 0) k = 0;
        if (k > ONE) k = ONE;

        raster(ox, oy, p0, p1, p2,
               shade_color(fc->cr, fc->cg, fc->cb, (int32_t)k));
    }

    /* Every pixel of the viewport may have been touched, so commit the whole
       rectangle once instead of once per drawn pixel. */
    fb_mark_dirty(ox, oy, VW, VH);
}
