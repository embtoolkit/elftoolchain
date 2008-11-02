/*-
 * Copyright (c) 2007,2008 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(__FBSDID)
__FBSDID("$FreeBSD$");
#elif defined(__RCSID)
__RCSID("$Id$");
#endif

#include <sys/param.h>
#include <sys/stat.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "elfcopy.h"
#include "target.h"

enum options
{
	ECP_ADD_SECTION,
	ECP_GLOBALIZE_SYMBOL,
	ECP_GLOBALIZE_SYMBOLS,
	ECP_LOCALIZE_SYMBOLS,
	ECP_ONLY_DEBUG,
	ECP_RENAME_SECTION,
	ECP_SET_SEC_FLAGS,
	ECP_STRIP_UNNEEDED
};

static struct option strip_longopts[] =
{
	{"discard-all", no_argument, NULL, 'x'},
	{"discard-locals", no_argument, NULL, 'X'},
	{"help", no_argument, NULL, 'h'},
	{"input-target", required_argument, NULL, 'I'},
	{"keep-symbol", required_argument, NULL, 'K'},
	{"only-keep-debug", no_argument, NULL, ECP_ONLY_DEBUG},
	{"output-file", required_argument, NULL, 'o'},
	{"output-target", required_argument, NULL, 'O'},
	{"preserve-dates", no_argument, NULL, 'p'},
	{"remove-section", required_argument, NULL, 'R'},
	{"strip-all", no_argument, NULL, 's'},
	{"strip-debug", no_argument, NULL, 'S'},
	{"strip-symbol", required_argument, NULL, 'N'},
	{"strip-unneeded", no_argument, NULL, ECP_STRIP_UNNEEDED},
	{NULL, 0, NULL, 0}
};

static struct option elfcopy_longopts[] =
{
	{"add-section", required_argument, NULL, ECP_ADD_SECTION},
	{"discard-all", no_argument, NULL, 'x'},
	{"discard-locals", no_argument, NULL, 'X'},
	{"globalize-symbol", required_argument, NULL, ECP_GLOBALIZE_SYMBOL},
	{"globalize-symbols", required_argument, NULL, ECP_GLOBALIZE_SYMBOLS},
	{"help", no_argument, NULL, 'h'},
	{"input-target", required_argument, NULL, 'I'},
	{"keep-symbol", required_argument, NULL, 'K'},
	{"localize-symbol", required_argument, NULL, 'L'},
	{"localize-symbols", required_argument, NULL, ECP_LOCALIZE_SYMBOLS},
	{"only-keep-debug", no_argument, NULL, ECP_ONLY_DEBUG},
	{"only-section", required_argument, NULL, 'j'},
	{"output-target", required_argument, NULL, 'O'},
	{"preserve-dates", no_argument, NULL, 'p'},
	{"remove-section", required_argument, NULL, 'R'},
	{"rename-section", required_argument, NULL, ECP_RENAME_SECTION},
	{"set-section-flags", required_argument, NULL, ECP_SET_SEC_FLAGS},
	{"strip-all", no_argument, NULL, 'S'},
	{"strip-debug", no_argument, 0, 'g'},
	{"strip-symbol", required_argument, NULL, 'N'},
	{"strip-unneeded", no_argument, NULL, ECP_STRIP_UNNEEDED},
	{NULL, 0, NULL, 0}
};

static struct {
	const char *name;
	int value;
} sec_flags[] = {
	{"alloc", SF_ALLOC},
	{"load", SF_LOAD},
	{"noload", SF_NOLOAD},
	{"readonly", SF_READONLY},
	{"debug", SF_DEBUG},
	{"code", SF_CODE},
	{"data", SF_DATA},
	{"rom", SF_ROM},
	{"share", SF_SHARED},
	{"contents", SF_CONTENTS},
	{NULL, 0}
};

static void	create_elf(struct elfcopy *ecp);
static void	create_file(struct elfcopy *ecp, const char *src,
    const char *dst);
static void	create_object(struct elfcopy *ecp, int ifd, int ofd);
static void	parse_sec_flags(struct sec_action *sac, char *s);
static void	parse_symlist_file(struct elfcopy *ecp, const char *fn,
    unsigned int op);
static void	strip_main(struct elfcopy *ecp, int argc, char **argv);
static void	strip_usage(void);
static void	set_output_target(struct elfcopy *ecp, const char *target_name);
static void	mcs_main(struct elfcopy *ecp, int argc, char **argv);
static void	mcs_usage(void);
static void	elfcopy_main(struct elfcopy *ecp, int argc, char **argv);
static void	elfcopy_usage(void);

/* 
 * An ELF object usually has a sturcture described by the
 * diagram below.
 *  _____________
 * |             |
 * |     NULL    | <- always a SHT_NULL section
 * |_____________|
 * |             |
 * |   .interp   |
 * |_____________|
 * |             |
 * |     ...     |
 * |_____________|
 * |             |
 * |    .text    |
 * |_____________|
 * |             |
 * |     ...     |
 * |_____________|
 * |             |
 * |  .comment   | <- above(include) this: normal sections
 * |_____________|
 * |             |
 * | add sections| <- unloadable sections added by --add-section
 * |_____________|
 * |             |
 * |  .shstrtab  | <- section name string table
 * |_____________|
 * |             |
 * |    shdrs    | <- section header table
 * |_____________|
 * |             |
 * |   .symtab   | <- symbol table, if any
 * |_____________|
 * |             |
 * |   .strtab   | <- symbol name string table, if any
 * |_____________|
 * |             |
 * |  .rel.text  | <- relocation info for .o files.
 * |_____________|
 */
