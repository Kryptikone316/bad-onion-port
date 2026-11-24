/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Video, input and timing for fxi on the SF3500, behind the SDL 1.2 look-alike in SDL.h.
 *
 *   video   fxi draws into an 8 or 16 bit surface that lives here; "flipping" converts it to
 *           RGB565 (8-bit frames through the palette) and hands it to the display driver.
 *   input   the gamepad is read from the front-end's shared memory and turned into SDL 1.2 key
 *           state and key events. Select + Start quits.
 *   timing  SDL_GetTicks / SDL_Delay on the monotonic clock.
 *
 * Test hooks (cost nothing unless the variable is set):
 *   SF3500_KEYSCRIPT="60:E,120:S:30"   press Enter at frame 60, Space at frame 120 for 30 frames
 *                                      (keys: E enter, S space, L/R/U/D arrows, C escape; 3 frames by default)
 *   SF3500_SHOT_PREFIX=p               with SF3500_SHOT_FRAMES="100,200" writes p_<frame>.ppm
 *   SF3500_QUIT_FRAME=n                exits cleanly after n presented frames
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "SDL.h"
#include "../sf3500/joykey.h"
#include "../sf3500/hwdisp.h"
#include "../sf3500/hwaudio.h"

extern int  fxsdl_audio_init(void);
extern void fxsdl_audio_quit(void);

/* ------------------------------------------------------------------------------------------- */
/* timing                                                                                      */
/* ------------------------------------------------------------------------------------------- */

static struct timespec t0;
static int t0_set = 0;

static long long now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	if (!t0_set) { t0 = t; t0_set = 1; }
	return (long long)(t.tv_sec - t0.tv_sec) * 1000000LL + (t.tv_nsec - t0.tv_nsec) / 1000;
}

Uint32 SDL_GetTicks(void) { return (Uint32)(now_us() / 1000); }

