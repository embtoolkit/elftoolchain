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
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "elfcopy.h"

/* Symbol table buffer structure. */
struct symbuf {
	Elf32_Sym *l32;		/* 32bit local symbol */
	Elf32_Sym *g32;		/* 32bit global symbol */
	Elf64_Sym *l64;		/* 64bit local symbol */
	Elf64_Sym *g64;		/* 64bit global symbol */
	size_t ngs, nls;	/* number of each kind */
};

/* String table buffer structure. */
struct strbuf {
	char *l;		/* local symbol string table */
	char *g;		/* global symbol string table */
	size_t lsz, gsz;	/* size of each kind */
};

static int	is_debug_symbol(GElf_Sym *s);
static int	is_global_symbol(GElf_Sym *s);
static int	is_needed_symbol(struct elfcopy *ecp, int i, GElf_Sym *s);
static int	is_remove_symbol(struct elfcopy *ecp, size_t sc, int i,
		    GElf_Sym *s, const char *name);
static int	is_weak_symbol(GElf_Sym *s);
static int	lookup_exact_string(const char *buf, size_t sz, const char *s);
static int	generate_symbols(struct elfcopy *ecp);
static void	mark_symbols(struct elfcopy *ecp, size_t sc);

/* Convenient bit vector operation macros. */
#define BIT_SET(v, n) (v[(n)>>3] |= 1U << ((n) & 7))
#define BIT_CLR(v, n) (v[(n)>>3] &= ~(1U << ((n) & 7)))
#define BIT_ISSET(v, n) (v[(n)>>3] & (1U << ((n) & 7)))

static int
is_debug_symbol(GElf_Sym *s)
{

	if (GELF_ST_TYPE(s->st_info) == STT_SECTION ||
	    GELF_ST_TYPE(s->st_info) == STT_FILE)
		return (1);

	return (0);
}

static int
is_global_symbol(GElf_Sym *s)
{

	if (GELF_ST_BIND(s->st_info) == STB_GLOBAL)
		return (1);

	return (0);
}

static int
is_weak_symbol(GElf_Sym *s)
{

	if (GELF_ST_BIND(s->st_info) == STB_WEAK)
		return (1);

	return (0);
}

static int
is_local_symbol(GElf_Sym *s)
{

	if (GELF_ST_BIND(s->st_info) == STB_LOCAL)
		return (1);

	return (0);
}

/*
 * Symbols related to relocation are needed.
 */
static int
is_needed_symbol(struct elfcopy *ecp, int i, GElf_Sym *s)
{

	/* If symbol involves relocation, it is needed. */
	if (BIT_ISSET(ecp->v_rel, i))
		return (1);

	/*
	 * For relocatable files (.o files), global and weak symbols
	 * are needed.
	 */
	if (ecp->flags & RELOCATABLE) {
		if (is_global_symbol(s) || is_weak_symbol(s))
			return (1);
	}

	return (0);
}

static int
is_remove_symbol(struct elfcopy *ecp, size_t sc, int i, GElf_Sym *s,
    const char *name)
{
	GElf_Sym sym0 = {
		0, 		/* st_name */
		0,		/* st_value */
		0,		/* st_size */
		0,		/* st_info */
		0,		/* st_other */
		SHN_UNDEF,	/* st_shndx */
	};

	if (lookup_symop_list(ecp, name, SYMOP_KEEP) != NULL)
		return (0);

	if (lookup_symop_list(ecp, name, SYMOP_STRIP) != NULL)
		return (1);

	/*
	 * Keep the first symbol if it is the special reserved symbol.
	 * XXX Should we generate one if it's missing?
	 */
	if (i == 0 && !memcmp(s, &sym0, sizeof(GElf_Sym)))
		return (0);

	/* Remove the symbol if the section it refers to was removed. */
	if (s->st_shndx != SHN_UNDEF && s->st_shndx < SHN_LORESERVE &&
	    ecp->secndx[s->st_shndx] == 0)
		return (1);

	if (ecp->strip == STRIP_ALL)
		return (1);

	if (ecp->v_rel == NULL)
		mark_symbols(ecp, sc);

	if (is_needed_symbol(ecp, i, s))
		return (0);

	if (ecp->strip == STRIP_UNNEEDED)
		return (1);

	if ((ecp->flags & DISCARD_LOCAL) && is_local_symbol(s))
		return (1);

	if (ecp->strip == STRIP_DEBUG && is_debug_symbol(s))
		return (1);

	return (0);
}