static void
create_elf(struct elfcopy *ecp)
{
	struct section *shtab;
	GElf_Ehdr ieh;
	GElf_Ehdr oeh;
	size_t ishnum;

	ecp->flags |= SYMTAB_INTACT;

	if (gelf_getehdr(ecp->ein, &ieh) == NULL)
		errx(EX_SOFTWARE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));
        if ((ecp->iec = gelf_getclass(ecp->ein)) == ELFCLASSNONE)
                errx(EX_SOFTWARE, "getclass() failed: %s",
                    elf_errmsg(-1));

	if (ecp->oec == ELFCLASSNONE)
		ecp->oec = ecp->iec;

	if (gelf_newehdr(ecp->eout, ecp->oec) == NULL)
		errx(EX_SOFTWARE, "gelf_newehdr failed: %s",
		    elf_errmsg(-1));
	if (gelf_getehdr(ecp->eout, &oeh) == NULL)
		errx(EX_SOFTWARE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));

	memcpy(oeh.e_ident, ieh.e_ident, sizeof(ieh.e_ident));
	oeh.e_ident[EI_CLASS] = ecp->oec;
	if (ecp->oed != ELFDATANONE)
		oeh.e_ident[EI_DATA] = ecp->oed;
	oeh.e_flags	= ieh.e_flags;
	oeh.e_machine	= ieh.e_machine;
	oeh.e_type	= ieh.e_type;
	oeh.e_entry	= ieh.e_entry;
	oeh.e_version	= ieh.e_version;

	if (ieh.e_type == ET_EXEC)
		ecp->flags |= EXECUTABLE;
	else if (ieh.e_type == ET_DYN)
		ecp->flags |= DYNAMIC;
	else if (ieh.e_type == ET_REL)
		ecp->flags |= RELOCATABLE;
	else
		errx(EX_DATAERR, "unsupported e_type");

	if (!elf_getshnum(ecp->ein, &ishnum))
		errx(EX_SOFTWARE, "elf_getshnum failed: %s",
		    elf_errmsg(-1));
	if (ishnum > 0 && (ecp->secndx = calloc(ishnum,
	    sizeof(*ecp->secndx))) == NULL)
		err(EX_SOFTWARE, "calloc failed");

	setup_phdr(ecp);

	create_scn(ecp);

	/* FIXME */
	if (ecp->strip == STRIP_DEBUG ||
	    ecp->strip == STRIP_UNNEEDED ||
	    !STAILQ_EMPTY(&ecp->v_symop))
		ecp->flags &= ~SYMTAB_INTACT;

	if (ecp->sections_to_add != 0)
		add_unloadables(ecp);

	if ((ecp->flags & SYMTAB_EXIST) != 0)
		create_symtab(ecp);

	copy_content(ecp);

	/*
	 * Write the underlying ehdr. Note that it should be called
	 * before elf_setshstrndx() since it will overwrite e->e_shstrndx.
	 */
	if (gelf_update_ehdr(ecp->eout, &oeh) == 0)
		errx(EX_SOFTWARE, "gelf_update_ehdr() failed: %s",
		    elf_errmsg(-1));

	/* Put .shstrtab after sections added from file. */
	set_shstrtab(ecp);

	/* Update section headers. */
	update_shdr(ecp);

	/* Renew oeh to get the updated e_shstrndx */
	if (gelf_getehdr(ecp->eout, &oeh) == NULL)
		errx(EX_SOFTWARE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));

	shtab = insert_shtab(ecp);

	/* Resync section offsets in the output object. */
	resync_sections(ecp);

	oeh.e_shoff = shtab->off;

