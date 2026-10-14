/* Test CPU Vita : mesure ce que coutent, par image, des charges typiques d'un jeu 2D
   (animation de personnages, collisions, recherche de chemin, petit interpreteur de scripts).
   Rien ne depend des fichiers du jeu. Les charges sont des substituts realistes, pas le vrai code. */
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static volatile float sink_f;
static volatile int sink_i;
static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }

/* ---------- 1. Animation de personnages (os + deformation de 38 quads) ---------- */
#define NB 32
#define NP 38
typedef struct { float a, b, c, d, tx, ty; } M2;
static void m2mul(M2 *o, const M2 *p, const M2 *q)
{
    o->a = p->a * q->a + p->c * q->b;   o->c = p->a * q->c + p->c * q->d;
    o->b = p->b * q->a + p->d * q->b;   o->d = p->b * q->c + p->d * q->d;
    o->tx = p->a * q->tx + p->c * q->ty + p->tx;
    o->ty = p->b * q->tx + p->d * q->ty + p->ty;
}
static float qv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
static float outv[NP * 4 * 2];
static void work_anim(int nchars, int frame)
{
    M2 world[NB];
    float acc = 0;
    for (int ch = 0; ch < nchars; ch++) {
        float t = frame * 0.05f + ch * 0.7f;
        for (int b = 0; b < NB; b++) {
            float ang = sinf(t * (1.0f + (b % 5) * 0.2f) + b) * 0.5f;
            float ca = cosf(ang), sa = sinf(ang);
            float len = 8.0f + (b % 4) * 3.0f;
            M2 loc = { ca, sa, -sa, ca, len, 0 };
            if (b == 0) world[0] = loc;
            else m2mul(&world[b], &world[(b - 1) / 2], &loc);
        }
        for (int p = 0; p < NP; p++) {
            const M2 *m0 = &world[p % NB], *m1 = &world[(p * 7) % NB];
            float w = (p % 3) * 0.33f;
            for (int v = 0; v < 4; v++) {
                float x = qv[v][0] * 10, y = qv[v][1] * 10;
                float x0 = m0->a * x + m0->c * y + m0->tx, y0 = m0->b * x + m0->d * y + m0->ty;
                float x1 = m1->a * x + m1->c * y + m1->tx, y1 = m1->b * x + m1->d * y + m1->ty;
                outv[(p * 4 + v) * 2] = x0 * (1 - w) + x1 * w;
                outv[(p * 4 + v) * 2 + 1] = y0 * (1 - w) + y1 * w;
            }
        }
        acc += outv[(ch % NP) * 8];
    }
    sink_f = acc;
}