/*
 * Mark symbols refered by relocation entries.
 */
static void
mark_symbols(struct elfcopy *ecp, size_t sc)
{
	const char	*name;
	Elf_Data	*d;
	Elf_Scn		*s;
	GElf_Rel	 r;
	GElf_Rela	 ra;
	GElf_Shdr	 sh;
	size_t		 n, indx;
	int		 elferr, i, len;

	ecp->v_rel = calloc((sc + 7) / 8, 1);
	if (ecp->v_rel == NULL)
		err(EX_SOFTWARE, "calloc failed");

	if (elf_getshstrndx(ecp->ein, &indx) == 0)
		errx(EX_SOFTWARE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));

	s = NULL;
	while ((s = elf_nextscn(ecp->ein, s)) != NULL) {
		if (gelf_getshdr(s, &sh) != &sh)
			errx(EX_SOFTWARE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));

		if (sh.sh_type != SHT_REL && sh.sh_type != SHT_RELA)
			continue;

		/*
		 * Skip if this reloc section won't appear in the
		 * output object.
		 */
		if ((name = elf_strptr(ecp->ein, indx, sh.sh_name)) == NULL)
			errx(EX_SOFTWARE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (is_remove_section(ecp, name) ||
		    is_remove_reloc_sec(ecp, sh.sh_info))
			continue;

		/* Skip if it's not for .symtab */
		if (sh.sh_link != elf_ndxscn(ecp->symtab->is))
			continue;

		d = NULL;
		n = 0;
		while (n < sh.sh_size && (d = elf_getdata(s, d)) != NULL) {
			len = d->d_size / sh.sh_entsize;
			for (i = 0; i < len; i++) {
				if (sh.sh_type == SHT_REL) {
					if (gelf_getrel(d, i, &r) != &r)
						errx(EX_SOFTWARE,
						    "elf_getrel failed: %s",
						     elf_errmsg(-1));
					n = GELF_R_SYM(r.r_info);
				} else {
					if (gelf_getrela(d, i, &ra) != &ra)
						errx(EX_SOFTWARE,
						    "elf_getrela failed: %s",
						     elf_errmsg(-1));
					n = GELF_R_SYM(ra.r_info);
				}
				if (n > 0 && n < sc)
					BIT_SET(ecp->v_rel, n);
				else if (n != 0)
					warnx("invalid symbox index");
			}
		}
		elferr = elf_errno();
		if (elferr != 0)
			errx(EX_SOFTWARE, "elf_getdata failed: %s",
			    elf_errmsg(elferr));
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EX_SOFTWARE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
}