void SDL_Delay(Uint32 ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------------------------------- */
/* init / quit                                                                                 */
/* ------------------------------------------------------------------------------------------- */

static int video_up = 0;
static int quitting = 0;

static const SDL_version linked = { SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL };
const SDL_version *SDL_Linked_Version(void) { return &linked; }

int SDL_Init(Uint32 flags)
{
	now_us();
	if ((flags & SDL_INIT_VIDEO) && !video_up) {
		joykey_init();
		if (hwdisp_init() != 0)
			fprintf(stderr, "fxsdl: no display driver, frames are not shown\n");
		video_up = 1;
	}
	if (flags & SDL_INIT_AUDIO) {
		if (fxsdl_audio_init() < 0) return -1;
	}
	return 0;
}

void SDL_Quit(void)
{
	if (quitting) return;
	quitting = 1;
	fxsdl_audio_quit();
	hwaudio_deinit();
	hwdisp_deinit();
}

/* ------------------------------------------------------------------------------------------- */
/* video                                                                                       */
/* ------------------------------------------------------------------------------------------- */

static SDL_Surface screen_surf;
static SDL_PixelFormat screen_fmt;
static SDL_Palette screen_pal;
static SDL_Color pal[256];
static unsigned short pal565[256];
static unsigned short *out565 = NULL;
static unsigned frames = 0;

static unsigned short pack565(int r, int g, int b)
{
	return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void palette_changed(int first, int n)
{
	int i;
	for (i = first; i < first + n && i < 256; i++)
		pal565[i] = pack565(pal[i].r, pal[i].g, pal[i].b);
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
	int bytes;

	if (width <= 0 || height <= 0 || (bpp != 8 && bpp != 16)) {
		fxsdl_set_error("video mode not supported");
		return NULL;
	}
	bytes = bpp / 8;

	free(screen_surf.pixels);
	free(out565);
	out565 = NULL;
	memset(&screen_surf, 0, sizeof(screen_surf));
	memset(&screen_fmt, 0, sizeof(screen_fmt));

	screen_surf.pitch = (Uint16)((width * bytes + 3) & ~3);
	screen_surf.pixels = calloc(1, (size_t)screen_surf.pitch * height);
	if (!screen_surf.pixels) { fxsdl_set_error("out of memory"); return NULL; }
	screen_surf.w = width;
	screen_surf.h = height;
	screen_surf.flags = (flags & (SDL_DOUBLEBUF | SDL_FULLSCREEN | SDL_HWPALETTE)) | SDL_HWSURFACE;
	screen_surf.format = &screen_fmt;

	screen_fmt.BitsPerPixel = (Uint8)bpp;
	screen_fmt.BytesPerPixel = (Uint8)bytes;
	if (bpp == 16) {
		screen_fmt.Rmask = 0xF800; screen_fmt.Gmask = 0x07E0; screen_fmt.Bmask = 0x001F;
		screen_fmt.Rshift = 11; screen_fmt.Gshift = 5; screen_fmt.Bshift = 0;
		screen_fmt.Rloss = 3; screen_fmt.Gloss = 2; screen_fmt.Bloss = 3;
	} else {
		screen_pal.ncolors = 256;
		screen_pal.colors = pal;
		screen_fmt.palette = &screen_pal;
		out565 = (unsigned short *)malloc((size_t)width * height * 2);
		if (!out565) { fxsdl_set_error("out of memory"); return NULL; }
	}
	fprintf(stderr, "fxsdl: video mode %dx%d, %d bit%s\n", width, height, bpp,
	        (screen_surf.flags & SDL_DOUBLEBUF) ? ", double buffer" : "");
	return &screen_surf;
}

int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }

int SDL_SetColors(SDL_Surface *s, SDL_Color *colors, int first, int n)
{
	int i;
	if (!s || !colors || s != &screen_surf || screen_fmt.BitsPerPixel != 8) return 0;
	if (first < 0 || first + n > 256) return 0;
	for (i = 0; i < n; i++) pal[first + i] = colors[i];
	palette_changed(first, n);
	return 1;
}

int SDL_SetPalette(SDL_Surface *s, int flags, SDL_Color *colors, int first, int n)
{
	(void)flags;
	return SDL_SetColors(s, colors, first, n);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
	if (fmt && fmt->palette) {
		/* nearest entry of the palette */
		int i, best = 0, bestd = 1 << 30;
		for (i = 0; i < fmt->palette->ncolors; i++) {
			int dr = fmt->palette->colors[i].r - r, dg = fmt->palette->colors[i].g - g, db = fmt->palette->colors[i].b - b;
			int d = dr * dr + dg * dg + db * db;
			if (d < bestd) { bestd = d; best = i; if (!d) break; }
		}
		return (Uint32)best;
	}
	return pack565(r, g, b);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth, int pitch,
                                      Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am)
{
	/* Only fxi's window icon uses this; the device has no window, so a header is enough. */
	SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(*s));
	SDL_PixelFormat *f = (SDL_PixelFormat *)calloc(1, sizeof(*f));
	if (!s || !f) { free(s); free(f); return NULL; }
	f->BitsPerPixel = (Uint8)depth; f->BytesPerPixel = (Uint8)(depth / 8);
	f->Rmask = rm; f->Gmask = gm; f->Bmask = bm; f->Amask = am;
	s->format = f; s->w = w; s->h = h; s->pitch = (Uint16)pitch; s->pixels = pixels;
	return s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s || s == &screen_surf) return;
	free(s->format);
	free(s);
}

int SDL_SetColorKey(SDL_Surface *s, Uint32 flag, Uint32 key)
{
	if (s && s->format) s->format->colorkey = key;
	(void)flag;
	return 0;
}

void SDL_WM_SetCaption(const char *title, const char *icon) { (void)title; (void)icon; }
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask) { (void)icon; (void)mask; }
int  SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }

/* ---- frame dumps and the frame counter for tests ------------------------------------------- */

static void write_ppm(const char *path, const unsigned short *rgb565, int w, int h, int pitch_px)
{
	FILE *f = fopen(path, "wb");
	int x, y;
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			unsigned short p = rgb565[y * pitch_px + x];
			unsigned char c[3];
			c[0] = (unsigned char)(((p >> 11) & 31) * 255 / 31);
			c[1] = (unsigned char)(((p >> 5) & 63) * 255 / 63);
			c[2] = (unsigned char)((p & 31) * 255 / 31);
			fwrite(c, 1, 3, f);
		}
	fclose(f);
}

static int frame_listed(const char *list, unsigned frame)
{
	const char *p = list;
	while (p && *p) {
		char *end;
		unsigned long v = strtoul(p, &end, 10);
		if (end == p) break;
		if (v == frame) return 1;
		p = (*end == ',') ? end + 1 : end;
	}
	return 0;
}