/* ---------- 2. Collisions : entites contre grille de tuiles + entre elles ---------- */
#define GW 256
#define GH 128
static unsigned char grid[GW * GH];
typedef struct { float x, y, vx, vy; } Ent;
static Ent ents[4096];
#define CELL 64
#define HW 64
#define HH 32
static int hhead[HW * HH], hnext[4096];
static void phys_init(void)
{
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++)
        grid[y * GW + x] = (y > GH - 6) || ((x * 7 + y * 13) % 29 == 0 && y > 20)
            || (x % 40 == 0 && x > 30 && y < GH - 6 && !(((x / 40) % 2) ? (y >= 10 && y < 16) : (y >= 90 && y < 96)));
    for (int i = 0; i < 4096; i++) {
        ents[i].x = 200 + (i * 37) % (GW * 16 - 400); ents[i].y = 100 + (i * 53) % (GH * 16 / 2);
        ents[i].vx = ((i % 7) - 3) * 0.7f; ents[i].vy = 0;
    }
}
static int solid(float x, float y)
{
    int tx = (int)(x / 16), ty = (int)(y / 16);
    if (tx < 0 || ty < 0 || tx >= GW || ty >= GH) return 1;
    return grid[ty * GW + tx];
}
static void work_phys(int n)
{
    for (int i = 0; i < n; i++) {
        Ent *e = &ents[i];
        e->vy += 0.3f;
        float nx = e->x + e->vx;
        if (solid(nx + (e->vx > 0 ? 6 : -6), e->y - 4) || solid(nx + (e->vx > 0 ? 6 : -6), e->y + 6)) e->vx = -e->vx; else e->x = nx;
        float ny = e->y + e->vy;
        if (solid(e->x - 5, ny + 8) || solid(e->x + 5, ny + 8)) { e->vy = -4.0f; } else e->y = ny;
        if (e->x < 20 || e->x > GW * 16 - 20) e->vx = -e->vx;
    }
    memset(hhead, -1, sizeof(hhead));
    for (int i = 0; i < n; i++) {
        int cx = (int)(ents[i].x / CELL) % HW, cy = (int)(ents[i].y / CELL) % HH;
        if (cx < 0) cx = 0; if (cy < 0) cy = 0;
        int c = cy * HW + cx; hnext[i] = hhead[c]; hhead[c] = i;
    }
    int hits = 0;
    for (int i = 0; i < n; i++) {
        int cx = (int)(ents[i].x / CELL) % HW, cy = (int)(ents[i].y / CELL) % HH;
        if (cx < 0) cx = 0; if (cy < 0) cy = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int x2 = cx + dx, y2 = cy + dy;
            if (x2 < 0 || y2 < 0 || x2 >= HW || y2 >= HH) continue;
            for (int j = hhead[y2 * HW + x2]; j >= 0; j = hnext[j]) {
                if (j <= i) continue;
                float ddx = ents[i].x - ents[j].x, ddy = ents[i].y - ents[j].y;
                if (ddx * ddx + ddy * ddy < 144) hits++;
            }
        }
    }
    sink_i = hits;
}

/* ---------- 3. Recherche de chemin A* sur la grille ---------- */
#define PN (GW * GH)
static int pg[PN], pparent[PN], pstamp[PN], pclosed[PN], heap[PN * 2], hsz, stampv;
static float hf[PN * 2];
static void hpush(int node, float f)
{
    int i = hsz++; heap[i] = node; hf[i] = f;
    while (i > 0) { int p = (i - 1) / 2; if (hf[p] <= hf[i]) break;
        int tn = heap[p]; float tf = hf[p]; heap[p] = heap[i]; hf[p] = hf[i]; heap[i] = tn; hf[i] = tf; i = p; }
}
static int hpop(void)
{
    int r = heap[0]; hsz--; heap[0] = heap[hsz]; hf[0] = hf[hsz]; int i = 0;
    for (;;) { int l = 2 * i + 1, rr = l + 1, m = i;
        if (l < hsz && hf[l] < hf[m]) m = l; if (rr < hsz && hf[rr] < hf[m]) m = rr; if (m == i) break;
        int tn = heap[m]; float tf = hf[m]; heap[m] = heap[i]; hf[m] = hf[i]; heap[i] = tn; hf[i] = tf; i = m; }
    return r;
}
static int astar(int sx, int sy, int gx, int gy)
{
    stampv++; hsz = 0; int start = sy * GW + sx, goal = gy * GW + gx, expanded = 0;
    pg[start] = 0; pparent[start] = -1; pstamp[start] = stampv; pclosed[start] = 0; hpush(start, 0);
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    while (hsz > 0) {
        int cur = hpop();
        if (pclosed[cur] == stampv) continue;
        pclosed[cur] = stampv; expanded++;
        if (cur == goal) break;
        int cx = cur % GW, cy = cur / GW;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
            int nn = ny * GW + nx;
            if (grid[nn] && nn != goal) continue;
            int ng = pg[cur] + 1;
            if (pstamp[nn] != stampv || ng < pg[nn]) {
                pstamp[nn] = stampv; pg[nn] = ng; pparent[nn] = cur;
                hpush(nn, ng + abs(gx - nx) + abs(gy - ny));
            }
        }
    }
    return expanded;
}
static void work_path(int searches)
{
    int tot = 0;
    for (int s = 0; s < searches; s++) {
        int sx = 4 + (s * 31) % 20, sy = 10 + (s * 7) % 20;
        int gx = GW - 6 - (s * 17) % 20, gy = 10 + (s * 11) % 30;
        if (grid[sy * GW + sx] || grid[gy * GW + gx]) continue;
        tot += astar(sx, sy, gx, gy);
    }
    sink_i = tot;
}