static int
generate_symbols(struct elfcopy *ecp)
{
	struct section	*s;
	struct symop	*sp;
	struct symbuf	*sy_buf;
	struct strbuf	*st_buf;
	const char	*name;
	unsigned char	*gsym;
	GElf_Shdr	 ish;
	GElf_Sym	 sym;
	Elf_Data*	 id;
	Elf_Scn		*is;
	size_t		 ishstrndx, ndx, nsyms, sc, symndx;
	size_t		 gsy_cap, lsy_cap;
	size_t		 gst_cap, lst_cap;
	int		 ec, elferr, i, pos;

	if (elf_getshstrndx(ecp->ein, &ishstrndx) == 0)
		errx(EX_SOFTWARE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));
	if ((ec = gelf_getclass(ecp->eout)) == ELFCLASSNONE)
		errx(EX_SOFTWARE, "gelf_getclass failed: %s",
		    elf_errmsg(-1));

	/* Create buffers for .symtab and .strtab. */
	if ((sy_buf = calloc(1, sizeof(*sy_buf))) == NULL)
		err(EX_SOFTWARE, "calloc failed");
	if ((st_buf = calloc(1, sizeof(*st_buf))) == NULL)
		err(EX_SOFTWARE, "calloc failed");
	nsyms = 0;
	gsy_cap = lsy_cap = 64;
	gst_cap = 256;
	lst_cap = 64;
	st_buf->lsz = 1;	/* '\0' at start. */
	st_buf->gsz = 0;

	/*
	 * Create bit vector v_secsym, which is used to mark sections
	 * that already have corresponding STT_SECTION symbols.
	 */
	ecp->v_secsym = calloc((ecp->nos + 7) / 8, 1);
	if (ecp->v_secsym == NULL)
		err(EX_SOFTWARE, "calloc failed");

	/* Locate .strtab of input object. */
	symndx = 0;
	name = NULL;
	is = NULL;
	while ((is = elf_nextscn(ecp->ein, is)) != NULL) {
		if (gelf_getshdr(is, &ish) != &ish)
			errx(EX_SOFTWARE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, ishstrndx, ish.sh_name)) ==
		    NULL)
			errx(EX_SOFTWARE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (strcmp(name, ".strtab") == 0) {
			symndx = elf_ndxscn(is);
			break;
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EX_SOFTWARE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
	/* FIXME don't panic if can't find .strtab */
	if (symndx == 0)
		errx(EX_DATAERR, "can't find .strtab section");

	/* Locate .symtab of input object. */
	is = NULL;
	while ((is = elf_nextscn(ecp->ein, is)) != NULL) {
		if (gelf_getshdr(is, &ish) != &ish)
			errx(EX_SOFTWARE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, ishstrndx, ish.sh_name)) ==
		    NULL)
			errx(EX_SOFTWARE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (strcmp(name, ".symtab") == 0)
			break;
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EX_SOFTWARE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
	if (is == NULL)
		errx(EX_DATAERR, "can't find .strtab section");

	/*
	 * Convenient macro for copying global/local 32/64 bit symbols
	 * from input object to the buffer created for output object.
	 * It handles buffer growing, st_name calculating and st_shndx
	 * updating for symbols with non-special section index.
	 */
#define	COPYSYM(B, SZ, NDX) do {					\
	if (sy_buf->B##SZ == NULL) {					\
		sy_buf->B##SZ = malloc(B##sy_cap *			\
		    sizeof(Elf##SZ##_Sym));				\
		if (sy_buf->B##SZ == NULL)				\
			err(EX_SOFTWARE, "malloc failed");		\
	} else if (sy_buf->n##B##s >= B##sy_cap) {			\
		B##sy_cap *= 2;						\
		sy_buf->B##SZ = realloc(sy_buf->B##SZ, B##sy_cap *	\
		    sizeof(Elf##SZ##_Sym));				\
		if (sy_buf->B##SZ == NULL)				\
			err(EX_SOFTWARE, "realloc failed");		\
	}								\
	sy_buf->B##SZ[sy_buf->n##B##s].st_info	= sym.st_info;		\
	sy_buf->B##SZ[sy_buf->n##B##s].st_other	= sym.st_other;		\
	sy_buf->B##SZ[sy_buf->n##B##s].st_value	= sym.st_value;		\
	sy_buf->B##SZ[sy_buf->n##B##s].st_size	= sym.st_size;		\
	if ((NDX))							\
		sy_buf->B##SZ[sy_buf->n##B##s].st_shndx = (NDX);	\
	else if (sym.st_shndx == SHN_UNDEF ||				\
	    sym.st_shndx >= SHN_LORESERVE)				\
		sy_buf->B##SZ[sy_buf->n##B##s].st_shndx = sym.st_shndx;	\
	else								\
		sy_buf->B##SZ[sy_buf->n##B##s].st_shndx	=		\
			ecp->secndx[sym.st_shndx];			\
	if (st_buf->B == NULL) {					\
		st_buf->B = calloc(B##st_cap, sizeof(*st_buf->B));	\
		if (st_buf->B == NULL)					\
			err(EX_SOFTWARE, "malloc failed");		\
	}								\
	if (sym.st_name != 0 && *name != '\0') {			\
		pos = lookup_exact_string(st_buf->B,			\
		    st_buf->B##sz, name);				\
		if (pos != -1)						\
			sy_buf->B##SZ[sy_buf->n##B##s].st_name = pos;	\
		else {							\
			sy_buf->B##SZ[sy_buf->n##B##s].st_name =	\
				st_buf->B##sz;				\
			while (st_buf->B##sz + strlen(name) >=		\
			    B##st_cap - 1) {				\
				B##st_cap *= 2;				\
				st_buf->B = realloc(st_buf->B,		\
				    B##st_cap);				\
				if (st_buf->B == NULL)			\
					err(EX_SOFTWARE,		\
					    "realloc failed");		\
			}						\
			strncpy(&st_buf->B[st_buf->B##sz], name,	\
			    strlen(name));				\
			st_buf->B[st_buf->B##sz + strlen(name)] = '\0';	\
			st_buf->B##sz += strlen(name) + 1;		\
		}							\
	} else								\
		sy_buf->B##SZ[sy_buf->n##B##s].st_name = 0;		\
	sy_buf->n##B##s++;						\
} while (0)

	/*
	 * Create bit vector gsym to mark global symbols, and symndx
	 * to keep track of symbol index changes from input object to
	 * output object, it is used by update_reloc() later to update
	 * relocation information.
	 */
	gsym = NULL;
	sc = ish.sh_size / ish.sh_entsize;
	if (sc > 0) {
		ecp->symndx = calloc(sc, sizeof(*ecp->symndx));
		if (ecp->symndx == NULL)
			err(EX_SOFTWARE, "calloc failed");
		gsym = calloc((sc + 7) / 8, sizeof(*gsym));
		if (gsym == NULL)
			err(EX_SOFTWARE, "calloc failed");
		if ((id = elf_getdata(is, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				errx(EX_SOFTWARE, "elf_getdata failed: %s",
				    elf_errmsg(elferr));
			return (0);
		}
	} else
		return (0);

	/* Copy/Filter each symbol. */
	for (i = 0; (size_t)i < sc; i++) {
		if (gelf_getsym(id, i, &sym) != &sym)
			errx(EX_SOFTWARE, "gelf_getsym failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, symndx,
			    sym.st_name)) == NULL)
			errx(EX_SOFTWARE, "elf_strptr failed: %s",
			    elf_errmsg(-1));

		/* Symbol filtering. */
		if (is_remove_symbol(ecp, sc, i, &sym, name) != 0)
			continue;

		/* Check if we need to change the binding of this symbol. */
		if (is_global_symbol(&sym) || is_weak_symbol(&sym)) {
			/*
			 * XXX Binutils objcopy does not weaken certain
			 * symbols.
			 */
			if (ecp->flags & WEAKEN_ALL ||
			    lookup_symop_list(ecp, name, SYMOP_WEAKEN) != NULL)
				sym.st_info = GELF_ST_INFO(STB_WEAK,
				    GELF_ST_TYPE(sym.st_info));
			/* Do not localize undefined symbols. */
			if (sym.st_shndx != SHN_UNDEF &&
			    lookup_symop_list(ecp, name, SYMOP_LOCALIZE) !=
			    NULL)
				sym.st_info = GELF_ST_INFO(STB_LOCAL,
				    GELF_ST_TYPE(sym.st_info));
			if (ecp->flags & KEEP_GLOBAL &&
			    sym.st_shndx != SHN_UNDEF &&
			    lookup_symop_list(ecp, name, SYMOP_KEEPG) == NULL)
				sym.st_info = GELF_ST_INFO(STB_LOCAL,
				    GELF_ST_TYPE(sym.st_info));
		} else {
			/* STB_LOCAL binding. */
			if (lookup_symop_list(ecp, name, SYMOP_GLOBALIZE) !=
			    NULL)
				sym.st_info = GELF_ST_INFO(STB_GLOBAL,
				    GELF_ST_TYPE(sym.st_info));
			/* XXX We should globalize weak symbol? */
		}

		/* Check if we need to rename this symbol. */
		if ((sp = lookup_symop_list(ecp, name, SYMOP_REDEF)) != NULL)
			name = sp->newname;

		/* Copy symbol, mark global/weak symbol and add to index map. */
		if (is_global_symbol(&sym) || is_weak_symbol(&sym)) {
			BIT_SET(gsym, i);
			ecp->symndx[i] = sy_buf->ngs;
		} else
			ecp->symndx[i] = sy_buf->nls;
		if (ec == ELFCLASS32) {
			if (is_local_symbol(&sym))
				COPYSYM(l, 32, 0);
			else
				COPYSYM(g, 32, 0);
		} else {
			if (is_local_symbol(&sym))
				COPYSYM(l, 64, 0);
			else
				COPYSYM(g, 64, 0);
		}

		/*
		 * If the symbol is a STT_SECTION symbol, mark the section
		 * it points to.
		 */
		if (GELF_ST_TYPE(sym.st_info) == STT_SECTION)
			BIT_SET(ecp->v_secsym, ecp->secndx[sym.st_shndx]);
	}


	/*
	 * Give up if there is no real symbols inside the table.
	 * XXX The logic here needs to be improved. We need to
	 * check if that only local symbol is the reserved symbol.
	 */
	if (sy_buf->nls <= 1 && sy_buf->ngs == 0)
		return (0);

	/*
	 * Create STT_SECTION symbols for sections that do not already
	 * got one. However, we do not create STT_SECTION symbol for
	 * .symtab, .strtab, .shstrtab and reloc sec of relocatables.
	 */
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (strcmp(s->name, ".symtab") == 0 ||
		    strcmp(s->name, ".strtab") == 0 ||
		    strcmp(s->name, ".shstrtab") == 0)
			continue;
		if ((ecp->flags & RELOCATABLE) != 0 &&
		    ((s->type == SHT_REL) || (s->type == SHT_RELA)))
			continue;

		if ((ndx = elf_ndxscn(s->os)) == SHN_UNDEF)
			errx(EX_SOFTWARE, "elf_ndxscn failed: %s",
			    elf_errmsg(-1));
		
		if (!BIT_ISSET(ecp->v_secsym, ndx)) {
			sym.st_name	= 0;
			sym.st_value	= s->vma;
			sym.st_size	= 0;
			sym.st_info	= GELF_ST_INFO(STB_LOCAL, STT_SECTION);
			/*
			 * Don't let COPYSYM touch sym.st_shndx. In this case,
			 * we know the index already.
			 */
			if (ec == ELFCLASS32)
				COPYSYM(l, 32, ndx);
			else
				COPYSYM(l, 64, ndx);
		}
	}

	/*
	 * Update st_name and index map for global/weak symbols. Note that
	 * global/weak symbols are put after local symbols.
	 */
	if (gsym != NULL) {
		for(i = 0; (size_t)i < sc; i++) {
			if (!BIT_ISSET(gsym, i))
				continue;

			/* Update st_name. */
			if (ec == ELFCLASS32)
				sy_buf->g32[ecp->symndx[i]].st_name +=
				    st_buf->lsz;
			else
				sy_buf->g64[ecp->symndx[i]].st_name +=
				    st_buf->lsz;

			/* Update index map. */
			ecp->symndx[i] += sy_buf->nls;
		}
		free(gsym);
	}

	/*
	 * Store symtab and strtab buffers in the global ecp structure for
	 * later use.
	 */
	ecp->symtab->sz = (sy_buf->nls + sy_buf->ngs) *
	    (ec == ELFCLASS32 ? sizeof(Elf32_Sym) : sizeof(Elf64_Sym));
	ecp->symtab->buf = sy_buf;
	ecp->strtab->sz = st_buf->lsz + st_buf->gsz;
	ecp->strtab->buf = st_buf;

	return (1);
}

void
create_symtab(struct elfcopy *ecp)
{
	struct section	*s, *sy, *st;
	struct symbuf	*sy_buf;
	struct strbuf	*st_buf;
	Elf_Data	*gsydata, *lsydata, *gstdata, *lstdata;
	GElf_Shdr	 shy, sht;
	size_t		 maxndx, ndx;

	sy = ecp->symtab;
	st = ecp->strtab;

	/*
	 * Set section index map for .symtab and .strtab. We need to set
	 * these map because otherwise symbols which refer to .symtab and
	 * .strtab will be removed by symbol filtering unconditionally. 
	 * And we have to figure out scn index this way (instead of calling
	 * elf_ndxscn) because we can not create Elf_Scn before we're certain
	 * that .symtab and .strtab will exist in the output object.
	 */
	maxndx = 0;
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (s->os == NULL)
			continue;
		if ((ndx = elf_ndxscn(s->os)) == SHN_UNDEF)
			errx(EX_SOFTWARE, "elf_ndxscn failed: %s",
			    elf_errmsg(-1));
		if (ndx > maxndx)
			maxndx = ndx;
	}
	ecp->secndx[elf_ndxscn(sy->is)] = maxndx + 1;
	ecp->secndx[elf_ndxscn(st->is)] = maxndx + 2;
	
	/*
	 * Generate symbols for output object if SYMTAB_INTACT is not set.
	 * If there is no symbol in the input object or all the symbols are
	 * stripped, then free all the resouces allotted for symbol table,
	 * and clear SYMTAB_EXIST flag.
	 */
	if (((ecp->flags & SYMTAB_INTACT) == 0) && !generate_symbols(ecp)) {
		TAILQ_REMOVE(&ecp->v_sec, ecp->symtab, sec_list);
		TAILQ_REMOVE(&ecp->v_sec, ecp->strtab, sec_list);
		free(ecp->symtab);
		free(ecp->strtab);
		ecp->flags &= ~SYMTAB_EXIST;
		return;
	}

	/* Create output Elf_Scn for .symtab and .strtab. */
	if ((sy->os = elf_newscn(ecp->eout)) == NULL ||
	    (st->os = elf_newscn(ecp->eout)) == NULL)
		errx(EX_SOFTWARE, "elf_newscn failed: %s",
		    elf_errmsg(-1));
	/* Update secndx anyway. */
	ecp->secndx[elf_ndxscn(sy->is)] = elf_ndxscn(sy->os);
	ecp->secndx[elf_ndxscn(st->is)] = elf_ndxscn(st->os);

	/*
	 * Copy .symtab and .strtab section headers from input to output
	 * object to start with, these will be overridden later if need.
	 */
	copy_shdr(ecp, sy, ".symtab", 1, 0);
	copy_shdr(ecp, st, ".strtab", 1, 0);

	if (gelf_getshdr(sy->os, &shy) == NULL)
		errx(EX_SOFTWARE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	if (gelf_getshdr(st->os, &sht) == NULL)
		errx(EX_SOFTWARE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));

	/* Copy verbatim if symbol table is intact. */
	if (ecp->flags & SYMTAB_INTACT) {
		copy_data(sy);
		copy_data(st);
		return;
	}

	/*
	 * Create two Elf_Data for .symtab section of output object, one
	 * for local symbols and another for global symbols. Note that
	 * local symbols appear first in the .symtab.
	 */
	sy_buf = sy->buf;
	if (sy_buf->nls > 0) {
		if ((lsydata = elf_newdata(sy->os)) == NULL)
			errx(EX_SOFTWARE, "elf_newdata() failed: %s.",
			     elf_errmsg(-1));
		if (ecp->oec == ELFCLASS32) {
			lsydata->d_align	= 4;
			lsydata->d_off		= 0;
			lsydata->d_buf		= sy_buf->l32;
			lsydata->d_size		= sy_buf->nls *
				sizeof(Elf32_Sym);
			lsydata->d_type		= ELF_T_SYM;
			lsydata->d_version	= EV_CURRENT;
		} else {
			lsydata->d_align	= 8;
			lsydata->d_off		= 0;
			lsydata->d_buf		= sy_buf->l64;
			lsydata->d_size		= sy_buf->nls *
				sizeof(Elf64_Sym);
			lsydata->d_type		= ELF_T_SYM;
			lsydata->d_version	= EV_CURRENT;
		}
	}
	if (sy_buf->ngs > 0) {
		if ((gsydata = elf_newdata(sy->os)) == NULL)
			errx(EX_SOFTWARE, "elf_newdata() failed: %s.",
			     elf_errmsg(-1));
		if (ecp->oec == ELFCLASS32) {
			gsydata->d_align	= 4;
			gsydata->d_off		= sy_buf->nls *
				sizeof(Elf32_Sym);
			gsydata->d_buf		= sy_buf->g32;
			gsydata->d_size		= sy_buf->ngs *
				sizeof(Elf32_Sym);
			gsydata->d_type		= ELF_T_SYM;
			gsydata->d_version	= EV_CURRENT;
		} else {
			gsydata->d_align	= 8;
			gsydata->d_off		= sy_buf->nls *
				sizeof(Elf64_Sym);
			gsydata->d_buf		= sy_buf->g64;
			gsydata->d_size		= sy_buf->ngs *
				sizeof(Elf64_Sym);
			gsydata->d_type		= ELF_T_SYM;
			gsydata->d_version	= EV_CURRENT;
		}
	}

	/*
	 * Create two Elf_Data for .strtab, one for local symbol name
	 * and another for globals. Same as .symtab, local symbol names
	 * appear first.
	 */
	st_buf = st->buf;
	if ((lstdata = elf_newdata(st->os)) == NULL)
		errx(EX_SOFTWARE, "elf_newdata() failed: %s.",
		    elf_errmsg(-1));
	lstdata->d_align	= 1;
	lstdata->d_off		= 0;
	lstdata->d_buf		= st_buf->l;
	lstdata->d_size		= st_buf->lsz;
	lstdata->d_type		= ELF_T_BYTE;
	lstdata->d_version	= EV_CURRENT;

	if (st_buf->gsz > 0) {
		if ((gstdata = elf_newdata(st->os)) == NULL)
			errx(EX_SOFTWARE, "elf_newdata() failed: %s.",
			    elf_errmsg(-1));
		gstdata->d_align	= 1;
		gstdata->d_off		= lstdata->d_size;
		gstdata->d_buf		= st_buf->g;
		gstdata->d_size		= st_buf->gsz;
		gstdata->d_type		= ELF_T_BYTE;
		gstdata->d_version	= EV_CURRENT;
	}

	shy.sh_addr		= 0;
	shy.sh_addralign	= (ecp->oec == ELFCLASS32 ? 4 : 8);
	shy.sh_size		= sy->sz;
	shy.sh_type		= SHT_SYMTAB;
	shy.sh_flags		= 0;
	shy.sh_entsize		= gelf_fsize(ecp->eout, ELF_T_SYM, 1,
	    EV_CURRENT);
	/*
	 * According to SYSV abi, here sh_info is one greater than
	 * the symbol table index of the last local symbol(binding
	 * STB_LOCAL).
	 */
	shy.sh_info		= sy_buf->nls;

	sht.sh_addr		= 0;
	sht.sh_addralign	= 1;
	sht.sh_size		= st->sz;
	sht.sh_type		= SHT_STRTAB;
	sht.sh_flags		= 0;
	sht.sh_entsize		= 0;
	sht.sh_info		= 0;
	sht.sh_link		= 0;

	if (!gelf_update_shdr(sy->os, &shy))
		errx(EX_SOFTWARE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));
	if (!gelf_update_shdr(st->os, &sht))
		errx(EX_SOFTWARE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));
}

void
add_to_symop_list(struct elfcopy *ecp, const char *name, const char *newname,
    unsigned int op)
{
	struct symop *s;

	if ((s = lookup_symop_list(ecp, name, ~0U)) == NULL) {
		if ((s = calloc(1, sizeof(*s))) == NULL)
			errx(EX_SOFTWARE, "not enough memory");
		s->name = name;
		if (op == SYMOP_REDEF)
			s->newname = newname;
	}

	s->op |= op;
	STAILQ_INSERT_TAIL(&ecp->v_symop, s, symop_list);
}

struct symop *
lookup_symop_list(struct elfcopy *ecp, const char *name, unsigned int op)
{
	struct symop *s;

	STAILQ_FOREACH(s, &ecp->v_symop, symop_list) {
		if (name == NULL || strcmp(name, s->name) == 0)
			if ((s->op & op) != 0)
				return (s);
	}

	return (NULL);
}

static int
lookup_exact_string(const char *buf, size_t sz, const char *s)
{
	const char	*b;
	size_t		 slen;

	slen = strlen(s);
	for (b = buf; b < buf + sz; b += strlen(b) + 1) {
		if (strlen(b) != slen)
			continue;
		if (!strcmp(b, s))
			return (b - buf);
	}

	return (-1);
}