#if 0
	fprintf(stderr, "ecp->shstrtab_off = %d\n", (int)ecp->shstrtab_off);
	fprintf(stderr, "ecp->shstrtab_size = %d\n", (int)ecp->shstrtab_size);
	fprintf(stderr, "e_shoff = %d\n", (int)oehdr.e_shoff);
	fprintf(stderr, "ecp->osec_cnt = %d\n", ecp->osec_cnt);
	fprintf(stderr, "ecp->symtab_off = %d\n", (int)ecp->symtab_off);
	fprintf(stderr, "ecp->symtab_size = %d\n", (int)ecp->symtab_size);
	fprintf(stderr, "ecp->strtab_off = %d\n", (int)ecp->strtab_off);
	fprintf(stderr, "ecp->strtab_size = %d\n", (int)ecp->strtab_size);
#endif
	/*
	 * Put program header table immediately after the Elf header.
	 */
	if (ecp->ophnum > 0) {
		oeh.e_phoff = gelf_fsize(ecp->eout, ELF_T_EHDR, 1, EV_CURRENT);
		if (oeh.e_phoff == 0)
			errx(EX_SOFTWARE, "gelf_fsize() failed: %s",
			    elf_errmsg(-1));
	}

	/*
	 * Update ehdr again before we call elf_update(), since we
	 * modified e_shoff and e_phoff.
	 */
	if (gelf_update_ehdr(ecp->eout, &oeh) == 0)
		errx(EX_SOFTWARE, "gelf_update_ehdr() failed: %s",
		    elf_errmsg(-1));

	if (ecp->ophnum > 0)
		copy_phdr(ecp);

        if (elf_update(ecp->eout, ELF_C_WRITE) < 0)
                errx(EX_SOFTWARE, "elf_update() failed: %s",
                    elf_errmsg(-1));
}

/* Create ELF_K_ELF object or ELF_K_AR object. */
static void
create_object(struct elfcopy *ecp, int ifd, int ofd)
{
	if ((ecp->ein = elf_begin(ifd, ELF_C_READ, NULL)) == NULL) {
		errx(EX_DATAERR, "elf_begin() failed: %s",
		     elf_errmsg(-1));
		return;
	}

	switch (elf_kind(ecp->ein)) {
	case ELF_K_NONE:
		errx(EX_DATAERR, "file format not recognized");
		return;
	case ELF_K_ELF:
		if ((ecp->eout = elf_begin(ofd, ELF_C_WRITE, NULL)) == NULL)
			errx(EX_SOFTWARE, "elf_begin() failed: %s",
			    elf_errmsg(-1));

		elf_flagelf(ecp->eout, ELF_C_SET, ELF_F_LAYOUT);
		create_elf(ecp);
		elf_end(ecp->eout);
		break;

	case ELF_K_AR:
		fprintf(stderr, "archive file not supported yet");
		return;
	default:
		errx(EX_DATAERR, "file format not supported");
	}

	elf_end(ecp->ein);
}

