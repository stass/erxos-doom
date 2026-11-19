/*
 * doomgeneric's platform layer for erXos: a demo, not shipped.
 *
 * The program is started with a window of its own (erx_prog_handler's
 * `window` option), so its /dev holds the window's files
 * (docs/design-files.md, "A program's window"):
 *
 * /dev/ctl,  read once: draw(3)'s information on image 0, the window's
 *            image, whose rectangle and channel DG_Init takes; the
 *            channel is x8r8g8b8 or a8r8g8b8, and on a8r8g8b8 every
 *            pixel drawn is opaque;
 * /dev/draw, written: each frame is `y` loads of image 0 in bands of
 *            rows, each write within the 64 KiB unit and ending on a
 *            message, the last ending with a `v`, which presents
 *            (docs/design-display.md, "Draw messages", "The draw
 *            connection");
 * /dev/kbd,  read by a thread of its own, since each read waits for a
 *            record: it turns the held sets of the `k` and `K` records
 *            into key edges (erxdoom_keys.h) and hands them to DG_GetKey
 *            through a ring.
 *
 * DG_DrawFrame scales Doom's frame by the largest whole factor that fits
 * the window as it was at the start and centres it.  It does not follow a
 * resize, which only a read of /dev/mouse would report: the display clips
 * image 0 to the window.
 *
 * Doom's IWAD is linked into the binary (wad.S) and every fopen of Doom's
 * goes through erxdoom_fopen (the build passes -Dfopen=erxdoom_fopen to
 * Doom's sources), which answers an fmemopen stream over the linked WAD
 * for its name and ENOENT for any other path: no config file, no
 * savegames.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "doomgeneric.h"
#include "erxdoom_keys.h"

#define WAD_NAME	"freedoom1.wad"

/* draw(3)'s ctl text: twelve fields, each 11 characters and a blank. */
#define CTL_BYTES	144
/* The most bytes one write to /dev/draw executes whole. */
#define DRAW_UNIT	65536
/* A `y` message before its pixels: the letter, the id and a rectangle. */
#define Y_HEADER	(1 + 4 + 4 * 4)
/* Key edges DG_GetKey has not yet taken; a power of two. */
#define RING_SIZE	256

/* The exits of a start the window cannot serve. */
enum {
	EXIT_OPEN = 10,		/* a file of /dev did not open */
	EXIT_CTL = 11,		/* ctl did not answer draw(3)'s text */
	EXIT_WINDOW = 12,	/* a channel or size Doom cannot draw in */
	EXIT_THREAD = 13,	/* the key reader did not start */
	EXIT_DRAW = 14,		/* a draw write failed */
};

extern const unsigned char erxdoom_wad[];
extern const unsigned char erxdoom_wad_end[];

static int draw_fd, kbd_fd;
static int image;		/* image 0's id, as ctl names it */
static long scale, left, top;	/* where Doom's frame lands in image 0 */
static uint32_t opaque;		/* OR-ed into each pixel: alpha 0xFF on a8r8g8b8 */
static unsigned char band[DRAW_UNIT];

/* Edges the key reader has put and DG_GetKey has taken, counted since the start. */
static struct {
	unsigned char	key;
	unsigned char	pressed;
} ring[RING_SIZE];
static atomic_uint ring_put, ring_taken;

FILE *
erxdoom_fopen(const char *path, const char *mode)
{
	const char *base = strrchr(path, '/');

	base = base != NULL ? base + 1 : path;
	if (strcmp(base, WAD_NAME) != 0 || mode[0] != 'r') {
		errno = ENOENT;
		return (NULL);
	}
	return (fmemopen((void *)erxdoom_wad,
	    (size_t)(erxdoom_wad_end - erxdoom_wad), "rb"));
}

static int
open_dev(const char *path, int flags)
{
	int fd = open(path, flags);

	if (fd < 0)
		exit(EXIT_OPEN);
	return (fd);
}

/*
 * Puts the edge of the rune into the ring when Doom has a key for it.  A
 * full ring drops the edge: DG_GetKey drains it every tic, so it is full
 * only while Doom stalls.
 */
static void
put_edge(const erxdoom_edge_t *edge)
{
	unsigned char key = erxdoom_key(edge->rune);
	unsigned put = atomic_load_explicit(&ring_put, memory_order_relaxed);

	if (key == 0 || put - atomic_load_explicit(&ring_taken,
	    memory_order_acquire) == RING_SIZE)
		return;
	ring[put % RING_SIZE].key = key;
	ring[put % RING_SIZE].pressed = (unsigned char)edge->pressed;
	atomic_store_explicit(&ring_put, put + 1, memory_order_release);
}

/*
 * The key reader: reads /dev/kbd until it fails, and puts the edges from
 * each held set to the next.  The window answers each read with whole
 * records, one a read; a record a read cuts short is dropped.
 */
static void *
read_keys(void *arg)
{
	unsigned char buf[512];
	erxdoom_held_t held = { .n = 0 }, now;
	erxdoom_edge_t edges[ERXDOOM_EDGES_MAX];
	size_t got, used, count;
	ssize_t n;
	int kind;

	(void)arg;
	for (;;) {
		n = read(kbd_fd, buf, sizeof(buf));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return (NULL);
		got = (size_t)n;
		for (size_t at = 0; at < got; at += used) {
			used = erxdoom_kbd_record(buf + at, got - at, &kind,
			    &now);
			if (used == 0)
				break;
			if (kind != 'k' && kind != 'K')
				continue;
			count = erxdoom_key_edges(&held, &now, edges);
			for (size_t i = 0; i < count; i++)
				put_edge(&edges[i]);
			held = now;
		}
	}
}

