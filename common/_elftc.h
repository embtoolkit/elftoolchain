/*-
 * Copyright (c) 2009 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

/**
 ** Miscellanous definitions needed by multiple components.
 **/

#ifndef	_ELFTC_H
#define	_ELFTC_H

#ifndef	NULL
#define NULL 	((void *) 0)
#endif

#ifndef	offsetof
#define	offsetof(T, M)		((int) &((T*) 0) -> M)
#endif

/*
 * Supply macros missing from <sys/queue.h>
 */

#ifndef	STAILQ_FOREACH_SAFE
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)            \
       for ((var) = STAILQ_FIRST((head));                      \
            (var) && ((tvar) = STAILQ_NEXT((var), field), 1);  \
            (var) = (tvar))
#endif

#ifndef	STAILQ_LAST
#define STAILQ_LAST(head, type, field)                                  \
        (STAILQ_EMPTY((head)) ?                                         \
                NULL :                                                  \
                ((struct type *)(void *)                                \
                ((char *)((head)->stqh_last) - offsetof(struct type, field))))
#endif

#ifndef	TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
	for ((var) = TAILQ_FIRST((head));                               \
	    (var) && ((tvar) = TAILQ_NEXT((var), field), 1);            \
	    (var) = (tvar))
#endif

/*
 * Symbols that are sometimes missing in system headers.
 */

#ifndef	DT_DEPRECATED_SPARC_REGISTER
#define	DT_DEPRECATED_SPARC_REGISTER	0x70000001
#endif

#ifndef	DT_GNU_HASH
#define	DT_GNU_HASH		0x6FFFFEF5U
#endif

#ifndef	DT_MAXPOSTAGS
#define	DT_MAXPOSTAGS		34
#endif

#ifndef	DT_SUNW_AUXILIARY
#define	DT_SUNW_AUXILIARY	0x6000000D
#endif

#ifndef	DT_SUNW_CAP
#define	DT_SUNW_CAP		0x60000010	
#endif

#ifndef	DT_SUNW_FILTER
#define	DT_SUNW_FILTER		0x6000000F	
#endif

#ifndef	DT_SUNW_RTLDINF
#define	DT_SUNW_RTLDINF		0x6000000E	
#endif

#ifndef	DT_USED
#define	DT_USED			0x7FFFFFFE	
#endif

#ifndef	ELFOSABI_86OPEN
#define	ELFOSABI_86OPEN		5
#endif

#ifndef	ELFOSABI_HURD
#define	ELFOSABI_HURD		4
#endif

#ifndef	ELFOSABI_NSK
#define	ELFOSABI_NSK		14
#endif

#ifndef	ELFOSABI_OPENVMS
#define	ELFOSABI_OPENVMS	13
#endif

/*
 * Supply missing EM_XXX definitions.
 */
#ifndef	EM_68HC05
#define	EM_68HC05		72
#endif

#ifndef	EM_68HC08
#define	EM_68HC08		71
#endif

#ifndef	EM_68HC11
#define	EM_68HC11		70
#endif

#ifndef	EM_68HC16
#define	EM_68HC16		69
#endif

#ifndef	EM_ARCA
#define	EM_ARCA			109
#endif

#ifndef	EM_ARC_A5
#define	EM_ARC_A5		93
#endif

#ifndef	EM_AVR
#define	EM_AVR			83
#endif

#ifndef	EM_BLACKFIN
#define	EM_BLACKFIN		106
#endif

#ifndef	EM_CR
#define	EM_CR			103
#endif

#ifndef	EM_CRIS
#define	EM_CRIS			76
#endif

#ifndef	EM_D10V
#define	EM_D10V			85
#endif

#ifndef	EM_D30V
#define	EM_D30V			86
#endif

#ifndef	EM_F2MC16
#define	EM_F2MC16		104
#endif

#ifndef	EM_FIREPATH
#define	EM_FIREPATH		78
#endif

#ifndef	EM_FR30
#define	EM_FR30			84
#endif

#ifndef	EM_FX66
#define	EM_FX66			66
#endif

#ifndef	EM_HUANY
#define	EM_HUANY		81
#endif

#ifndef	EM_IP2K
#define	EM_IP2K			101
#endif

#ifndef	EM_JAVELIN
#define	EM_JAVELIN		77
#endif

#ifndef	EM_M32R
#define	EM_M32R			88
#endif

#ifndef	EM_MAX
#define	EM_MAX			102
#endif

#ifndef	EM_MMIX
#define	EM_MMIX			80
#endif

#ifndef	EM_MN10200
#define	EM_MN10200		90
#endif

#ifndef	EM_MN10300
#define	EM_MN10300		89
#endif

#ifndef	EM_MSP430
#define	EM_MSP430		105
#endif

#ifndef	EM_NS32K
#define	EM_NS32K		97
#endif

#ifndef	EM_OPENRISC
#define	EM_OPENRISC		92
#endif

#ifndef	EM_PDSP
#define	EM_PDSP			63
#endif

#ifndef	EM_PJ
#define	EM_PJ			91
#endif

#ifndef	EM_PRISM
#define	EM_PRISM		82
#endif

#ifndef	EM_SEP
#define	EM_SEP			108
#endif

#ifndef	EM_SE_C33
#define	EM_SE_C33		107
#endif

#ifndef	EM_SNP1K
#define	EM_SNP1K		99
#endif

#ifndef	EM_ST19
#define	EM_ST19			74
#endif

#ifndef	EM_ST200
#define	EM_ST200		100
#endif

#ifndef	EM_ST7
#define	EM_ST7			68
#endif

