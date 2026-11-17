/* The IWAD, linked into the program: a native program has no open() yet. */
	.section .rodata.erxdoom_wad, "a"
	.balign 16
	.globl erxdoom_wad
	.globl erxdoom_wad_end
erxdoom_wad:
	.incbin ERXDOOM_WAD_PATH
erxdoom_wad_end:
