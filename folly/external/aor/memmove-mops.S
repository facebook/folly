/*
 * memmove using MOPS extension.
 *
 * Copyright (c) 2023, Arm Limited.
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

#include "asmdefs.h"

ENTRY (__folly_memmove_aarch64_mops)
	PTR_ARG (0)
	PTR_ARG (1)
	SIZE_ARG (2)

	mov	x3, x0
	.inst	0x1d010443	/* cpyp    [x3]!, [x1]!, x2!  */
	.inst	0x1d410443	/* cpym    [x3]!, [x1]!, x2!  */
	.inst	0x1d810443	/* cpye    [x3]!, [x1]!, x2!  */
	ret

END (__folly_memmove_aarch64_mops)