#define	TEMPLATE "ecp.XXXXXXXX"

static void
create_file(struct elfcopy *ecp, const char *src, const char *dst)
{
	struct stat sb;
	struct timeval tv[2];
	const char *tmpdir;
	char *cp, *tmpf;
	size_t tlen, plen;
	int ifd, ofd;

	if (src == NULL)
		errx(EX_SOFTWARE, "internal: src == NULL");
	if ((ifd = open(src, O_RDONLY)) == -1)
		err(EX_IOERR, "open %s failed", src);

	if (ecp->flags & PRESERVE_DATE) {
		if (fstat(ifd, &sb) == -1)
			err(EX_IOERR, "fstat %s failed", src);
	}

	tmpf = NULL;
	if (dst == NULL) {
		/* Repect TMPDIR environment variable. */
		tmpdir = getenv("TMPDIR");
		if (tmpdir != NULL && *tmpdir != '\0') {
			tlen = strlen(tmpdir);
			plen = strlen(TEMPLATE);
			tmpf = malloc(tlen + plen + 2);
			if (tmpf == NULL)
				err(EX_SOFTWARE, "malloc failed");
			strncpy(tmpf, tmpdir, tlen);
			cp = &tmpf[tlen - 1];
			if (*cp++ != '/')
				*cp++ = '/';
			strncpy(cp, TEMPLATE, plen);
			cp[plen] = '\0';
		} else {
			tmpf = strdup(TEMPLATE);
			if (tmpf == NULL)
				err(EX_SOFTWARE, "strdup failed");
		}
		if ((ofd = mkstemp(tmpf)) == -1)
			err(EX_IOERR, "mkstemp %s failed", tmpf);
		if (fchmod(ofd, 0755) == -1)
			err(EX_IOERR, "fchmod %s failed", tmpf);
	} else
		if ((ofd = open(dst, O_RDWR|O_CREAT, 0755)) == -1)
			err(EX_IOERR, "open %s failed", dst);

	create_object(ecp, ifd, ofd);

	if (dst == NULL && tmpf != NULL) {
		if (rename(tmpf, src) == -1)
			err(EX_IOERR, "rename %s to %s failed",
			    tmpf, src);
		free(tmpf);
	}

	if (ecp->flags & PRESERVE_DATE) {
		tv[0].tv_sec = sb.st_atime;
		tv[0].tv_usec = 0;
		tv[1].tv_sec = sb.st_mtime;
		tv[1].tv_usec = 0;
		if (futimes(ofd, tv) == -1)
			err(EX_IOERR, "futimes failed");
	}

	close(ifd);
	close(ofd);
}