static void present(void)
{
	static long long last_us = 0, sum_us = 0, present_us = 0;
	static unsigned count = 0;
	long long a, b;
	const void *src;
	int pitch;
	static const char *shot_prefix = (const char *)-1, *shot_frames = NULL, *quit_frame = NULL;

	if (shot_prefix == (const char *)-1) {
		shot_prefix = getenv("SF3500_SHOT_PREFIX");
		shot_frames = getenv("SF3500_SHOT_FRAMES");
		quit_frame = getenv("SF3500_QUIT_FRAME");
	}

	a = now_us();
	if (screen_fmt.BitsPerPixel == 8) {
		const unsigned char *s = (const unsigned char *)screen_surf.pixels;
		unsigned short *d = out565;
		int x, y;
		for (y = 0; y < screen_surf.h; y++) {
			const unsigned char *row = s + (size_t)y * screen_surf.pitch;
			for (x = 0; x < screen_surf.w; x++) *d++ = pal565[row[x]];
		}
		src = out565; pitch = screen_surf.w * 2;
	} else {
		src = screen_surf.pixels; pitch = screen_surf.pitch;
	}
	hwdisp_present(src, screen_surf.w, screen_surf.h, pitch);
	b = now_us();

	frames++;
	if (shot_prefix && shot_frames && frame_listed(shot_frames, frames)) {
		char path[512];
		snprintf(path, sizeof(path), "%s_%u.ppm", shot_prefix, frames);
		write_ppm(path, (const unsigned short *)src, screen_surf.w, screen_surf.h, pitch / 2);
	}

	/* One log line about every 250 frames: achieved frame rate and the time spent in the driver. */
	if (last_us) { sum_us += a - last_us; present_us += b - a; count++; }
	last_us = a;
	if (count >= 250) {
		fprintf(stderr, "fxsdl: perf frames=%u avg=%lld us (%.1f fps) present=%lld us\n",
		        frames, sum_us / count, 1e6 * count / (double)sum_us, present_us / count);
		sum_us = present_us = 0; count = 0;
	}
	if (quit_frame && frames >= (unsigned)atoi(quit_frame)) {
		fprintf(stderr, "fxsdl: SF3500_QUIT_FRAME reached\n");
		SDL_Quit();
		exit(0);
	}
}

void SDL_Flip(SDL_Surface *s) { if (s == &screen_surf) present(); }

void SDL_UpdateRect(SDL_Surface *s, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
	(void)x; (void)y; (void)w; (void)h;
	if (s == &screen_surf) present();
}

/* ------------------------------------------------------------------------------------------- */
/* input                                                                                       */
/* ------------------------------------------------------------------------------------------- */

static Uint8 keystate[SDLK_LAST];
static Uint8 prevstate[SDLK_LAST];
static int unicode_on = 0;

static struct { SDLKey sym; Uint16 uni; } const jk_key[JK_COUNT] = {
	/* JK_LEFT   */ { SDLK_LEFT,   0 },
	/* JK_RIGHT  */ { SDLK_RIGHT,  0 },
	/* JK_UP     */ { SDLK_UP,     0 },
	/* JK_DOWN   */ { SDLK_DOWN,   0 },
	/* JK_A      */ { SDLK_SPACE,  ' ' },   /* jump */
	/* JK_B      */ { SDLK_SPACE,  ' ' },   /* jump */
	/* JK_L1     */ { SDLK_UNKNOWN, 0 },
	/* JK_R1     */ { SDLK_UNKNOWN, 0 },
	/* JK_X      */ { SDLK_RETURN, 13 },    /* start / confirm */
	/* JK_Y      */ { SDLK_UNKNOWN, 0 },
	/* JK_START  */ { SDLK_RETURN, 13 },
	/* JK_SELECT */ { SDLK_ESCAPE, 0 },    /* back to the title; from the title, leaves the game */
};

/* ---- scripted keys for headless tests ------------------------------------------------------- */

#define SCRIPT_MAX 64
static struct { unsigned frame, hold; SDLKey sym; } script[SCRIPT_MAX];
static int script_n = 0, script_loaded = 0;

