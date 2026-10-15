/*
 * groucho-time: a screensaver clock cut out of old pictures, as Terry Gilliam cut
 * his: Father Time sitting on his clock, from a nineteenth-century design, with
 * Groucho Marx's head (1931) pasted on. The hands go round the engraving's own dial;
 * Groucho's eyes go to and fro with the seconds behind the holes in his head; his
 * eyebrows waggle every minute; and every quarter of an hour the top of his head
 * flips open and one of his lines comes up on a spring. Now and then Gummo, cut from
 * a 1913 newspaper, peeks out from behind the clock.
 *
 *   groucho-time            full screen on every display; any key or mouse movement quits
 *   groucho-time --window   in a window; Esc or Q quits
 *   GrouchoTime.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (public-domain prints and photographs, cut by tools/make_cutouts.py) are built into the
 * program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__APPLE__)
/* Mach-O: its own section names, and C names carry a leading underscore */
#define ART_SECTION ".section __DATA,__const\n"
#define ART_SYM "_art_"
#elif defined(_WIN32)
#define ART_SECTION ".section .rdata,\"dr\"\n"
#define ART_SYM "art_"
#else
#define ART_SECTION ".section .rodata\n"
#define ART_SYM "art_"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".globl " ART_SYM #name "\n" ART_SYM #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".globl " ART_SYM #name "_end\n" ART_SYM #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
/* Every picture, by name and file (src/art.h). The figures are drawn to one scale:
 * Groucho stands 480 high, and the others at their heights beside him, each with
 * its feet at the middle of the bottom edge. */
#include "art.h"
#include "cutouts.h"
#define HEAD_S 0.44   /* Groucho's head on the monument, as part of the photograph's size */
#define X(name, file) ART(name, file)
SPRITES
#undef X
#define X(name, file) SP_##name,
enum { SPRITES NSPRITE };
#undef X
#define X(name, file) {art_##name, art_##name##_end},
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = { SPRITES };
#undef X
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {10, 9, 12}, INK = {30, 24, 20}, PAPER = {236, 226, 200};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c); vert(x1, y1, c); vert(x2, y2, c); vert(x3, y3, c);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}

/* ---- randomness and timing helpers ---- */
static uint64_t seed_state;
static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }


/* ---- the time ----
 *
 * The clock shows the time of day where the computer is: read at the start, and then
 * kept by the frame clock so that it moves smoothly. --clock HH:MM[:SS] starts it at
 * another time instead, to see the hour come round. */
static double opt_clock = -1;          /* seconds after midnight to start at, or -1: the real time */
static double tod0, anim_t;            /* the time of day at the start, and the seconds since */
static double T;                       /* the time of day now, in seconds after midnight */

static void read_time(void)
{
    if (opt_clock >= 0) { tod0 = opt_clock; return; }
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    tod0 = tm ? tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec : 0;
}
static void update(double dt)
{
    anim_t += dt * opt_speed;
    T = fmod(tod0 + anim_t, 86400);
    /* the screensaver may run for hours: read the real clock again now and then,
     * so that it never drifts */
    static double since;
    since += dt;
    if (opt_clock < 0 && since > 60 && !opt_snapshot) { since = 0; read_time(); anim_t = 0; }
}
static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    read_time();
    T = tod0;
}
/* a number from 0 to 1 that is the same for the same n, and looks random */
static double hash01(uint64_t n)
{
    n = (n + 0x9E3779B97F4A7C15ULL) * 0xBF58476D1CE4E5B9ULL;
    n ^= n >> 31; n *= 0x94D049BB133111EBULL; n ^= n >> 29;
    return (n >> 11) * (1.0 / 9007199254740992.0);
}

/* ---- what he says: his own lines, every quarter of an hour, with the film each is
 * from (as Wikiquote gives them) ---- */
