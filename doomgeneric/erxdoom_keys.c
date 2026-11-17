/*
 * The window's kbd records and the table from their runes to Doom's keys
 * (erxdoom_keys.h).
 *
 * The runes are erXos's (abi/formats/input.txt, enum input_rune, Plan 9's
 * values; abi/keymaps/us.txt for the keys that type a character).  The
 * bindings follow doomgeneric's X11 port: Ctrl fires, Space uses, Shift
 * runs and Alt strafes.
 */

#include <stddef.h>
#include <string.h>

#include "doomkeys.h"
#include "erxdoom_keys.h"

/* erXos's special runes (abi/formats/input.txt). */
enum {
	RUNE_BS = 0x08,
	RUNE_TAB = 0x09,
	RUNE_ENTER = 0x0a,
	RUNE_ESC = 0x1b,
	RUNE_SPACE = 0x20,
	RUNE_DEL = 0x7f,
	RUNE_F1 = 0xf001,
	RUNE_F10 = 0xf00a,
	RUNE_F11 = 0xf00b,
	RUNE_F12 = 0xf00c,
	RUNE_HOME = 0xf00d,
	RUNE_UP = 0xf00e,
	RUNE_PGUP = 0xf00f,
	RUNE_PRINT = 0xf010,
	RUNE_LEFT = 0xf011,
	RUNE_RIGHT = 0xf012,
	RUNE_PGDOWN = 0xf013,
	RUNE_INS = 0xf014,
	RUNE_ALT = 0xf015,
	RUNE_SHIFT = 0xf016,
	RUNE_CTL = 0xf017,
	RUNE_END = 0xf018,
	RUNE_SCROLL = 0xf019,
	RUNE_DOWN = 0xf800,
	RUNE_BREAK = 0xf861,
	RUNE_CAPS = 0xf864,
	RUNE_NUM = 0xf865,
};

static const struct {
	uint32_t rune;
	unsigned char key;
} special[] = {
	{ RUNE_BS, KEY_BACKSPACE },
	{ RUNE_TAB, KEY_TAB },
	{ RUNE_ENTER, KEY_ENTER },
	{ RUNE_ESC, KEY_ESCAPE },
	{ RUNE_SPACE, KEY_USE },
	{ RUNE_DEL, KEY_DEL },
	{ RUNE_F11, KEY_F11 },
	{ RUNE_F12, KEY_F12 },
	{ RUNE_HOME, KEY_HOME },
	{ RUNE_UP, KEY_UPARROW },
	{ RUNE_PGUP, KEY_PGUP },
	{ RUNE_PRINT, KEY_PRTSCR },
	{ RUNE_LEFT, KEY_LEFTARROW },
	{ RUNE_RIGHT, KEY_RIGHTARROW },
	{ RUNE_PGDOWN, KEY_PGDN },
	{ RUNE_INS, KEY_INS },
	{ RUNE_ALT, KEY_RALT },
	{ RUNE_SHIFT, KEY_RSHIFT },
	{ RUNE_CTL, KEY_FIRE },
	{ RUNE_END, KEY_END },
	{ RUNE_SCROLL, KEY_SCRLCK },
	{ RUNE_DOWN, KEY_DOWNARROW },
	{ RUNE_BREAK, KEY_PAUSE },
	{ RUNE_CAPS, KEY_CAPSLOCK },
	{ RUNE_NUM, KEY_NUMLOCK },
};

unsigned char
erxdoom_key(uint32_t rune)
{
	for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); i++)
		if (special[i].rune == rune)
			return (special[i].key);
	/* F1 to F10 are contiguous in both sets; F11 and F12 are not in Doom's. */
	if (rune >= RUNE_F1 && rune <= RUNE_F10)
		return ((unsigned char)(KEY_F1 + (rune - RUNE_F1)));
	/* A key that types a printable character: its unshifted rune. */
	if (rune > RUNE_SPACE && rune < RUNE_DEL)
		return ((unsigned char)rune);
	return (0);
}

/*
 * Decodes the UTF-8 rune at p[0..len): answers its length, or 0 for a
 * sequence that is cut short, has a bad lead or continuation byte, or is
 * longer than four bytes.
 */
static size_t
utf8_rune(const unsigned char *p, size_t len, uint32_t *rune)
{
	size_t n;
	uint32_t r;

	if (p[0] < 0x80) {
		*rune = p[0];
		return (1);
	}
	if (p[0] >= 0xc0 && p[0] < 0xe0) {
		n = 2;
		r = p[0] & 0x1f;
	} else if (p[0] >= 0xe0 && p[0] < 0xf0) {
		n = 3;
		r = p[0] & 0x0f;
	} else if (p[0] >= 0xf0 && p[0] < 0xf8) {
		n = 4;
		r = p[0] & 0x07;
	} else
		return (0);
	if (n > len)
		return (0);
	for (size_t i = 1; i < n; i++) {
		if ((p[i] & 0xc0) != 0x80)
			return (0);
		r = r << 6 | (p[i] & 0x3f);
	}
	*rune = r;
	return (n);
}

size_t
erxdoom_kbd_record(const unsigned char *buf, size_t len, int *kind,
    erxdoom_held_t *held)
{
	const unsigned char *end = memchr(buf, 0, len);
	size_t at = 1, n;
	uint32_t rune;

	if (end == NULL)
		return (0);
	*kind = end == buf ? 0 : buf[0];
	held->n = 0;
	if (*kind != 'k' && *kind != 'K')
		return ((size_t)(end - buf) + 1);
	while (buf + at < end) {
		n = utf8_rune(buf + at, (size_t)(end - buf) - at, &rune);
		if (n == 0) {
			*kind = 0;
			break;
		}
		if (held->n < ERXDOOM_HELD_MAX)
			held->runes[held->n++] = rune;
		at += n;
	}
	return ((size_t)(end - buf) + 1);
}

static int
held_has(const erxdoom_held_t *set, uint32_t rune)
{
	for (size_t i = 0; i < set->n; i++)
		if (set->runes[i] == rune)
			return (1);
	return (0);
}

size_t
erxdoom_key_edges(const erxdoom_held_t *was, const erxdoom_held_t *now,
    erxdoom_edge_t out[ERXDOOM_EDGES_MAX])
{
	size_t n = 0;

	for (size_t i = 0; i < now->n; i++)
		if (!held_has(was, now->runes[i]))
			out[n++] = (erxdoom_edge_t){ now->runes[i], 1 };
	for (size_t i = 0; i < was->n; i++)
		if (!held_has(now, was->runes[i]))
			out[n++] = (erxdoom_edge_t){ was->runes[i], 0 };
	return (n);
}