/* ---------- 4. Petit interpreteur de scripts (substitut de Lua) ---------- */
enum { OP_ADD, OP_SUB, OP_MUL, OP_LT, OP_JMPF, OP_JMP, OP_LOADK, OP_GETT, OP_SETT, OP_CALL, OP_END };
typedef struct { unsigned char op, a, b, c; } Ins;
static double regs[16]; static double tab[1024]; static double consts[8] = {1, 2, 0.5, 100, 3, 7, 0.25, 9};
static Ins prog[24] = {
    {OP_LOADK,0,0,0},{OP_LOADK,1,3,0},{OP_ADD,2,2,0},{OP_MUL,3,2,1},{OP_GETT,4,2,0},{OP_ADD,5,4,3},{OP_SETT,2,5,0},
    {OP_LT,6,2,1},{OP_SUB,7,5,4},{OP_CALL,8,5,7},{OP_ADD,9,9,6},{OP_LT,10,9,1},{OP_JMPF,10,3,0},{OP_ADD,11,11,0},
    {OP_GETT,12,11,0},{OP_MUL,13,12,2},{OP_SETT,11,13,0},{OP_ADD,14,14,0},{OP_LT,15,14,1},{OP_JMPF,15,2,0},{OP_JMP,0,0,0},{OP_END,0,0,0}};
static void work_script(int nops)
{
    int pc = 0, done = 0;
    while (done < nops) {
        const Ins *i = &prog[pc]; done++; pc++;
        switch (i->op) {
        case OP_ADD: regs[i->a] = regs[i->b] + regs[i->c] + 1; break;
        case OP_SUB: regs[i->a] = regs[i->b] - regs[i->c]; break;
        case OP_MUL: regs[i->a] = regs[i->b] * (regs[i->c] + 0.001); break;
        case OP_LT: regs[i->a] = regs[i->b] < regs[i->c]; break;
        case OP_JMPF: if (!regs[i->a]) pc += i->b; break;
        case OP_JMP: pc = 2; break;
        case OP_LOADK: regs[i->a] = consts[i->b & 7]; break;
        case OP_GETT: regs[i->a] = tab[((int)regs[i->b]) & 1023]; break;
        case OP_SETT: tab[((int)regs[i->a]) & 1023] = regs[i->b]; break;
        case OP_CALL: regs[i->a] = sqrt(fabs(regs[i->b]) + regs[i->c]); break;
        default: pc = 2; break;
        }
        if (pc >= 22) pc = 2;
        if (regs[3] > 1e12 || regs[3] != regs[3]) { regs[3] = 1; regs[2] = 0; }
    }
    sink_f = (float)regs[3];
}

/* ---------- Programme ---------- */
#define NW 4
static const char *NAME[NW] = {"Animation (personnages)", "Collisions (entites)", "Chemins A* (recherches)", "Scripts (instructions)"};
static const int BASE[NW] = {5, 100, 1, 20000};
static const int MULT[] = {1, 2, 4, 8, 16};
#define NM 5

