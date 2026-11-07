#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>

uint64_t timer_get_ticks(void);

static void u_to_s(uint32_t v, char *out)
{
    char t[10];
    int n = 0;
    if (v == 0)
        t[n++] = '0';
    while (v) {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    int p = 0;
    while (n)
        out[p++] = t[--n];
    out[p] = 0;
}

static uint32_t rng_state;

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state;
}

/* ============================ SNAKE ============================ */

#define SK_N 20
#define SK_MAX (SK_N * SK_N)
#define SK_STEP 8

static struct ui_win SW;
static int sbody_x[SK_MAX], sbody_y[SK_MAX];
static int sk_len, sk_dir, sk_ndir, sk_over, sk_score;
static int food_x, food_y;
static uint32_t sk_last;
static int sk_inited;

static void place_food(void)
{
    for (int tries = 0; tries < 200; tries++) {
        int fx = (int)(rng_next() % SK_N);
        int fy = (int)(rng_next() % SK_N);
        int on = 0;
        for (int i = 0; i < sk_len; i++)
            if (sbody_x[i] == fx && sbody_y[i] == fy) {
                on = 1;
                break;
            }
        if (!on) {
            food_x = fx;
            food_y = fy;
            return;
        }
    }
    sk_over = 1;
}

static void snake_reset(void)
{
    rng_state = (uint32_t)timer_get_ticks() | 1u;
    sk_len = 3;
    sk_dir = 1;
    sk_ndir = 1;
    sk_over = 0;
    sk_score = 0;
    for (int i = 0; i < sk_len; i++) {
        sbody_x[i] = 8 - i;
        sbody_y[i] = 10;
    }
    sk_last = (uint32_t)timer_get_ticks();
    place_food();
}

void snake_open(void)
{
    ui_fit(480, 560, &SW);
    snake_reset();
    sk_inited = 1;
}

static void snake_step(void)
{
    if (sk_len >= SK_MAX) {
        sk_over = 1;
        return;
    }
    sk_dir = sk_ndir;
    int hx = sbody_x[0], hy = sbody_y[0];
    if (sk_dir == 0)
        hy--;
    else if (sk_dir == 1)
        hx++;
    else if (sk_dir == 2)
        hy++;
    else
        hx--;
    if (hx < 0 || hy < 0 || hx >= SK_N || hy >= SK_N) {
        sk_over = 1;
        return;
    }
    for (int i = 0; i < sk_len - 1; i++)
        if (sbody_x[i] == hx && sbody_y[i] == hy) {
            sk_over = 1;
            return;
        }
    for (int i = sk_len; i >= 1; i--) {
        sbody_x[i] = sbody_x[i - 1];
        sbody_y[i] = sbody_y[i - 1];
    }
    sbody_x[0] = hx;
    sbody_y[0] = hy;
    if (hx == food_x && hy == food_y) {
        sk_len++;
        place_food();
    }
}

static void snake_set_dir(int d)
{
    if ((d + 2) % 4 == sk_dir)
        return;
    sk_ndir = d;
}

