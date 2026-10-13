# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

	.section .xa, "ax"
	.byte 1
	.section .ra, "a"
	.byte 2
	.section .wa, "aw"
	.byte 3
	.section .ba, "aw", @nobits
	.skip 1
	.section .xb, "ax"
	.byte 4
	.section .rb, "a"
	.byte 5
	.section .wb, "aw"
	.byte 6
