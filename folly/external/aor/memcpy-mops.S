/*
 * memcpy using MOPS extension.
 *
 * Copyright (c) 2023, Arm Limited.
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

#include "asmdefs.h"

ENTRY (__folly_memcpy_aarch64_mops)
	PTR_ARG (0)
	PTR_ARG (1)
	SIZE_ARG (2)

	mov	x3, x0
	.inst	0x19010443	/* cpyfp   [x3]!, [x1]!, x2!  */
	.inst	0x19410443	/* cpyfm   [x3]!, [x1]!, x2!  */
	.inst	0x19810443	/* cpyfe   [x3]!, [x1]!, x2!  */
	ret

END (__folly_memcpy_aarch64_mops)