static void
elfcopy_main(struct elfcopy *ecp, int argc, char **argv)
{
	struct sec_action *sac;
	struct sec_add *sa;
	struct stat sb;
	const char *infile, *outfile;
	char *fn, *s;
	FILE *fp;
	int opt, len;

	while ((opt = getopt_long(argc, argv, "I:j:K:L:N:O:pR:sSdgxX",
	    elfcopy_longopts, NULL)) != -1) {
		switch(opt) {
		case 'R':
			sac = lookup_sec_act(ecp, optarg, 1);
			if (sac->copy != 0)
				errx(EX_DATAERR,
				    "both copy and remove specified");
			sac->remove = 1;
			ecp->sections_to_remove = 1;
			break;
		case 'S':
			ecp->strip = STRIP_ALL;
			break;
		case 'g':
			ecp->strip = STRIP_DEBUG;
			break;
		case 'I':
		case 's':
			/* ignored */
			break;
		case 'j':
			sac = lookup_sec_act(ecp, optarg, 1);
			if (sac->remove != 0)
				errx(EX_DATAERR,
				    "both copy and remove specified");
			sac->copy = 1;
			ecp->sections_to_copy = 1;
			break;
		case 'K':
			add_to_symop_list(ecp, optarg, SYMOP_KEEP);
			break;
		case 'L':
			add_to_symop_list(ecp, optarg, SYMOP_LOCALIZE);
			break;
		case 'N':
			add_to_symop_list(ecp, optarg, SYMOP_STRIP);
			break;
		case 'O':
			set_output_target(ecp, optarg);
			break;
		case 'p':
			ecp->flags |= PRESERVE_DATE;
			break;
		case 'x':
		case 'X':
			ecp->flags |= DISCARD_LOCAL;
			break;
		case ECP_ADD_SECTION:
			if ((s = strchr(optarg, '=')) == NULL)
				errx(EX_USAGE,
				    "illegal format for --add-section option");
			if ((sa = malloc(sizeof(*sa))) == NULL)
				err(EX_SOFTWARE, "malloc failed");

			len = s - optarg;
			if ((sa->name = malloc(len + 1)) == NULL)
				err(EX_SOFTWARE, "malloc failed");
			strncpy(sa->name, optarg, len);
			sa->name[len] = '\0';

			fn = s + 1;
			if (stat(fn, &sb) == -1)
				err(EX_DATAERR, "stat failed");
			sa->size = sb.st_size;
			if ((sa->content = malloc(sa->size)) == NULL)
				err(EX_SOFTWARE, "malloc failed");
			if ((fp = fopen(fn, "r")) == NULL)
				err(EX_DATAERR, "can not open %s", fn);
			if (fread(sa->content, 1, sa->size, fp) == 0 ||
			    ferror(fp))
				err(EX_DATAERR, "fread failed");
			fclose(fp);

			STAILQ_INSERT_TAIL(&ecp->v_sadd, sa, sadd_list);
			ecp->sections_to_add = 1;
			break;
		case ECP_GLOBALIZE_SYMBOL:
			add_to_symop_list(ecp, optarg, SYMOP_GLOBALIZE);
			break;
		case ECP_GLOBALIZE_SYMBOLS:
			parse_symlist_file(ecp, optarg, SYMOP_GLOBALIZE);
			break;
		case ECP_LOCALIZE_SYMBOLS:
			parse_symlist_file(ecp, optarg, SYMOP_LOCALIZE);
			break;
		case ECP_ONLY_DEBUG:
			ecp->strip = STRIP_NONDEBUG;
			break;
		case ECP_RENAME_SECTION:
			if ((fn = strchr(optarg, '=')) == NULL)
				errx(EX_USAGE,
				    "illegal format for --rename-section");
			*fn++ = '\0';

			/* Check for optional flags. */
			if ((s = strchr(fn, ',')) != NULL)
				*s++ = '\0';
				
			sac = lookup_sec_act(ecp, optarg, 1);
			sac->rename = 1;
			sac->newname = fn;
			if (s != NULL)
				parse_sec_flags(sac, s);
			break;
		case ECP_SET_SEC_FLAGS:
			if ((s = strchr(optarg, '=')) == NULL)
				errx(EX_USAGE,
				    "illegal format for --set-section-flags");
			*s++ = '\0';
			sac = lookup_sec_act(ecp, optarg, 1);
			parse_sec_flags(sac, s);
			break;
		case ECP_STRIP_UNNEEDED:
			ecp->strip = STRIP_UNNEEDED;
			break;
		default:
			elfcopy_usage();
		}
	}

	if (optind == argc || optind + 2 < argc)
		elfcopy_usage();

	infile = argv[optind];
	outfile = NULL;
	if (optind + 1 < argc)
		outfile = argv[optind + 1];

	create_file(ecp, infile, outfile);
}