void
DG_Init(void)
{
	char text[CTL_BYTES + 1], chan[16];
	int ctl_fd, conn, repl, x0, y0, x1, y1, cx0, cy0, cx1, cy1;
	pthread_t reader;
	long width, height;

	draw_fd = open_dev("/dev/draw", O_RDWR);
	ctl_fd = open_dev("/dev/ctl", O_RDONLY);
	kbd_fd = open_dev("/dev/kbd", O_RDONLY);
	if (read(ctl_fd, text, CTL_BYTES) != CTL_BYTES)
		exit(EXIT_CTL);
	text[CTL_BYTES] = '\0';
	(void)close(ctl_fd);
	if (sscanf(text, "%d %d %15s %d %d %d %d %d %d %d %d %d", &conn,
	    &image, chan, &repl, &x0, &y0, &x1, &y1, &cx0, &cy0, &cx1,
	    &cy1) != 12)
		exit(EXIT_CTL);
	/*
	 * Doom's pixels are 0x00RRGGBB words, x8r8g8b8 in draw(3)'s order;
	 * a8r8g8b8 has the same layout with alpha in the top byte, which
	 * DG_DrawFrame sets to 0xFF so the display draws them opaque.
	 */
	width = (long)x1 - x0;
	height = (long)y1 - y0;
	if (strcmp(chan, "a8r8g8b8") == 0)
		opaque = 0xff000000u;
	else if (strcmp(chan, "x8r8g8b8") != 0)
		exit(EXIT_WINDOW);
	if (width < DOOMGENERIC_RESX || height < DOOMGENERIC_RESY)
		exit(EXIT_WINDOW);
	scale = width / DOOMGENERIC_RESX;
	if (height / DOOMGENERIC_RESY < scale)
		scale = height / DOOMGENERIC_RESY;
	/* A band must hold one row and the final `v`. */
	if (Y_HEADER + DOOMGENERIC_RESX * scale * 4 + 1 > DRAW_UNIT)
		scale = (DRAW_UNIT - Y_HEADER - 1) / (DOOMGENERIC_RESX * 4);
	left = x0 + (width - DOOMGENERIC_RESX * scale) / 2;
	top = y0 + (height - DOOMGENERIC_RESY * scale) / 2;
	if (pthread_create(&reader, NULL, read_keys, NULL) != 0)
		exit(EXIT_THREAD);
	(void)pthread_detach(reader);
}

static unsigned char *
put32(unsigned char *p, int32_t v)
{
	uint32_t u = (uint32_t)v;

	p[0] = (unsigned char)u;
	p[1] = (unsigned char)(u >> 8);
	p[2] = (unsigned char)(u >> 16);
	p[3] = (unsigned char)(u >> 24);
	return (p + 4);
}

/* Writes band[0..len), whole: a write within the unit is all or nothing. */
static void
write_band(size_t len)
{
	ssize_t n;

	while ((n = write(draw_fd, band, len)) < 0 && errno == EINTR)
		;
	if (n < 0 || (size_t)n != len)
		exit(EXIT_DRAW);
}

void
DG_DrawFrame(void)
{
	long rows = DOOMGENERIC_RESY * scale, cols = DOOMGENERIC_RESX * scale;
	size_t row_bytes = (size_t)cols * 4;
	long per = (long)((DRAW_UNIT - Y_HEADER - 1) / row_bytes);

	for (long y = 0; y < rows; y += per) {
		long n = rows - y < per ? rows - y : per;
		unsigned char *p = band;

		*p++ = 'y';
		p = put32(p, image);
		p = put32(p, (int32_t)left);
		p = put32(p, (int32_t)(top + y));
		p = put32(p, (int32_t)(left + cols));
		p = put32(p, (int32_t)(top + y + n));
		for (long r = y; r < y + n; r++, p += row_bytes) {
			const uint32_t *src = DG_ScreenBuffer +
			    (r / scale) * DOOMGENERIC_RESX;

			/* A repeat of the row above, within this band. */
			if (r != y && r % scale != 0) {
				memcpy(p, p - row_bytes, row_bytes);
				continue;
			}
			for (long x = 0; x < DOOMGENERIC_RESX; x++) {
				uint32_t px = src[x] | opaque;

				for (long k = 0; k < scale; k++)
					memcpy(p + (x * scale + k) * 4, &px, 4);
			}
		}
		if (y + n == rows)
			*p++ = 'v';
		write_band((size_t)(p - band));
	}
}

void
DG_SleepMs(uint32_t ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000 };

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

uint32_t
DG_GetTicksMs(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return ((uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

int
DG_GetKey(int *pressed, unsigned char *key)
{
	unsigned taken = atomic_load_explicit(&ring_taken,
	    memory_order_relaxed);

	if (taken == atomic_load_explicit(&ring_put, memory_order_acquire))
		return (0);
	*key = ring[taken % RING_SIZE].key;
	*pressed = ring[taken % RING_SIZE].pressed;
	atomic_store_explicit(&ring_taken, taken + 1, memory_order_release);
	return (1);
}

void
DG_SetWindowTitle(const char *title)
{
	(void)title;
}

int
main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);
	for (;;)
		doomgeneric_Tick();
}