int snake_frame(int mx, int my, int clicked)
{
    if (!sk_inited)
        snake_open();
    int S = ui_scale();
    if (ui_window(&SW, "Snake", mx, my, clicked))
        return 1;

    char k;
    while (ps2_read_key(&k)) {
        if (k == JB_KEY_UP || k == 'w')
            snake_set_dir(0);
        else if (k == JB_KEY_RIGHT || k == 'd')
            snake_set_dir(1);
        else if (k == JB_KEY_DOWN || k == 's')
            snake_set_dir(2);
        else if (k == JB_KEY_LEFT || k == 'a')
            snake_set_dir(3);
        else if (k == '\n' || k == ' ')
            snake_reset();
    }

    if (!sk_over) {
        uint32_t now = (uint32_t)timer_get_ticks();
        if (now - sk_last >= SK_STEP) {
            sk_last = now;
            snake_step();
        }
    }

    int board = SW.w - 40 * S;
    if (board > SW.h - 120 * S)
        board = SW.h - 120 * S;
    int bx = SW.x + (SW.w - board) / 2;
    int by = SW.y + 52 * S;
    int cell = board / SK_N;

    ui_rect(bx, by, cell * SK_N, cell * SK_N, fb_rgb(20, 24, 34));
    ui_border(bx, by, cell * SK_N, cell * SK_N, fb_rgb(80, 96, 130));
    for (int i = 1; i < SK_N; i++) {
        ui_rect(bx + i * cell, by, 1, cell * SK_N, fb_rgb(30, 36, 50));
        ui_rect(bx, by + i * cell, cell * SK_N, 1, fb_rgb(30, 36, 50));
    }

    ui_round_rect(bx + food_x * cell + 2, by + food_y * cell + 2,
                  cell - 4, cell - 4, 3 * S, fb_rgb(225, 70, 70));

    for (int i = sk_len - 1; i >= 0; i--) {
        uint32_t c = i == 0 ? fb_rgb(120, 220, 120) : fb_rgb(60, 170, 90);
        ui_round_rect(bx + sbody_x[i] * cell + 1, by + sbody_y[i] * cell + 1,
                      cell - 2, cell - 2, 3 * S, c);
    }

    char score[16];
    u_to_s((uint32_t)sk_score, score);
    gfx_draw_string(bx, by + cell * SK_N + 10 * S, fb_rgb(150, 210, 255),
                    fb_rgb(30, 34, 46), "Score: ");
    gfx_draw_string(bx + 60 * S, by + cell * SK_N + 10 * S,
                    fb_rgb(230, 240, 250), fb_rgb(30, 34, 46), score);
    gfx_draw_string(bx, by + cell * SK_N + 30 * S, fb_rgb(180, 190, 210),
                    fb_rgb(30, 34, 46), "Arrows/WASD to move, Enter to restart");

    if (sk_over) {
        ui_round_rect(bx, by + cell * SK_N / 2 - 40 * S, cell * SK_N,
                      80 * S, 8 * S, fb_rgb(40, 20, 30));
        gfx_draw_string(bx + cell * SK_N / 2 - 44 * S,
                        by + cell * SK_N / 2 - 24 * S, fb_rgb(255, 120, 120),
                        fb_rgb(40, 20, 30), "Game Over");
        gfx_draw_string(bx + cell * SK_N / 2 - 88 * S,
                        by + cell * SK_N / 2 + 4 * S, fb_rgb(220, 225, 235),
                        fb_rgb(40, 20, 30), "Press Enter to restart");
    }
    return 0;
}

/* ============================ 2048 ============================ */

static struct ui_win GW;
static int g_grid[4][4];
static int g_score, g_gone, g_inited;

void g2048_open(void)
{
    ui_fit(460, 560, &GW);
    rng_state = (uint32_t)timer_get_ticks() | 1u;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            g_grid[r][c] = 0;
    g_score = 0;
    g_gone = 0;
    /* two starting tiles */
    {
        int placed = 0;
        while (placed < 2) {
            int r = (int)(rng_next() % 4), c = (int)(rng_next() % 4);
            if (g_grid[r][c] == 0) {
                g_grid[r][c] = (rng_next() % 10 == 0) ? 4 : 2;
                placed++;
            }
        }
    }
    g_inited =1;
}

static int slide_line(int v[4])
{
    int old[4];
    for (int i = 0; i < 4; i++)
        old[i] = v[i];
    int t[4], n = 0;
    for (int i = 0; i < 4; i++)
        if (v[i])
            t[n++] = v[i];
    for (int i = 0; i + 1 < n; i++) {
        if (t[i] == t[i + 1]) {
            t[i] *= 2;
            g_score += t[i];
            t[i + 1] = 0;
            i++;
        }
    }
    int m = 0;
    for (int i = 0; i < n; i++)
        if (t[i])
            v[m++] = t[i];
    while (m < 4)
        v[m++] = 0;
    for (int i = 0; i < 4; i++)
        if (old[i] != v[i])
            return 1;
    return 0;
}

static int g_move(int dir)
{
    int moved = 0;
    if (dir == 0 || dir == 2) {
        for (int c = 0; c < 4; c++) {
            int line[4];
            for (int r = 0; r < 4; r++)
                line[r] = g_grid[r][c];
            if (dir == 2) {
                int tmp;
                for (int r = 0; r < 2; r++) {
                    tmp = line[r];
                    line[r] = line[3 - r];
                    line[3 - r] = tmp;
                }
            }
            if (slide_line(line))
                moved = 1;
            if (dir == 2) {
                int tmp;
                for (int r = 0; r < 2; r++) {
                    tmp = line[r];
                    line[r] = line[3 - r];
                    line[3 - r] = tmp;
                }
            }
            for (int r = 0; r < 4; r++)
                g_grid[r][c] = line[r];
        }
    } else {
        for (int r = 0; r < 4; r++) {
            int line[4];
            for (int c = 0; c < 4; c++)
                line[c] = g_grid[r][c];
            if (dir == 1) {
                int tmp;
                for (int c = 0; c < 2; c++) {
                    tmp = line[c];
                    line[c] = line[3 - c];
                    line[3 - c] = tmp;
                }
            }
            if (slide_line(line))
                moved = 1;
            if (dir == 1) {
                int tmp;
                for (int c = 0; c < 2; c++) {
                    tmp = line[c];
                    line[c] = line[3 - c];
                    line[3 - c] = tmp;
                }
            }
            for (int c = 0; c < 4; c++)
                g_grid[r][c] = line[c];
        }
    }
    return moved;
}

