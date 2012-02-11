/*-
 * Copyright (c) 2011,2012 Kai Wang
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "ld.h"
#include "ld_arch.h"
#include "ld_output.h"
#include "ld_layout.h"

ELFTC_VCSID("$Id$");

#define	WRITE_8(P,V)					\
	do {						\
		*(P)++ = (V) & 0xff;			\
	} while (0)
#define	WRITE_16(P,V)					\
	do {						\
		if (lo->lo_endian == ELFDATA2MSB)	\
			WRITE_16BE(P,V);		\
		else					\
			WRITE_16LE(P,V);		\
	} while (0)
#define	WRITE_32(P,V)					\
	do {						\
		if (lo->lo_endian == ELFDATA2MSB)	\
			WRITE_32BE(P,V);		\
		else					\
			WRITE_32LE(P,V);		\
	} while (0)
#define	WRITE_64(P,V)					\
	do {						\
		if (lo->lo_endian == ELFDATA2MSB)	\
			WRITE_64BE(P,V);		\
		else					\
			WRITE_64LE(P,V);		\
	} while (0)
#define	WRITE_16BE(P,V)					\
	do {						\
		(P)[0] = ((V) >> 8) & 0xff;             \
		(P)[1] = (V) & 0xff;                    \
		(P) += 2;                               \
	} while (0)
#define	WRITE_32BE(P,V)					\
	do {						\
		(P)[0] = ((V) >> 24) & 0xff;            \
		(P)[1] = ((V) >> 16) & 0xff;            \
		(P)[2] = ((V) >> 8) & 0xff;             \
		(P)[3] = (V) & 0xff;                    \
		(P) += 4;                               \
	} while (0)
#define	WRITE_64BE(P,V)					\
	do {						\
		WRITE_32BE((P),(V) >> 32);		\
		WRITE_32BE((P),(V) & 0xffffffffU);	\
	} while (0)
#define	WRITE_16LE(P,V)					\
	do {						\
		(P)[0] = (V) & 0xff;			\
		(P)[1] = ((V) >> 8) & 0xff;		\
	} while (0)
#define	WRITE_32LE(P,V)					\
	do {						\
		(P)[0] = (V) & 0xff;			\
		(P)[1] = ((V) >> 8) & 0xff;		\
		(P)[2] = ((V) >> 16) & 0xff;		\
		(P)[3] = ((V) >> 24) & 0xff;		\
	} while (0)
#define	WRITE_64LE(P,V)					\
	do {						\
		WRITE_32LE((P), (V) & 0xffffffffU);	\
		WRITE_32LE((P), (V) >> 32);		\
	} while (0)

static void _write_elf_header(struct ld *ld);

void
ld_output_init(struct ld *ld)
{
	struct ld_output *lo;

	if ((lo = calloc(1, sizeof(*lo))) == NULL)
		ld_fatal_std(ld, "calloc");
	STAILQ_INIT(&lo->lo_oelist);
	STAILQ_INIT(&lo->lo_oslist);
	ld->ld_output = lo;
}

void
ld_output_determine_arch(struct ld *ld)
{
	char *end, target[MAX_TARGET_NAME_LEN + 1];
	size_t len;

	if (ld->ld_otgt != NULL) {
		ld->ld_arch = ld_arch_get_arch_from_target(ld,
		    ld->ld_otgt_name);
		if (ld->ld_arch == NULL)
			ld_fatal(ld, "target %s is not supported",
			    ld->ld_otgt_name);
	} else {
		if ((end = strrchr(ld->ld_progname, '-')) != NULL &&
		    end != ld->ld_progname) {
			len = end - ld->ld_progname + 1;
			if (len > MAX_TARGET_NAME_LEN)
				return;
			strncpy(target, ld->ld_progname, len);
			target[len] = '\0';
			ld->ld_arch = ld_arch_get_arch_from_target(ld, target);
		}
	}
}

void
ld_output_verify_arch(struct ld *ld, struct ld_input *li)
{

	/*
	 * TODO: Guess arch if the output arch is not yet determined.
	 * Otherwise, verify the arch of the input object match the arch
	 * of the output file.
	 */

	(void) ld;
	(void) li;
}

void
ld_output_format(struct ld *ld, char *def, char *be, char *le)
{

	ld->ld_otgt_name = def;
	if ((ld->ld_otgt = elftc_bfd_find_target(def)) == NULL)
		ld_fatal(ld, "invalid BFD format %s", def);

	ld->ld_otgt_be_name = be;
	if ((ld->ld_otgt_be = elftc_bfd_find_target(be)) == NULL)
		ld_fatal(ld, "invalid BFD format %s", be);

	ld->ld_otgt_le_name = le;
	if ((ld->ld_otgt_le = elftc_bfd_find_target(le)) == NULL)
		ld_fatal(ld, "invalid BFD format %s", le);
}