static void load_script(void)
{
	const char *e = getenv("SF3500_KEYSCRIPT");
	script_loaded = 1;
	while (e && *e && script_n < SCRIPT_MAX) {
		char *end;
		unsigned long frame = strtoul(e, &end, 10);
		unsigned long hold = 3;
		SDLKey sym = SDLK_UNKNOWN;
		if (end == e || *end != ':') break;
		switch (end[1]) {
		case 'E': sym = SDLK_RETURN; break;
		case 'S': sym = SDLK_SPACE; break;
		case 'L': sym = SDLK_LEFT; break;
		case 'R': sym = SDLK_RIGHT; break;
		case 'U': sym = SDLK_UP; break;
		case 'D': sym = SDLK_DOWN; break;
		case 'C': sym = SDLK_ESCAPE; break;
		}
		e = end + 2;
		if (*e == ':') hold = strtoul(e + 1, (char **)&e, 10);
		script[script_n].frame = (unsigned)frame;
		script[script_n].hold = (unsigned)hold;
		script[script_n].sym = sym;
		script_n++;
		if (*e == ',') e++; else break;
	}
}

#define EVQ 64
static SDL_Event evq[EVQ];
static int ev_head = 0, ev_tail = 0;

static void push_key(int down, SDLKey sym, Uint16 uni)
{
	SDL_Event *e = &evq[ev_tail];
	int next = (ev_tail + 1) % EVQ;
	if (next == ev_head) return; /* full: drop */
	memset(e, 0, sizeof(*e));
	e->key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e->key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e->key.keysym.sym = sym;
	e->key.keysym.unicode = unicode_on ? uni : 0;
	e->key.keysym.mod = KMOD_NONE;
	ev_tail = next;
}

static void refresh_input(void)
{
	unsigned int bits = joykey_read();
	Uint8 now[SDLK_LAST];
	Uint16 uni[SDLK_LAST];
	int i;

	if (!script_loaded) load_script();

	memset(now, 0, sizeof(now));
	memset(uni, 0, sizeof(uni));
	for (i = 0; i < JK_COUNT; i++) {
		if ((bits & (1u << i)) && jk_key[i].sym != SDLK_UNKNOWN) {
			now[jk_key[i].sym] = 1;
			uni[jk_key[i].sym] = jk_key[i].uni;
		}
	}
	for (i = 0; i < script_n; i++) {
		if (frames >= script[i].frame && frames < script[i].frame + script[i].hold && script[i].sym != SDLK_UNKNOWN) {
			now[script[i].sym] = 1;
			uni[script[i].sym] = (script[i].sym == SDLK_SPACE) ? ' ' : (script[i].sym == SDLK_RETURN) ? 13 : 0;
		}
	}

	/* Standing SF3500 exit combo: Select + Start. */
	if ((bits & (1u << JK_SELECT)) && (bits & (1u << JK_START))) {
		fprintf(stderr, "fxsdl: Select+Start, leaving\n");
		SDL_Quit();
		exit(0);
	}

	for (i = 0; i < SDLK_LAST; i++) {
		if (now[i] != prevstate[i]) push_key(now[i], (SDLKey)i, uni[i]);
		prevstate[i] = now[i];
		keystate[i] = now[i];
	}
}

int SDL_PollEvent(SDL_Event *event)
{
	if (ev_head == ev_tail) refresh_input();
	if (ev_head == ev_tail) return 0;
	if (event) *event = evq[ev_head];
	ev_head = (ev_head + 1) % EVQ;
	return 1;
}

Uint8 *SDL_GetKeyState(int *numkeys)
{
	if (numkeys) *numkeys = SDLK_LAST;
	return keystate;
}

SDLMod SDL_GetModState(void) { return KMOD_NONE; }
int SDL_EnableUNICODE(int enable) { int old = unicode_on; if (enable >= 0) unicode_on = enable; return old; }

int SDL_NumJoysticks(void) { return 0; }
int SDL_JoystickEventState(int state) { return state; }

/* ---- CD audio: none ---------------------------------------------------------------------- */

SDL_CD *SDL_CDOpen(int drive) { (void)drive; return NULL; }
void SDL_CDClose(SDL_CD *cd) { (void)cd; }
CDstatus SDL_CDStatus(SDL_CD *cd) { (void)cd; return CD_TRAYEMPTY; }
int SDL_CDPlayTracks(SDL_CD *cd, int a, int b, int c, int d) { (void)cd; (void)a; (void)b; (void)c; (void)d; return -1; }
int SDL_CDStop(SDL_CD *cd) { (void)cd; return -1; }
