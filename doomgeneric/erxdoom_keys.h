/*
 * The window's kbd records and the table from their runes to Doom's keys:
 * a demo, not shipped.
 *
 * A read of the program's /dev/kbd (docs/design-display.md, "The window
 * tree") answers records of one byte naming the kind, the record's runes
 * in UTF-8 and a NUL: 'k' and 'K' carry the whole set of runes held after
 * a key goes down or comes up, 'c' the rune a key types.  Doom wants edges,
 * so the glue keeps the last set and turns each new one into the runes
 * that joined it and those that left it.
 */

#ifndef ERXDOOM_KEYS_H
#define ERXDOOM_KEYS_H

#include <stddef.h>
#include <stdint.h>

/* The runes of a held set the glue keeps; a longer set's rest is dropped. */
#define ERXDOOM_HELD_MAX	16
#define ERXDOOM_EDGES_MAX	(2 * ERXDOOM_HELD_MAX)

typedef struct erxdoom_held {
	uint32_t	runes[ERXDOOM_HELD_MAX];
	size_t		n;
} erxdoom_held_t;

typedef struct erxdoom_edge {
	uint32_t	rune;
	int		pressed;
} erxdoom_edge_t;

/*
 * Doom's key for the rune `rune` of a window's key set, or 0 for a rune
 * Doom has no key for.  A held set carries each key's unshifted rune, so
 * letters arrive lower case, as Doom's keys are.
 */
unsigned char erxdoom_key(uint32_t rune);

/*
 * Reads the record at buf[0..len): answers the bytes it takes, its NUL
 * included, or 0 when buf holds no NUL.  Sets *kind to the record's kind
 * byte and, for 'k' and 'K', *held to its runes.  An empty record, and a
 * 'k' or 'K' record with a sequence cut short or a bad lead or continuation
 * byte, get kind 0; an overlong form reads as its value, which the key
 * table maps to no key unless it names one.
 */
size_t erxdoom_kbd_record(const unsigned char *buf, size_t len, int *kind,
    erxdoom_held_t *held);

/*
 * The edges from the held set `was` to `now`: a press for each rune of now
 * not in was, then a release for each rune of was not in now.  Answers
 * their count.
 */
size_t erxdoom_key_edges(const erxdoom_held_t *was, const erxdoom_held_t *now,
    erxdoom_edge_t out[ERXDOOM_EDGES_MAX]);

#endif