struct ld_output_element *
ld_output_create_element(struct ld *ld, struct ld_output_element_head *head,
    enum ld_output_element_type type, void *entry)
{
	struct ld_output_element *oe;

	if ((oe = calloc(1, sizeof(*oe))) == NULL)
		ld_fatal_std(ld, "calloc");
	oe->oe_type = type;
	oe->oe_entry = entry;
	STAILQ_INSERT_TAIL(head, oe, oe_next);

	return (oe);
}

struct ld_output_section *
ld_output_alloc_section(struct ld *ld, const char *name,
    struct ld_output_section *after)
{
	struct ld_output *lo;
	struct ld_output_section *os;

	lo = ld->ld_output;
	if ((os = calloc(1, sizeof(*os))) == NULL)
		ld_fatal_std(ld, "calloc");
	if ((os->os_name = strdup(name)) == NULL)
		ld_fatal_std(ld, "strdup");
	STAILQ_INIT(&os->os_e);
	HASH_ADD_KEYPTR(hh, lo->lo_ostbl, os->os_name, strlen(os->os_name), os);
	if (after == NULL)
		STAILQ_INSERT_TAIL(&lo->lo_oslist, os, os_next);
	else
		STAILQ_INSERT_AFTER(&lo->lo_oslist, os, after, os_next);
	ld_output_create_element(ld, &lo->lo_oelist, OET_OUTPUT_SECTION, os);

	return (os);
}

void
ld_output_create(struct ld *ld)
{
	struct ld_output *lo;
	const char *fn;
	GElf_Ehdr eh;

	if (ld->ld_output_file == NULL)
		fn = "a.out";
	else
		fn = ld->ld_output_file;

	lo = ld->ld_output;

	if ((lo->lo_fd = open(fn, O_WRONLY)) < 0)
		ld_fatal_std(ld, "can not create output file: open %s", fn);
	if ((lo->lo_elf = elf_begin(lo->lo_fd, ELF_C_WRITE, NULL)) == NULL)
		ld_fatal(ld, "elf_begin failed: %s", elf_errmsg(-1));

	elf_flagelf(lo->lo_elf, ELF_C_SET, ELF_F_LAYOUT);

	assert(ld->ld_otgt != NULL);
	lo->lo_ec = elftc_bfd_target_class(ld->ld_otgt);
	lo->lo_endian = elftc_bfd_target_byteorder(ld->ld_otgt);

	if (gelf_newehdr(lo->lo_elf, lo->lo_ec) == NULL)
		ld_fatal(ld, "gelf_newehdr failed: %s", elf_errmsg(-1));
	if (gelf_getehdr(lo->lo_elf, &eh) == NULL)
		ld_fatal(ld, "gelf_getehdr failed: %s", elf_errmsg(-1));
	eh.e_flags = 0;		/* TODO */
	eh.e_machine = elftc_bfd_target_machine(ld->ld_otgt);
	eh.e_type = ET_EXEC;	/* TODO */
	eh.e_version = EV_CURRENT;

	if (gelf_update_ehdr(lo->lo_elf, &eh) == 0)
		ld_fatal(ld, "gelf_update_ehdr failed: %s", elf_errmsg(-1));
}

void
ld_output_write(struct ld *ld)
{

	_write_elf_header(ld);
}

static void
_write_elf_header(struct ld *ld)
{
	struct ld_output *lo;
	uint8_t *p;
	int i;

	lo = ld->ld_output;
	assert(lo != NULL);

	p = NULL;		/* TODO */

	WRITE_8(p, 0x7f);
	WRITE_8(p, 'E');
	WRITE_8(p, 'L');
	WRITE_8(p, 'F');
	WRITE_8(p, lo->lo_ec);
	WRITE_8(p, lo->lo_endian);
	WRITE_8(p, EV_CURRENT);
	WRITE_8(p, lo->lo_osabi);
	WRITE_8(p, 0);
	/* Padding */
	for (i = 0; i < 7; i++)
		WRITE_8(p, 0);
	
	if (lo->lo_ec == ELFCLASS32) {
		WRITE_16(p, ELF_K_ELF);
		/* TODO */
	} else {
		WRITE_16(p, ELF_K_ELF);
		/* TODO */
	}
}