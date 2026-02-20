/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Regents of the University of California
 */

#define __ASM_STR(x)	x
#define __REG_SEL(a, b)	__ASM_STR(a)
#define SZREG		__REG_SEL(8, 4)
#define REG_L		__REG_SEL(ld, lw)
#define REG_S		__REG_SEL(sd, sw)

#define pDst a0
#define pSrc a1
#define iNum a2
#define iVL a3
#define ELEM_LMUL_SETTING m8
#define vData v0

#define CSR_VSTART		0x8

.global __memcpy

__memcpy:
loop:
	vsetvli iVL, iNum, e8, ELEM_LMUL_SETTING, ta, ma
	vle8.v vData, (pSrc)
	sub iNum, iNum, iVL
	add pSrc, pSrc, iVL
	vse8.v vData, (pDst)
	add pDst, pDst, iVL
	bnez iNum, loop

	mv	a0, iNum
	ret
