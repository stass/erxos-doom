/*
 * Host test of the table from a window's key runes to Doom's keys, of the
 * kbd record reader and of the edges between held sets (erxdoom_keys.h).
 * run.rb's `test` builds and runs it.
 */

#include <stdio.h>
#include <string.h>

#include "doomkeys.h"
#include "erxdoom_keys.h"

static int failures;

static void
expect_key(unsigned int rune, unsigned int want)
{
	unsigned int got = erxdoom_key(rune);

	if (got != want) {
		printf("rune %#x: key %#x, want %#x\n", rune, got, want);
		failures++;
	}
}

/*
 * Reads the record at bytes[0..len) and checks the bytes taken, the kind
 * and, for 'k' and 'K', the runes.
 */
static void
expect_record(const char *bytes, size_t len, size_t want_used, int want_kind,
    size_t want_n, const uint32_t *want_runes)
{
	erxdoom_held_t held = { .n = 0 };
	int kind = -1;
	size_t used = erxdoom_kbd_record((const unsigned char *)bytes, len,
	    &kind, &held);
	int same = used == want_used;

	if (same && used != 0)
		same = kind == want_kind;
	if (same && used != 0 && (kind == 'k' || kind == 'K'))
		same = held.n == want_n && memcmp(held.runes, want_runes,
		    want_n * sizeof(uint32_t)) == 0;
	if (!same) {
		printf("record of %zu bytes: used %zu kind %d n %zu, "
		    "want %zu %d %zu\n", len, used, kind, held.n, want_used,
		    want_kind, want_n);
		failures++;
	}
}

static void
expect_edges(const erxdoom_held_t *was, const erxdoom_held_t *now,
    size_t want_n, const erxdoom_edge_t *want)
{
	erxdoom_edge_t got[ERXDOOM_EDGES_MAX];
	size_t n = erxdoom_key_edges(was, now, got);
	int same = n == want_n;

	for (size_t i = 0; same && i < n; i++)
		same = got[i].rune == want[i].rune &&
		    got[i].pressed == want[i].pressed;
	if (!same) {
		printf("edges: %zu, want %zu\n", n, want_n);
		failures++;
	}
}

static void
records(void)
{
	/* Kind, runes in UTF-8, NUL; a read may hold more than one. */
	expect_record("k\n", 3, 3, 'k', 1, (const uint32_t[]){ 0x0a });
	expect_record("K", 2, 2, 'K', 0, NULL);
	/* Ctrl (U+F017) and Up (U+F00E), three bytes each, then 'c'. */
	expect_record("k\xef\x80\x97\xef\x80\x8e\0c", 10, 8, 'k', 2,
	    (const uint32_t[]){ 0xf017, 0xf00e });
	expect_record("c\xc3\xa9", 4, 4, 'c', 0, NULL);
	/* No NUL yet: nothing taken. */
	expect_record("k\xef\x80", 3, 0, 0, 0, NULL);
	/* A cut or bad sequence makes the record kind 0, taken whole. */
	expect_record("k\xef\x80", 4, 4, 0, 0, NULL);
	expect_record("ka\x80", 4, 4, 0, 0, NULL);
	expect_record("k\xf8\x80\x80\x80\x80", 7, 7, 0, 0, NULL);
	expect_record("", 1, 1, 0, 0, NULL);
	/* A set longer than ERXDOOM_HELD_MAX keeps its first runes. */
	expect_record("kabcdefghijklmnopqrs", 21, 21, 'k', ERXDOOM_HELD_MAX,
	    (const uint32_t[]){ 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
	    'j', 'k', 'l', 'm', 'n', 'o', 'p' });
}

static void
edges(void)
{
	const erxdoom_held_t none = { .n = 0 };
	const erxdoom_held_t ctl = { { 0xf017 }, 1 };
	const erxdoom_held_t ctl_up = { { 0xf017, 0xf00e }, 2 };
	const erxdoom_held_t up = { { 0xf00e }, 1 };

	expect_edges(&none, &ctl, 1, (const erxdoom_edge_t[]){ { 0xf017, 1 } });
	expect_edges(&ctl, &ctl_up, 1,
	    (const erxdoom_edge_t[]){ { 0xf00e, 1 } });
	expect_edges(&ctl_up, &up, 1,
	    (const erxdoom_edge_t[]){ { 0xf017, 0 } });
	expect_edges(&ctl, &up, 2,
	    (const erxdoom_edge_t[]){ { 0xf00e, 1 }, { 0xf017, 0 } });
	expect_edges(&up, &up, 0, NULL);
	expect_edges(&ctl_up, &none, 2,
	    (const erxdoom_edge_t[]){ { 0xf017, 0 }, { 0xf00e, 0 } });
}

int
main(void)
{
	/* The menu's keys, with the runes abi/keymaps/us.txt gives them. */
	expect_key(0x0a, KEY_ENTER);
	expect_key(0x1b, KEY_ESCAPE);
	expect_key(0xf00e, KEY_UPARROW);
	expect_key(0xf800, KEY_DOWNARROW);
	expect_key(0xf011, KEY_LEFTARROW);
	expect_key(0xf012, KEY_RIGHTARROW);
	expect_key('y', 'y');
	expect_key('n', 'n');
	/* Play: Ctrl fires, Space uses, Shift runs, Alt strafes. */
	expect_key(0xf017, KEY_FIRE);
	expect_key(0x20, KEY_USE);
	expect_key(0xf016, KEY_RSHIFT);
	expect_key(0xf015, KEY_RALT);
	/* Weapons, the automap and the screen size. */
	expect_key('1', '1');
	expect_key('7', '7');
	expect_key(0x09, KEY_TAB);
	expect_key('-', KEY_MINUS);
	expect_key('=', KEY_EQUALS);
	/* F1 to F10 run together; F11 and F12 do not in Doom's set. */
	expect_key(0xf001, KEY_F1);
	expect_key(0xf005, KEY_F5);
	expect_key(0xf00a, KEY_F10);
	expect_key(0xf00b, KEY_F11);
	expect_key(0xf00c, KEY_F12);
	expect_key(0x08, KEY_BACKSPACE);
	expect_key(0x7f, KEY_DEL);
	expect_key(0xf861, KEY_PAUSE);
	/* Runes Doom has no key for. */
	expect_key(0, 0);
	expect_key(0x01, 0);
	expect_key(0xe9, 0);
	expect_key(0xf000, 0);
	expect_key(0xf867, 0);
	expect_key(0x10ffff, 0);
	records();
	edges();

	if (failures == 0)
		printf("erxdoom_keys: ok\n");
	return (failures != 0);
}