static void
mcs_main(struct elfcopy *ecp, int argc, char **argv)
{
	struct sec_action *sac;
	const char *string;
	int append, delete, compress, name, print;
	int opt, i;

	append = delete = compress = name = print = 0;
	string = NULL;
	while ((opt = getopt(argc, argv, "a:cdn:pV")) != -1) {
		switch(opt) {
		case 'a':
			append = 1;
			string = optarg; /* XXX multiple -a not supported */
			break;
		case 'c':
			compress = 1;
			break;
		case 'd':
			delete = 1;
			break;
		case 'n':
			name = 1;
			(void)lookup_sec_act(ecp, optarg, 1);
			break;
		case 'p':
			print = 1;
			break;
		case 'V':
			fprintf(stderr, "mcs %s\n", ELFCOPY_VERSION);
			exit(EX_OK);
		default:
			mcs_usage();
		}
	}

	if (optind == argc)
		mcs_usage();

	/* Must specify one operation at least. */
	if (!append && !compress && !delete && !print)
		mcs_usage();

	/*
	 * If we are going to delete, ignore other operations. This is
	 * different from the Solaris implementation, which can print
	 * and delete a section at the same time, for example. Also, this
	 * implementation do not respect the order between operations that
	 * user specified, i.e., "mcs -pc a.out" equals to "mcs -cp a.out".
	 */
	if (delete) {
		append = compress = print = 0;
		ecp->sections_to_remove = 1;
	}
	ecp->sections_to_append = append;
	ecp->sections_to_compress = compress;
	ecp->sections_to_print = print;

	/* .comment is the default section to operate on. */
	if (!name)
		(void)lookup_sec_act(ecp, ".comment", 1);

	STAILQ_FOREACH(sac, &ecp->v_sac, sac_list) {
		sac->append = append;
		sac->compress = compress;
		sac->print = print;
		sac->remove = delete;
		sac->string = string;
	}

	for (i = optind; i < argc; i++) {
		/* If only -p is specified, output to /dev/null */
		if (print && !append && !compress && !delete)
			create_file(ecp, argv[i], "/dev/null");
		else
			create_file(ecp, argv[i], NULL);
	}
}

static void
strip_main(struct elfcopy *ecp, int argc, char **argv)
{
	struct sec_action *sac;
	const char *outfile;
	int opt;
	int i;

	outfile = NULL;
	while ((opt = getopt_long(argc, argv, "I:K:N:o:O:pR:sSdgxX",
	    strip_longopts, NULL)) != -1) {
		switch(opt) {
		case 'R':
			sac = lookup_sec_act(ecp, optarg, 1);
			sac->remove = 1;
			ecp->sections_to_remove = 1;
			break;
		case 's':
			ecp->strip = STRIP_ALL;
			break;
		case 'S':
		case 'g':
		case 'd':
			ecp->strip = STRIP_DEBUG;
			break;
		case 'I':
			/* ignored */
			break;
		case 'K':
			add_to_symop_list(ecp, optarg, SYMOP_KEEP);
			break;
		case 'N':
			add_to_symop_list(ecp, optarg, SYMOP_STRIP);
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'O':
			set_output_target(ecp, optarg);
			break;
		case 'p':
			ecp->flags |= PRESERVE_DATE;
			break;
		case 'x':
		case 'X':
			ecp->flags |= DISCARD_LOCAL;
			break;
		case ECP_ONLY_DEBUG:
			ecp->strip = STRIP_NONDEBUG;
			break;
		case ECP_STRIP_UNNEEDED:
			ecp->strip = STRIP_UNNEEDED;
			break;
		default:
			strip_usage();
		}
	}

	if (ecp->strip == 0)
		ecp->strip = STRIP_ALL;
	if (optind == argc)
		strip_usage();

	for (i = optind; i < argc; i++)
		create_file(ecp, argv[i], outfile);
}

static void
parse_sec_flags(struct sec_action *sac, char *s)
{
	const char *flag;
	int found, i;

	for (flag = strtok(s, ","); flag; flag = strtok(NULL, ",")) {
		found = 0;
		for (i = 0; sec_flags[i].name != NULL; i++)
			if (strcasecmp(sec_flags[i].name, flag) == 0) {
				sac->flags |= sec_flags[i].value;
				found = 1;
				break;
			}
		if (!found)
			errx(EX_USAGE, "unrecognized section flag %s", flag);
	}

	printf("sec->flags = 0x%4x\n",sac->flags);
}