#ifndef	EM_ST9PLUS
#define	EM_ST9PLUS		67
#endif

#ifndef	EM_SVX
#define	EM_SVX			73
#endif

#ifndef	EM_TMM_GPP
#define	EM_TMM_GPP		96
#endif

#ifndef	EM_TPC
#define	EM_TPC			98
#endif

#ifndef	EM_UNICORE
#define	EM_UNICORE		110
#endif

#ifndef	EM_V850
#define	EM_V850			87
#endif

#ifndef	EM_VAX
#define	EM_VAX			75
#endif

#ifndef	EM_VIDEOCORE
#define	EM_VIDEOCORE		95
#endif

#ifndef	EM_XTENSA
#define	EM_XTENSA		94
#endif

#ifndef	EM_ZSP
#define	EM_ZSP			79
#endif

#ifndef	PN_XNUM
#define	PN_XNUM			0xFFFFU
#endif

#ifndef	R_IA_64_DIR32LSB
#define	R_IA_64_DIR32LSB	0x25
#endif

#ifndef	R_IA_64_DIR64LSB
#define	R_IA_64_DIR64LSB	0x27
#endif

#ifndef	R_MIPS_32
#define	R_MIPS_32		0x2
#endif

#ifndef	SHT_AMD64_UNWIND
#define	SHT_AMD64_UNWIND	0x70000001
#endif

#ifndef	SHT_SUNW_ANNOTATE
#define	SHT_SUNW_ANNOTATE	0X6FFFFFF7
#endif

#ifndef	SHT_SUNW_DEBUGSTR
#define	SHT_SUNW_DEBUGSTR	0X6FFFFFF8
#endif

#ifndef	SHT_SUNW_DEBUG
#define	SHT_SUNW_DEBUG		0X6FFFFFF9
#endif

#ifndef	SHT_SUNW_cap
#define	SHT_SUNW_cap		0x6FFFFFF5
#endif

#ifndef	SHT_SUNW_dof
#define	SHT_SUNW_dof		0x6FFFFFF4
#endif

#ifndef	SHT_SUNW_verdef
#define	SHT_SUNW_verdef		0x6FFFFFFD
#endif

#ifndef	SHT_SUNW_verneed
#define	SHT_SUNW_verneed	0x6FFFFFFE
#endif

#ifndef	SHT_SUNW_versym
#define	SHT_SUNW_versym		0x6FFFFFFF
#endif

#ifndef	SHN_XINDEX
#define	SHN_XINDEX		0xFFFFU
#endif

#ifndef	SHT_GNU_HASH
#define	SHT_GNU_HASH		0x6FFFFFF6U
#endif


/*
 * VCS Ids.
 */

#ifndef	ELFTC_VCSID

#if defined(__FreeBSD__)
#define	ELFTC_VCSID(ID)		__FBSDID(ID)
#endif

#if defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
#define	ELFTC_VCSID(ID)		/**/
#endif

#if defined(__NetBSD__)
#define	ELFTC_VCSID(ID)		__RCSID(ID)
#endif

#endif	/* ELFTC_VCSID */

/*
 * Provide an equivalent for getprogname(3).
 */

#ifndef	ELFTC_GETPROGNAME

#if defined(__FreeBSD__) || defined(__NetBSD__)

#include <stdlib.h>

#define	ELFTC_GETPROGNAME()	getprogname()

#endif	/* defined(__FreeBSD__) || defined(__NetBSD__) */


#if defined(__linux__)

/*
 * GLIBC based systems have a global 'char *' pointer referencing
 * the executable's name.
 */
extern const char *program_invocation_short_name;

#define	ELFTC_GETPROGNAME()	program_invocation_short_name

#endif	/* __linux__ */

#endif	/* ELFTC_GETPROGNAME */

/**
 ** Per-OS configuration.
 **/

#if defined(__linux__)

#include <endian.h>

#define	ELFTC_BYTE_ORDER			__BYTE_ORDER
#define	ELFTC_BYTE_ORDER_LITTLE_ENDIAN		__LITTLE_ENDIAN
#define	ELFTC_BYTE_ORDER_BIG_ENDIAN		__BIG_ENDIAN

/*
 * Debian GNU/Linux is missing strmode(3).
 */
#define	ELFTC_HAVE_STRMODE			0

/* Whether we need to define Elf_Note. */
#define	ELFTC_NEED_ELF_NOTE_DEFINITION		1
/* Whether we need to supply {be,le}32dec. */
#define ELFTC_NEED_BYTEORDER_EXTENSIONS		1

#define	roundup2	roundup

#endif	/* __linux__ */


#if defined(__FreeBSD__)

#include <sys/endian.h>

#define	ELFTC_BYTE_ORDER			_BYTE_ORDER
#define	ELFTC_BYTE_ORDER_LITTLE_ENDIAN		_LITTLE_ENDIAN
#define	ELFTC_BYTE_ORDER_BIG_ENDIAN		_BIG_ENDIAN

#define	ELFTC_HAVE_STRMODE	1
#endif	/* __FreeBSD__ */


#if defined(__NetBSD__)

#include <sys/endian.h>

#define	ELFTC_BYTE_ORDER			_BYTE_ORDER
#define	ELFTC_BYTE_ORDER_LITTLE_ENDIAN		_LITTLE_ENDIAN
#define	ELFTC_BYTE_ORDER_BIG_ENDIAN		_BIG_ENDIAN

#define	ELFTC_HAVE_STRMODE	1
#endif	/* __NetBSD __ */

#endif	/* _ELFTC_H */