typedef struct { const char *say, *film; } Saying;
#define AC "ANIMAL CRACKERS, 1930"
#define CN "THE COCOANUTS, 1929"
#define DS "DUCK SOUP, 1933"
#define HF "HORSE FEATHERS, 1932"
#define MB "MONKEY BUSINESS, 1931"
#define NO "A NIGHT AT THE OPERA, 1935"
#define DR "A DAY AT THE RACES, 1937"
static const Saying SAYINGS[] = {
    {"ONE MORNING I SHOT AN ELEPHANT IN MY PAJAMAS. HOW HE GOT IN MY PAJAMAS, I DON'T KNOW.", AC},
    {"WHY, YOU'RE ONE OF THE MOST BEAUTIFUL WOMEN I'VE EVER SEEN, AND THAT'S NOT SAYING MUCH FOR YOU.", AC},
    {"AFRICA IS GOD'S COUNTRY, AND HE CAN HAVE IT.", AC},
    {"HELLO, I MUST BE GOING.", AC},
    {"THE PRINCIPAL ANIMALS INHABITING THE AFRICAN JUNGLE ARE MOOSE, ELKS AND KNIGHTS OF PYTHIAS.", AC},
    {"YOU TAKE CARE OF YOUR ANIMALS AND I'LL TAKE CARE OF MINE!", AC},
    {"WHEN YOU GET NEAR A SONG, PLAY IT.", AC},
    {"HOW MUCH WOULD YOU WANT TO RUN INTO AN OPEN MANHOLE?", AC},
    {"PARDON ME WHILE I HAVE A STRANGE INTERLUDE.", AC},
    {"THREE YEARS AGO I CAME TO FLORIDA WITHOUT A NICKEL IN MY POCKET. NOW I'VE GOT A NICKEL IN MY POCKET.", CN},
    {"NOW THIS IS OUR CEMETERY. I'VE GOT A LIST OF A HUNDRED PEOPLE JUST DYING TO GET IN.", CN},
    {"I'M GOING TO GET YOU A STEADY POSITION. AND IF I CAN ARRANGE IT, IT'LL BE HORIZONTAL.", CN},
    {"ICE WATER IN ROOM 318? NO KIDDING, WHERE'D YOU GET IT?", CN},
    {"DON'T PICK THEIR POCKETS, JUST MINGLE WITH THEM.", CN},
    {"OH, I SEE! YOU'RE JUST AN IDLE ROOMER!", CN},
    {"WELL, I DON'T KNOW WHY A NO CHICKEN. I'M A STRANGER HERE MYSELF.", CN},
    {"A YES LIKE THAT WAS ONCE RESPONSIBLE FOR ME JUMPING OUT OF A WINDOW.", CN},
    {"I COULD DANCE WITH YOU TILL THE COWS COME HOME. ON SECOND THOUGHT, I'D RATHER DANCE WITH THE COWS TILL YOU CAME HOME.", DS},
    {"DON'T LOOK NOW, BUT THERE'S ONE MAN TOO MANY IN THIS ROOM, AND I THINK IT'S YOU.", DS},
    {"I'LL SEE MY LAWYER ABOUT THIS AS SOON AS HE GRADUATES FROM LAW SCHOOL.", DS},
    {"CHICOLINI HERE MAY TALK LIKE AN IDIOT, AND LOOK LIKE AN IDIOT. BUT DON'T LET THAT FOOL YOU. HE REALLY IS AN IDIOT.", DS},
    {"REMEMBER, YOU'RE FIGHTING FOR THIS WOMAN'S HONOR, WHICH IS PROBABLY MORE THAN SHE EVER DID.", DS},
    {"YOU MUST HAVE BEEN VACCINATED WITH A PHONOGRAPH NEEDLE.", DS},
    {"GET ME HEADQUARTERS. NOT HINDQUARTERS, HEADQUARTERS!", DS},
    {"LET ME OUT OF HERE, OR THROW ME A MAGAZINE!", DS},
    {"YOU'VE GOT THE BRAIN OF A FOUR-YEAR-OLD BOY, AND I BET HE WAS GLAD TO GET RID OF IT.", HF},
    {"WELL, I THOUGHT MY RAZOR WAS DULL UNTIL I HEARD THIS SPEECH.", HF},
    {"I'D HORSEWHIP YOU, IF I HAD A HORSE.", HF},
    {"I MARRIED YOUR MOTHER BECAUSE I WANTED CHILDREN. IMAGINE MY DISAPPOINTMENT WHEN YOU ARRIVED.", HF},
    {"OH, I LOVE SITTING ON YOUR LAP. I COULD SIT HERE ALL DAY IF YOU DIDN'T STAND UP.", HF},
    {"WHY DON'T YOU BORE A HOLE IN YOURSELF AND LET THE SAP RUN OUT?", HF},
    {"IS THIS STUFF ON THE LEVEL OR ARE YOU JUST MAKING IT UP AS YOU GO ALONG?", HF},
    {"I'M ALWAYS THAT WAY AFTER I EAT RADISHES.", HF},
    {"SIR, ARE YOU TRYING TO OFFER ME A BRIBE? HOW MUCH?", MB},
    {"WITH A LITTLE STUDY YOU'LL GO A LONG WAY. AND I WISH YOU'D START NOW.", MB},
    {"MADAM, I RESENT THAT. SOME OF MY BEST FRIENDS ARE HOUSEWIVES!", MB},
    {"ALL RIGHT, GIVE ME THE DOLLAR, I'LL KEEP THE CHANGE.", MB},
    {"AFRAID? ME? A MAN WHO'S LICKED HIS WEIGHT IN WILD CATERPILLARS? AFRAID? YOU BET I'M AFRAID!", MB},
    {"LADIES AND GENTLEMEN. I GUESS THAT TAKES IN MOST OF YOU.", NO},
    {"EVERYTHING ABOUT YOU REMINDS ME OF YOU. EXCEPT YOU.", NO},
    {"YES. JUST ME AND MY MEMORIES. I'M PRACTICALLY A HERMIT.", NO},
    {"THEY'RE FAMOUS AS AVIATORS, BUT YOU NOTICE THEY'RE TRAVELING BY BOAT.", NO},
    {"CAN YOU SLEEP ON YOUR STOMACH WITH SUCH BIG BUTTONS ON YOUR PAJAMAS?", NO},
    {"WELL, I HAVEN'T SAID ANYTHING WORTH HEARING.", NO},
    {"EITHER HE'S DEAD OR MY WATCH HAS STOPPED!", DR},
    {"TAKE ONE OF THOSE EVERY HALF MILE AND CALL ME IF THERE'S ANY CHANGE.", DR},
    {"DON'T DRINK THAT POISON! THAT'S FOUR DOLLARS AN OUNCE.", DR},
    {"IF I HOLD YOU ANY CLOSER, I'LL BE IN BACK OF YA!", DR},
    {"THE LAST PATIENT I GAVE ONE OF THOSE TO WON THE KENTUCKY DERBY.", DR},
    {"ALL I WANTED WAS A HORSE, NOT A PUBLIC LIBRARY.", DR},
    {"IT'S THE OLD, OLD STORY. BOY MEETS GIRL - ROMEO AND JULIET - MINNEAPOLIS AND ST. PAUL!", DR},
};
#define NSAYING (int)(sizeof SAYINGS / sizeof *SAYINGS)
#define QUARTER 900.0
#define CARD_LEN 10.0                  /* how long a saying stays up */
/* the saying for a quarter of an hour: a different order each day */
static const Saying *saying_for(double t)
{
    uint64_t day = (uint64_t)(time(NULL) / 86400), q = (uint64_t)(t / QUARTER);
    return &SAYINGS[(int)(hash01(day * 131 + q) * NSAYING) % NSAYING];
}
#define HOUR_SHOW 12.0                 /* how long the hour's business takes */