static void
parse_symlist_file(struct elfcopy *ecp, const char *fn, unsigned int op)
{
	struct symfile *sf;
	struct stat sb;
	FILE *fp;
	char *data, *p, *line, *end, *e;

	if (stat(fn, &sb) == -1)
		err(EX_IOERR, "stat %s failed", fn);

	/* Check if we already read and processed this file. */
	STAILQ_FOREACH(sf, &ecp->v_symfile, symfile_list) {
		if (sf->dev == sb.st_dev && sf->ino == sb.st_ino)
			goto process_symfile;
	}

	if ((fp = fopen(fn, "r")) == NULL)
		err(EX_IOERR, "can not open %s", fn);
	if ((data = malloc(sb.st_size + 1)) == NULL)
		err(EX_SOFTWARE, "malloc failed");
	if (fread(data, 1, sb.st_size, fp) == 0 || ferror(fp))
		err(EX_DATAERR, "fread failed");
	fclose(fp);
	data[sb.st_size] = '\0';

	if ((sf = calloc(1, sizeof(*sf))) == NULL)
		err(EX_SOFTWARE, "malloc failed");
	sf->dev = sb.st_dev;
	sf->ino = sb.st_ino;
	sf->size = sb.st_size + 1;
	sf->data = data;

process_symfile:

	/*
	 * Basically what we do here is to convert EOL to '\0', and remove
	 * leading and trailing whitespaces for each line.
	 */

	end = sf->data + sf->size;
	line = NULL;
	for(p = sf->data; p < end; p++) {
		/* Skip leading whitespaces. */
		if ((*p == '\t' || *p == ' ') && line == NULL)
			continue;

		if (*p == '\r' || *p == '\n' || *p == '\0') {
			*p = '\0';
			if (line != NULL) {
				/* Skip comment. */
				if (*line == '#') {
					line = NULL;
					continue;
				}
				/* Remove trailing whitespace. */
				e = p - 1;
				while(e != line && (*e == '\t' || *e == ' '))
					*e-- = '\0';
				add_to_symop_list(ecp, line, op);
				line = NULL;
			}
			continue;
		}

		if (line == NULL)
			line = p;
	}
}

static void
set_output_target(struct elfcopy *ecp, const char *target_name)
{
	elf_target *tgt;

	if ((tgt = elf_find_target(target_name)) == NULL)
		errx(EX_USAGE, "%s: invalid target name", target_name);
	ecp->oec = elf_target_class(tgt);
	ecp->oed = elf_target_byteorder(tgt);
}

static void
elfcopy_usage()
{

	fprintf(stderr, "usage: elfcopy\n");
	exit(EX_USAGE);
}

static void
mcs_usage()
{

	fprintf(stderr, "usage: mcs [-cdpVz] [-a string] [-n name] file ...\n");
	exit(EX_USAGE);
}

static void
strip_usage()
{

	fprintf(stderr, "usage: strip\n");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	struct elfcopy *ecp;

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EX_SOFTWARE, "ELF library initialization failed: %s",
		    elf_errmsg(-1));

	ecp = malloc(sizeof(*ecp));
	if (ecp == NULL)
		err(EX_SOFTWARE, "malloc failed");
	memset(ecp, 0, sizeof(*ecp));

	/* There is always an empty section. */
	ecp->nos = 1;

	STAILQ_INIT(&ecp->v_seg);
	STAILQ_INIT(&ecp->v_sac);
	STAILQ_INIT(&ecp->v_sadd);
	STAILQ_INIT(&ecp->v_symop);
	STAILQ_INIT(&ecp->v_symfile);
	TAILQ_INIT(&ecp->v_sec);

	if ((ecp->progname = getprogname()) == NULL)
		ecp->progname = "elfcopy";

	if (strcmp(ecp->progname, "strip") == 0)
		strip_main(ecp, argc, argv);
	else if (strcmp(ecp->progname, "mcs") == 0)
		mcs_main(ecp, argc, argv);
	else
		elfcopy_main(ecp, argc, argv);

	free(ecp);

	exit(EX_OK);
}