static void g_spawn(void)
{
    int empty[16][2], ec = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (g_grid[r][c] == 0) {
                empty[ec][0] = r;
                empty[ec][1] = c;
                ec++;
            }
    if (ec == 0)
        return;
    int pick = (int)(rng_next() % (uint32_t)ec);
    g_grid[empty[pick][0]][empty[pick][1]] =
        (rng_next() % 10 == 0) ? 4 : 2;
}

static int g_can_move(void)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (g_grid[r][c] == 0)
                return 1;
            if (r + 1 < 4 && g_grid[r][c] == g_grid[r + 1][c])
                return 1;
            if (c + 1 < 4 && g_grid[r][c] == g_grid[r][c + 1])
                return 1;
        }
    return 0;
}

static uint32_t tile_color(int v)
{
    switch (v) {
    case 2: return fb_rgb(238, 228, 218);
    case 4: return fb_rgb(237, 224, 200);
    case 8: return fb_rgb(242, 177, 121);
    case 16: return fb_rgb(245, 149, 99);
    case 32: return fb_rgb(246, 124, 95);
    case 64: return fb_rgb(246, 94, 59);
    case 128: return fb_rgb(237, 207, 114);
    case 256: return fb_rgb(237, 204, 97);
    case 512: return fb_rgb(237, 200, 80);
    case 1024: return fb_rgb(237, 197, 63);
    case 2048: return fb_rgb(237, 194, 46);
    default: return fb_rgb(60, 58, 50);
    }
}

int g2048_frame(int mx, int my, int clicked)
{
    if (!g_inited)
        g2048_open();
    int S = ui_scale();
    if (ui_window(&GW, "2048", mx, my, clicked))
        return 1;

    char k;
    while (ps2_read_key(&k)) {
        int dir = -1;
        if (k == JB_KEY_UP || k == 'w')
            dir = 0;
        else if (k == JB_KEY_RIGHT || k == 'd')
            dir = 1;
        else if (k == JB_KEY_DOWN || k == 's')
            dir = 2;
        else if (k == JB_KEY_LEFT || k == 'a')
            dir = 3;
        else if (k == '\n' || k == ' ')
            g2048_open();
        if (dir >= 0 && !g_gone) {
            if (g_move(dir)) {
                g_spawn();
                if (!g_can_move())
                    g_gone = 1;
            }
        }
    }

    int board = GW.w - 40 * S;
    if (board > GW.h - 120 * S)
        board = GW.h - 120 * S;
    int bx = GW.x + (GW.w - board) / 2;
    int by = GW.y + 52 * S;
    int gap = 8 * S;
    int cell = (board - gap * 5) / 4;

    ui_round_rect(bx, by, board, board, 8 * S, fb_rgb(187, 173, 160));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int tx = bx + gap + c * (cell + gap);
            int ty = by + gap + r * (cell + gap);
            int v = g_grid[r][c];
            if (v == 0) {
                ui_round_rect(tx, ty, cell, cell, 4 * S,
                              fb_rgb(205, 193, 180));
            } else {
                ui_round_rect(tx, ty, cell, cell, 4 * S, tile_color(v));
                char num[8];
                u_to_s((uint32_t)v, num);
                int tw = gfx_text_width(num);
                uint32_t tc = v <= 4 ? fb_rgb(119, 110, 101)
                                     : fb_rgb(249, 246, 242);
                gfx_draw_string(tx + (cell - tw) / 2, ty + cell / 2 - 8 * S,
                                tc, tile_color(v), num);
            }
        }

    char score[16];
    u_to_s((uint32_t)g_score, score);
    gfx_draw_string(bx, by + board + 10 * S, fb_rgb(150, 210, 255),
                    fb_rgb(30, 34, 46), "Score: ");
    gfx_draw_string(bx + 60 * S, by + board + 10 * S, fb_rgb(230, 240, 250),
                    fb_rgb(30, 34, 46), score);
    gfx_draw_string(bx, by + board + 30 * S, fb_rgb(180, 190, 210),
                    fb_rgb(30, 34, 46), "Arrows/WASD to slide, Enter to restart");

    if (g_gone) {
        ui_round_rect(bx, by + board / 2 - 40 * S, board, 80 * S, 8 * S,
                      fb_rgb(40, 20, 30));
        gfx_draw_string(bx + board / 2 - 52 * S, by + board / 2 - 24 * S,
                        fb_rgb(255, 120, 120), fb_rgb(40, 20, 30),
                        "No moves left");
        gfx_draw_string(bx + board / 2 - 88 * S, by + board / 2 + 4 * S,
                        fb_rgb(220, 225, 235), fb_rgb(40, 20, 30),
                        "Press Enter to restart");
    }
    return 0;
}