/* ---- Gummo ----
 * In about one minute in four he peeks in from the left for a few seconds: in, a
 * look, a raised hat, a wink or a finger to his lips, and gone. */
#define GUMMO_LEN 4.6
/* who it is this time: Gummo from behind the wing, or Chico or Zeppo from the left */
enum { PEEK_GUMMO, PEEK_CHICO, PEEK_ZEPPO };
static int peeker(double t) { return (int)(hash01((uint64_t)floor(t / 60) * 7 + 9) * 3) % 3; }
static int gummo_at(double t, double *q, int *frame)
{
    double m = floor(t / 60), s = t - m * 60;
    if (hash01((uint64_t)m * 7 + 3) > 0.40) return 0;
    double s0 = 18 + hash01((uint64_t)m * 7 + 5) * 30;
    if (s < s0 || s > s0 + GUMMO_LEN) return 0;
    double u = s - s0;
    *q = ease(ramp(u, 0, 0.45)) * (1 - ease(ramp(u, GUMMO_LEN - 0.45, GUMMO_LEN)));
    *frame = u < 1.5 ? 0 : 1 + (int)(hash01((uint64_t)m * 7 + 6) * 3);
    return 1;
}


/* ---- drawing ---- */
static double unit;
static int sw, sh;

static void old_film(void)
{
    double f = 0.03 + 0.02 * sin(anim_t * 37) * sin(anim_t * 13);
    rect(0, 0, sw, sh, col(BG, f));
    uint64_t keep = seed_state;
    seed_state = 1 + (uint64_t)(anim_t * 24);
    for (int i = 0; i < 3; i++) {                  /* a scratch or two */
        if (frand() < 0.5) continue;
        double x = frand() * sw;
        line(x, 0, x + (frand() - 0.5) * unit, sh, unit * 0.08, col(PAPER, 0.12));
    }
    for (int i = 0; i < 40; i++) {                 /* dust */
        double x = frand() * sw, y = frand() * sh, r = unit * (0.05 + frand() * 0.12);
        disc(x, y, r, col(frand() < 0.5 ? INK : PAPER, 0.25), col(INK, 0));
    }
    seed_state = keep;
    flush(ren);
}