int main(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 22, 30, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    SceCtrlData pad, old; memset(&old, 0, sizeof(old));
    phys_init();
    int mhz = 333, sel = 0, mi[NW] = {1, 1, 1, 1}, on[NW] = {1, 1, 1, 1};
    scePowerSetArmClockFrequency(333);
    double acc[NW] = {0}; int nacc = 0; float ms[NW] = {0};
    unsigned long long tlast = now_us(); int frame = 0;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons; old = pad;
        int reset = 0;
        if (pr & SCE_CTRL_DOWN) { sel = (sel + 1) % NW; }
        if (pr & SCE_CTRL_UP) { sel = (sel + NW - 1) % NW; }
        if (pr & SCE_CTRL_RIGHT) { if (mi[sel] < NM - 1) mi[sel]++; reset = 1; }
        if (pr & SCE_CTRL_LEFT) { if (mi[sel] > 0) mi[sel]--; reset = 1; }
        if (pr & SCE_CTRL_CROSS) { on[sel] = !on[sel]; reset = 1; }
        if (pr & SCE_CTRL_TRIANGLE) { mhz = (mhz == 333) ? 444 : 333; scePowerSetArmClockFrequency(mhz); reset = 1; }
        if (pr & SCE_CTRL_START) break;
        if (reset) { memset(acc, 0, sizeof(acc)); nacc = 0; memset(ms, 0, sizeof(ms)); tlast = now_us(); }
        if ((pr & SCE_CTRL_CIRCLE) && nacc == 0 && ms[0] + ms[1] + ms[2] + ms[3] > 0) {
            FILE *f = fopen("ux0:data/heist_cpu_test.txt", "a");
            if (f) {
                float tot = 0;
                for (int w = 0; w < NW; w++) { if (on[w]) tot += ms[w];
                    fprintf(f, "%s x%d (%d) %s : %.2f ms | ", NAME[w], MULT[mi[w]], BASE[w] * MULT[mi[w]], on[w] ? "actif" : "coupe", ms[w]); }
                fprintf(f, "cpu=%dMHz TOTAL=%.2f ms\n", mhz, tot); fclose(f);
            }
        }

        unsigned long long t0, t1;
        for (int w = 0; w < NW; w++) {
            if (!on[w]) continue;
            int q = BASE[w] * MULT[mi[w]];
            t0 = now_us();
            if (w == 0) work_anim(q, frame);
            else if (w == 1) work_phys(q);
            else if (w == 2) work_path(q);
            else work_script(q);
            t1 = now_us();
            acc[w] += (t1 - t0) / 1000.0;
        }
        nacc++; frame++;
        unsigned long long te = now_us();
        if (te - tlast >= 1000000ULL) {
            for (int w = 0; w < NW; w++) { ms[w] = (float)(acc[w] / nacc); acc[w] = 0; }
            nacc = 0; tlast = te;
        }

        vita2d_start_drawing(); vita2d_clear_screen();
        vita2d_pgf_draw_textf(pgf, 10, 24, RGBA8(255, 255, 255, 255), 1.1f, "Test CPU | processeur %d MHz | mesure moyenne par image", mhz);
        float tot = 0;
        for (int w = 0; w < NW; w++) {
            unsigned int col = (w == sel) ? RGBA8(255, 255, 0, 255) : RGBA8(200, 200, 200, 255);
            vita2d_pgf_draw_textf(pgf, 10, 70 + w * 34, col, 1.1f, "%s %s  x%d  (%d)   %.2f ms",
                                  w == sel ? ">" : " ", NAME[w], MULT[mi[w]], BASE[w] * MULT[mi[w]], on[w] ? ms[w] : 0.0f);
            if (!on[w]) vita2d_pgf_draw_text(pgf, 700, 70 + w * 34, RGBA8(160, 90, 90, 255), 1.0f, "(coupe)");
            else tot += ms[w];
        }
        vita2d_pgf_draw_textf(pgf, 10, 230, RGBA8(120, 255, 120, 255), 1.3f, "TOTAL %.2f ms  (60 fps = 16.67 ms, 30 fps = 33.33 ms)", tot);
        vita2d_pgf_draw_textf(pgf, 10, 262, RGBA8(255, 255, 255, 255), 1.0f, "Reste pour le dessin et le reste du jeu a 60 fps : %.1f ms", 16.67f - tot);
        vita2d_pgf_draw_text(pgf, 10, 510, RGBA8(200, 200, 200, 255), 0.9f,
            "Haut/Bas: choisir  Gauche/Droite: quantite  Croix: couper/activer  Triangle: 333/444 MHz  Rond: noter  Start: quitter");
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
    }
    vita2d_free_pgf(pgf); vita2d_fini(); sceKernelExitProcess(0);
    return 0;
}