/* ---- the cut-out ----
 *
 * Everything moves as Terry Gilliam's cut-outs do: on twelve frames a second, in
 * jerks, the pieces turning on their pins. The monument is drawn at MON_S of its
 * picture's size; Groucho's head at HEAD_S of the photograph's. */
static double ms, mx0, my0;             /* the monument's scale and where its top left is */
static double hs;                       /* the head's scale on the screen */
static double jerky(double t) { return floor(t * 12) / 12; }

/* a picture drawn with the point (ax, ay) of it at (x, y), scaled and turned about that point */
static void put(int which, double x, double y, double ax, double ay, double scale, double angle, double alpha, int flip)
{
    flush(ren);
    if (which < 0 || !cur_tex || !cur_tex[which] || alpha <= 0.01) return;
    SDL_FRect d = {(float)(x - ax * scale), (float)(y - ay * scale), (float)(sprite[which].w * scale), (float)(sprite[which].h * scale)};
    SDL_FPoint c = {(float)(ax * scale), (float)(ay * scale)};
    SDL_SetTextureAlphaMod(cur_tex[which], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &d, angle, &c, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(cur_tex[which], 255);
}
/* a point of the monument picture, and of the photograph (by where his chin goes), on the screen */
static void on_mon(double px, double py, double *x, double *y) { *x = mx0 + px * ms; *y = my0 + py * ms; }
static double head_rot, head_dx, head_dy;   /* the whole head turned about his chin, and moved */
static void on_photo(double px, double py, double *x, double *y)
{
    double cx, cy, a = head_rot * M_PI / 180;
    on_mon(CHIN_X, CHIN_Y, &cx, &cy);
    double dx = (px - PHOTO_CHIN_X) * hs, dy = (py - PHOTO_CHIN_Y) * hs;
    *x = cx + dx * cos(a) - dy * sin(a) + head_dx; *y = cy + dx * sin(a) + dy * cos(a) + head_dy;
}

/* ---- the business in between ----
 * About every minute and a half, away from the quarter hours, someone does something
 * for a moment: his head spins round and comes up as Zeppo's, and back; Margaret
 * Dumont sweeps in and he makes eyes at her; Raquel Torres glides by and his head
 * floats off after her; the top of his head opens and Chico pops out on a spring; or
 * Harpo's head goes up out of the sphinx on a spring. */
enum { EV_ZEPPO, EV_DUMONT, EV_TORRES, EV_CHICO, EV_HARPO, NEV };
static const double EV_LEN[NEV] = {2.6, 4.2, 4.6, 2.8, 2.4};
#define EV_EVERY 90.0
static int event_at(double t, double *u)
{
    double slot = floor(t / EV_EVERY), s = t - slot * EV_EVERY;
    int kind = (int)(hash01((uint64_t)slot * 13 + 1) * NEV) % NEV;
    double s0 = 20 + hash01((uint64_t)slot * 13 + 2) * 50;
    if (s < s0 || s > s0 + EV_LEN[kind]) return -1;
    if (fmod(t, QUARTER) < CARD_LEN + 4) return -1;          /* not while he's saying something */
    *u = (s - s0) / EV_LEN[kind];
    return kind;
}

/* a clock hand as an engraver draws one: a long tapering spade */
static void hand(double cx, double cy, double turn, double len, double w)
{
    double a = turn * 2 * M_PI, dx = sin(a), dy = -cos(a), nx = -dy, ny = dx;
    SDL_Color ink = col((Rgb){22, 18, 16}, 1);
    double tail = len * 0.18, waist = len * 0.62, tipx = cx + dx * len, tipy = cy + dy * len;
    vb_need(6, 12);
    int k = nv;
    vert(cx - dx * tail, cy - dy * tail, ink);
    vert(cx + nx * w, cy + ny * w, ink);
    vert(cx + dx * waist + nx * w * 1.8, cy + dy * waist + ny * w * 1.8, ink);
    vert(tipx, tipy, ink);
    vert(cx + dx * waist - nx * w * 1.8, cy + dy * waist - ny * w * 1.8, ink);
    vert(cx - nx * w, cy - ny * w, ink);
    int t[] = {0, 1, 5, 1, 2, 4, 1, 4, 5, 2, 3, 4};
    for (int i = 0; i < 12; i++) ib[ni++] = k + t[i];
    disc(cx + dx * waist, cy + dy * waist, w * 1.1, col(PAPER, 1), col(PAPER, 1));   /* the open spade */
    disc(cx, cy, w * 1.6, ink, ink);
}

/* the saying, on a printed label held up from inside his head on a coiled spring */
static void saying_label(const Saying *sy, double x, double y, double up)
{
    const int WRAP = 22;
    char lines[7][64]; int n = 0, len = 0, most = 0;
    const char *p = sy->say; lines[0][0] = 0;
    while (*p && n < 7) {
        const char *e = p; while (*e && *e != ' ') e++;
        int wl = (int)(e - p);
        if (len && len + 1 + wl > WRAP) { n++; len = 0; if (n >= 7) break; lines[n][0] = 0; }
        if (len) lines[n][len++] = ' ';
        memcpy(lines[n] + len, p, wl); len += wl; lines[n][len] = 0;
        if (len > most) most = len;
        p = *e ? e + 1 : e;
    }
    n++;
    /* sized to the screen, and the words sized to fit inside the printer's rule */
    double w = fmin(sw * 0.40, sh * 0.70), pad = unit * 2.6;
    double s = (w - 2 * pad) / text_width(lines[0][0] ? "MMMMMMMMMMMMMMMMMMMMMM" : "M", 1.0) * (22.0 / fmax(most, 12));
    s = fmin(s, (w - 2 * pad) / (most * 6.2));
    double lh = s * 9.5, fsz = fmin(fmax(s * 0.55, unit * 0.45), (w - 2 * pad) / text_width(sy->film, 1.0)), h = 2 * pad + lh * (n - 1) + s * 6 + fsz * 9;
    double rest = fmax(unit * 2, y - h - sh * 0.10);        /* where it comes to rest: never off the top */
    double top = y + (rest - y) * up - h * fmin(up * 3, 1) * (1 - up), cx = fmin(fmax(x, w / 2 + unit * 2), sw - w / 2 - unit * 2);
    SDL_Color ink = col((Rgb){26, 20, 18}, 1), red = col((Rgb){168, 34, 30}, 1);
    for (int i = 0; i < 9; i++) {                          /* the spring */
        double ya = y + (top + h - y) * i / 9.0, yb = y + (top + h - y) * (i + 1) / 9.0;
        line(x + (i & 1 ? -1 : 1) * unit * 1.4, ya, x + (i & 1 ? 1 : -1) * unit * 1.4, yb, unit * 0.35, col((Rgb){90, 84, 78}, 1));
    }
    rect(cx - w / 2 + unit * 0.7, top + unit * 0.9, w, h, col(INK, 0.45));
    rect(cx - w / 2, top, w, h, col((Rgb){240, 230, 204}, 1));
    for (int i = 0; i < 2; i++) {                          /* a printer's double rule */
        double m = unit * (0.8 + i * 0.9);
        double x0 = cx - w / 2 + m, x1 = cx + w / 2 - m, y0 = top + m, y1 = top + h - m;
        double lw = unit * (i ? 0.18 : 0.45);
        line(x0, y0, x1, y0, lw, red); line(x1, y0, x1, y1, lw, red); line(x1, y1, x0, y1, lw, red); line(x0, y1, x0, y0, lw, red);
    }
    for (int i = 0; i < n; i++) text(lines[i], cx, top + pad + i * lh, s, 1.1, ink);
    text(sy->film, cx, top + pad + (n - 1) * lh + s * 6 + fsz * 2.5, fsz, 1.0, red);
    flush(ren);
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    double jt = jerky(anim_t);
    /* a flat painted ground, as Gilliam's were, darker at the edges */
    SDL_SetRenderDrawColor(r, 34, 58, 70, 255);
    SDL_RenderClear(r);
    {
        SDL_Color top = col((Rgb){58, 92, 104}, 1), bottom = col((Rgb){30, 48, 58}, 1);
        vb_need(4, 6);
        int k = nv;
        vert(0, 0, top); vert(sw, 0, top); vert(sw, sh, bottom); vert(0, sh, bottom);
        ib[ni++] = k; ib[ni++] = k + 1; ib[ni++] = k + 2; ib[ni++] = k; ib[ni++] = k + 2; ib[ni++] = k + 3;
    }
    flush(ren);

    double hp = fmod(T, 3600), mp = fmod(T, 60), qp = fmod(T, QUARTER);
    double eu = 0;
    int ev = event_at(T, &eu);
    double ju = jerky(eu * EV_LEN[ev < 0 ? 0 : ev]) / EV_LEN[ev < 0 ? 0 : ev];
    head_rot = head_dx = head_dy = 0;
    int zeppo_face = 0;
    if (ev == EV_ZEPPO) {                                  /* round once, Zeppo; round again, Groucho */
        if (ju < 0.3) head_rot = 360 * ease(ju / 0.3);
        else if (ju < 0.7) zeppo_face = 1;
        else head_rot = 360 * ease((ju - 0.7) / 0.3);
        zeppo_face |= (ju > 0.15 && ju < 0.3) || (ju >= 0.7 && ju < 0.85);
    }
    double smitten = 0;
    if (ev == EV_TORRES) {                                 /* his head floats up after her */
        smitten = ramp(ju, 0.25, 0.4) * (1 - ramp(ju, 0.78, 0.92));
        head_dy = -sh * 0.10 * smitten - sh * 0.02 * sin(ju * 30) * smitten;
        head_dx = -sw * 0.05 * smitten;
        head_rot = -12 * smitten;
    }
    double gq; int gframe;
    int gummo = gummo_at(T, &gq, &gframe);
    if (ev >= 0) gummo = 0;                                 /* one thing at a time */

    /* the monument: Father Time and his clock fill the screen, and the plinth goes
     * on down out of sight */
    ms = sh * 0.80 / (MONUMENT_H * 0.52);
    mx0 = sw / 2.0 - MONUMENT_W * ms / 2;
    my0 = sh * 0.14;
    hs = ms * HEAD_S;

    /* now and then a brother peeks in, jerkily: Gummo from behind the sphinx, his
     * body running off the bottom; Chico or Zeppo from behind Father Time's left */
    int who = peeker(T);
    if (gummo && who == PEEK_GUMMO) {
        double q = jerky(gq * 4) / 4, gs = sh * 0.62 / sprite[SP_gummo].h;
        double px, py;
        on_mon(MONUMENT_W * 0.66, MONUMENT_H * 0.30, &px, &py);
        double x = px + q * sprite[SP_gummo].w * gs * 0.62;
        put(SP_gummo, x, sh - sprite[SP_gummo].h * gs * 0.80, 0, 0, gs, (gframe ? 5 : -2) * q, 1, 0);
    } else if (gummo) {
        int b = who == PEEK_CHICO ? SP_chico : SP_zeppo;
        double q = jerky(gq * 4) / 4, bs = sh * 0.30 / sprite[b].h;
        double px, py;
        on_mon(MONUMENT_W * 0.30, MONUMENT_H * 0.36, &px, &py);
        /* leaning out from behind him, turning on his chin */
        put(b, px - q * sprite[b].w * bs * 0.50, py, sprite[b].w / 2.0, sprite[b].h, bs, -q * 24 + (gframe ? -6 : 3) * q, 1, 0);
    }
    put(SP_monument, mx0, my0, 0, 0, ms, 0, 1, 0);

    /* the time, on Father Time's own dial */
    double dx, dy, dr = DIAL_R * ms;
    on_mon(DIAL_X, DIAL_Y, &dx, &dy);
    double h12 = fmod(T / 3600, 12);
    hand(dx, dy, h12 / 12, dr * 0.95, dr * 0.07);
    hand(dx, dy, hp / 3600, dr * 1.42, dr * 0.05);
    flush(ren);

    /* Harpo, as the sphinx: silent, and now and then his head goes over to one side */
    {
        double hx, hy, tt = fmod(T, 23.0), tilt = 0;
        if (tt > 17) tilt = sin(M_PI * jerky(tt - 17) / 6) * 14;
        if (gummo) tilt = -10 * gq;                          /* and towards whoever has come */
        on_mon(SPHINX_CHIN_X, SPHINX_CHIN_Y, &hx, &hy);
        if (ev == EV_HARPO) {                                /* up on a spring, and down again */
            double lift = ju < 0.3 ? ju / 0.3 * 1.15 : ju < 0.8 ? 1 + 0.15 * cos((ju - 0.3) * 30) * exp(-(ju - 0.3) * 6) : (1 - ju) / 0.2;
            double top = hy - sh * 0.22 * lift;
            for (int i = 0; i < 10; i++) {
                double ya = hy + (top - hy) * i / 10.0, yb = hy + (top - hy) * (i + 1) / 10.0;
                line(hx + (i & 1 ? -1 : 1) * unit, ya, hx + (i & 1 ? 1 : -1) * unit, yb, unit * 0.3, col((Rgb){90, 84, 78}, 1));
            }
            flush(ren);
            hy = top; tilt = sin(ju * 25) * 12;
        }
        put(SP_harpo, hx, hy, HARPO_CHIN_X, HARPO_CHIN_Y, ms * 0.56, tilt, 1, 0);
    }

    /* his eyes, behind the holes in his head: paper whites, and the irises going to
     * and fro with the seconds like a cat clock's */
    double sec = fmod(T, 1.0), snap = 1 - pow(1 - fmin(sec / 0.18, 1), 3);
    int odd = (long)floor(T) & 1;
    double look = (odd ? 1 : -1) * (2 * snap - 1);
    if (gummo) look = look * (1 - gq) + gq;                 /* and they go to Gummo when he's there */
    for (int e = 0; e < 2; e++) {
        double ex = e ? EYE_R_X : EYE_L_X, ey = e ? EYE_R_Y : EYE_L_Y, erx = e ? EYE_R_RX : EYE_L_RX;
        double ery = e ? EYE_R_RY : EYE_L_RY, x, y;
        on_photo(ex, ey, &x, &y);
        disc(x, y, erx * hs * 1.15, col((Rgb){236, 228, 210}, 1), col((Rgb){236, 228, 210}, 1));
        flush(ren);
        int ir = e ? SP_iris_r : SP_iris_l;
        double ox = look * erx * 0.55, oy = 0;
        if (ev == EV_DUMONT && ju > 0.2 && ju < 0.85) ox = erx * 0.6;        /* at her */
        if (ev == EV_TORRES && ju > 0.1 && ju < 0.9) ox = -erx * 0.6;
        (void)ery;
        double ra = head_rot * M_PI / 180;
        put(ir, x + (ox * cos(ra) - oy * sin(ra)) * hs, y + (ox * sin(ra) + oy * cos(ra)) * hs,
            sprite[ir].w / 2.0, sprite[ir].h / 2.0, hs * 1.05, head_rot, 1, 0);
    }

    /* the head */
    double x, y;
    on_photo(HEAD_X, HEAD_Y, &x, &y);
    if (zeppo_face) {                                      /* Zeppo, where Groucho was */
        double zx, zy, zs = hs * sprite[SP_head].w / sprite[SP_zeppo].w * 0.9;
        on_photo(PHOTO_CHIN_X, PHOTO_CHIN_Y - 40, &zx, &zy);
        put(SP_zeppo, zx, zy, ZEPPO_CHIN_X, ZEPPO_CHIN_Y, zs, head_rot, 1, 0);
    } else
        put(SP_head, x, y, 0, 0, hs, head_rot, 1, 0);

    /* the eyebrows: up and down as each minute begins, and up for Gummo */
    double wag = mp < 1.5 ? fabs(sin(jerky(mp) * M_PI * 3)) * (1 - mp / 1.5) : 0;
    if (ev == EV_DUMONT && ju > 0.25 && ju < 0.85) wag = fabs(sin(jerky(ju * 4.2) * M_PI * 2.5));
    if (smitten > 0) wag = fmax(wag, smitten * fabs(sin(jerky(anim_t) * 9)));
    double lift = wag * 22 + (gummo ? 14 * gq : 0);
    if (!zeppo_face) {
    on_photo(BROW_L_X, BROW_L_Y - lift, &x, &y);
    put(SP_brow_l, x, y, 0, 0, hs, head_rot - wag * 6, 1, 0);
    on_photo(BROW_R_X, BROW_R_Y - lift, &x, &y);
    put(SP_brow_r, x, y, 0, 0, hs, head_rot + wag * 6, 1, 0);
    }

    /* every quarter of an hour the top of his head flips open on its hinge and up
     * comes a saying, on a spring */
    double open = 0, up = 0;
    if (qp < CARD_LEN + 2.4) {
        open = ramp(qp, 0.2, 0.7) * (1 - ramp(qp, CARD_LEN + 1.6, CARD_LEN + 2.2));
        up = ramp(qp, 0.7, 1.3) * (1 - ramp(qp, CARD_LEN + 0.6, CARD_LEN + 1.4));
        open = jerky(open * 3) / 3; up = jerky(up * 4) / 4;
        up *= 1 + 0.15 * sin(fmin(qp - 1.3, 2) * 9) * exp(-fmax(qp - 1.3, 0) * 2.5) * (qp > 1.3);
    }
    double chico_up = 0;
    if (ev == EV_CHICO) {                                  /* the lid opens and up pops Chico */
        open = ramp(ju, 0, 0.15) * (1 - ramp(ju, 0.85, 1));
        chico_up = ramp(ju, 0.15, 0.3) * (1 - ramp(ju, 0.7, 0.85));
        chico_up *= 1 + 0.12 * sin((ju - 0.3) * 40) * exp(-fmax(ju - 0.3, 0) * 8) * (ju > 0.3);
    }
    if (open > 0) {                                        /* the inside of his head: a dark slot along the cut */
        double ax, ay, bx, by;
        on_photo(FLAP_X + 18, FLAP_Y + 214, &ax, &ay);
        on_photo(FLAP_X + 474, FLAP_Y + 198, &bx, &by);
        double cx = (ax + bx) / 2, cy = (ay + by) / 2, rx = (bx - ax) / 2, ry = rx * 0.16 * fmin(open * 2, 1);
        int n = 40;
        vb_need(n + 1, 3 * n);
        int k = nv;
        SDL_Color dark = col((Rgb){26, 14, 12}, 1);
        vert(cx, cy, dark);
        for (int i = 0; i < n; i++) vert(cx + rx * cos(i * 2 * M_PI / n), cy + ry * sin(i * 2 * M_PI / n) + (by - ay) * cos(i * 2 * M_PI / n) / 2, dark);
        for (int i = 0; i < n; i++) { ib[ni++] = k; ib[ni++] = k + 1 + i; ib[ni++] = k + 1 + (i + 1) % n; }
        flush(ren);
    }
    if (chico_up > 0) {
        double cx, cy, top, cs = hs * sprite[SP_head].w / sprite[SP_chico].w * 0.75;
        on_photo(430, 205, &cx, &cy);
        top = cy - sh * 0.09 * chico_up;
        for (int i = 0; i < 8; i++) {
            double ya = cy + (top - cy) * i / 8.0, yb = cy + (top - cy) * (i + 1) / 8.0;
            line(cx + (i & 1 ? -1 : 1) * unit * 1.2, ya, cx + (i & 1 ? 1 : -1) * unit * 1.2, yb, unit * 0.32, col((Rgb){90, 84, 78}, 1));
        }
        flush(ren);
        put(SP_chico, cx, top, CHICO_CHIN_X, CHICO_CHIN_Y, cs, sin(ju * 30) * 10, 1, 0);
    }
    on_photo(HINGE_X, HINGE_Y, &x, &y);
    if (!zeppo_face) put(SP_flap, x, y, HINGE_X - FLAP_X, HINGE_Y - FLAP_Y, hs, head_rot - open * 115, 1, 0);

    /* the ladies, sweeping in and out in front of everything, jerkily */
    if (ev == EV_DUMONT) {
        double q = jerky(ramp(ju, 0, 0.2) * 4) / 4 * (1 - jerky(ramp(ju, 0.85, 1) * 4) / 4);
        double ds = sh * 0.80 / sprite[SP_dumont].h, wdt = sprite[SP_dumont].w * ds;
        put(SP_dumont, sw - wdt * 0.92 * q, sh - sprite[SP_dumont].h * ds * 0.95, 0, 0, ds, -4 * q + 3 * sin(ju * 12) * q, 1, 0);
    }
    if (ev == EV_TORRES) {
        double q = jerky(ju * EV_LEN[EV_TORRES] * 3) / (EV_LEN[EV_TORRES] * 3);
        double ts = sh * 0.72 / sprite[SP_torres].h, wdt = sprite[SP_torres].w * ts;
        double x0 = -wdt, x1 = sw * 0.42;
        double px = q < 0.45 ? x0 + (x1 - x0) * ease(q / 0.45) : q < 0.6 ? x1 : x1 - (x1 - x0) * ease((q - 0.6) / 0.4);
        put(SP_torres, px, sh - sprite[SP_torres].h * ts * 0.95, 0, 0, ts, 2 * sin(q * 20), 1, 0);
        if (q > 0.35 && q < 0.75)                          /* a heart or two, cut from red paper */
            for (int k = 0; k < 3; k++) {
                double hx, hy, hq = fmod(q * 3 + k * 0.33, 1);
                on_photo(450 + k * 60, 120, &hx, &hy);
                hy -= hq * sh * 0.15;
                double rr = unit * 1.6;
                SDL_Color red = col((Rgb){190, 30, 40}, 1 - hq);
                disc(hx - rr * 0.5, hy, rr * 0.62, red, red); disc(hx + rr * 0.5, hy, rr * 0.62, red, red);
                vb_need(3, 3); int kk = nv;
                vert(hx - rr * 1.08, hy + rr * 0.2, red); vert(hx + rr * 1.08, hy + rr * 0.2, red); vert(hx, hy + rr * 1.5, red);
                ib[ni++] = kk; ib[ni++] = kk + 1; ib[ni++] = kk + 2;
                flush(ren);
            }
    }
    if (up > 0) {
        double cx, cy;
        on_photo(430, 205, &cx, &cy);
        saying_label(saying_for(T), cx, cy, up);
    }
    (void)jt;
    old_film();
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Groucho Time", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}


static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "groucho-time: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "groucho-time: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Groucho Time",
        "Groucho Time screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --clock HH:MM   start at that time of day instead of the real time\n"
        "  --speed X       how fast time goes, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Groucho Time", "Groucho Time screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--clock") && i + 1 < argc) {
            int hh = 0, mm = 0, ss = 0;
            if (sscanf(argv[++i], "%d:%d:%d", &hh, &mm, &ss) < 2) { usage(); return 1; }
            opt_clock = hh * 3600 + mm * 60 + ss;
        }
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "groucho-time: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "groucho-time: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}

