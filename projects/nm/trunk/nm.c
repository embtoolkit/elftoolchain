/*-
 * Copyright (c) 2007 Hyogeol Lee <hyogeollee@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <ar.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <getopt.h>
#include <inttypes.h>
#include <libelf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "cpp_demangle.h"
#include "cpp_demangle_arm.h"
#include "cpp_demangle_gnu2.h"
#include "dwarf_line_number.h"

/* symbol information list */
STAILQ_HEAD(sym_head, sym_entry);

struct sym_entry {
	char		*name;
	GElf_Sym	*sym;
	STAILQ_ENTRY(sym_entry) sym_entries;
};

typedef int (*fn_sort)(const void *, const void *);
typedef void (*fn_elem_print)(char, const char *, const GElf_Sym *, const char *);
typedef void (*fn_sym_print)(const GElf_Sym *);
typedef int (*fn_filter)(char, const GElf_Sym *, const char *);

/* output filter list */
SLIST_HEAD(filter_head, filter_entry) nm_out_filter =
SLIST_HEAD_INITIALIZER(nm_out_filter);

struct filter_entry {
	fn_filter	fn;
	SLIST_ENTRY(filter_entry) filter_entries;
};

struct sym_print_data {
	struct sym_head	*headp;
	size_t		sh_num, list_num;
	const char	*t_table, **s_table, *filename, *objname;
};

struct nm_prog_info {
	const char	*name;
	const char	*version;
	const char	*def_filename;
};

/* output numric type */
enum radix {
	RADIX_DEFAULT, RADIX_OCT, RADIX_HEX, RADIX_DEC
};

/* output symbol type, PRINT_SYM_DYN for dynamic symbol only */
enum print_symbol {
	PRINT_SYM_SYM, PRINT_SYM_DYN
};

/* output name type */
enum print_name {
	PRINT_NAME_NONE, PRINT_NAME_FULL, PRINT_NAME_MULTI
};

enum demangle {
	DEMANGLE_NONE, DEMANGLE_AUTO, DEMANGLE_GV2, DEMANGLE_GV3, DEMANGLE_ARM
};

struct nm_prog_options {
	enum print_symbol	print_symbol;
	enum print_name		print_name;
	enum demangle		demangle_type;
	bool			print_debug;
	bool			print_armap;
	int			print_size;
	bool			debug_line;
	int			def_only;
	bool			undef_only;
	int			sort_size;
	bool			sort_reverse;
	int			no_demangle;

	/*
	 * function pointer to sort symbol list.
	 * possible function - cmp_name, cmp_none, cmp_size, cmp_value
	 */
	fn_sort			sort_fn;

	/*
	 * function pointer to print symbol elem.
	 * possible function - sym_elem_print_all
	 *		       sym_elem_print_all_portable
	 *		       sym_elem_print_all_sysv
	 */
	fn_elem_print		elem_print_fn;

	fn_sym_print		value_print_fn;
	fn_sym_print		size_print_fn;
};

#define	CHECK_SYM_PRINT_DATA(p)	(p->headp == NULL || p->sh_num == 0 ||	      \
p->t_table == NULL || p->s_table == NULL || p->filename == NULL)
#define	IS_SYM_TYPE(t)		(t == '?' || isalpha(t) != 0)
#define	IS_UNDEF_SYM_TYPE(t)	(t == 'U' || t == 'v' || t == 'w')
#define	IS_COM_SYM(s)		(s->st_shndx == SHN_COMMON)
#define	FASTLOWER(t)		(t + 32)
#define	STDLOWER(t)		(tolower(t))
#define	TOLOWER(t)		(STDLOWER(t))
#define	SLIST_HINIT_AFTER(l)	{l->slh_first = NULL;}
#define	STAILQ_HINIT_AFTER(l)	{l.stqh_first = NULL; \
l.stqh_last = &(l).stqh_first;}
#define	UNUSED(p)		((void)p)

static int		cmp_name(const void *, const void *);
static int		cmp_none(const void *, const void *);
static int		cmp_size(const void *, const void *);
static int		cmp_value(const void *, const void *);
static void		filter_dest(void);
static int		filter_insert(fn_filter);
static enum demangle	get_demangle_type(const char *);
static enum demangle	get_demangle_option(const char *);
static int		get_sym(Elf *, struct sym_head *, int,
			    const Elf_Data *, const Elf_Data *, const char *);
static char		get_sym_type(const GElf_Sym *, const char *);
static void		global_init(void);
static bool		is_file(const char *);
static bool		is_sec_data(GElf_Shdr *);
static bool		is_sec_debug(const char *);
static bool		is_sec_nobits(GElf_Shdr *);
static bool		is_sec_readonly(GElf_Shdr *);
static bool		is_sec_text(GElf_Shdr *);
static void		print_ar_index(int fd, Elf *);
static void		print_demangle_name(const char *, const char *);
static void		print_header(const char *, const char *);
static void		print_version(void);
static int		read_elf(const char *);
static unsigned char	*relocate_sec(Elf_Data *, Elf_Data *, int);
static struct line_info_entry	*search_addr(struct line_info_head *, GElf_Sym *);
static void		set_opt_value_print_fn(enum radix);
static int		sym_elem_def(char, const GElf_Sym *, const char *);
static int		sym_elem_global(char, const GElf_Sym *, const char *);
static int		sym_elem_global_static(char, const GElf_Sym *,
			    const char *);
static int		sym_elem_nondebug(char, const GElf_Sym *, const char *);
static int		sym_elem_nonzero_size(char, const GElf_Sym *,
			    const char *);
static void		sym_elem_print_all(char, const char *,
			    const GElf_Sym *, const char *);
static void		sym_elem_print_all_portable(char, const char *,
			    const GElf_Sym *, const char *);
static void		sym_elem_print_all_sysv(char, const char *,
			    const GElf_Sym *, const char *);
static int		sym_elem_undef(char, const GElf_Sym *, const char *);
static void		sym_list_dest(struct sym_head *);
static int		sym_list_insert(struct sym_head *, const char *,
			    const GElf_Sym *);
static void		sym_list_print(struct sym_print_data *,
			    struct line_info_head *);
static void		sym_list_print_each(struct sym_entry *,
			    struct sym_print_data *, struct line_info_head *);
static struct sym_entry	*sym_list_sort(struct sym_print_data *);
static int		sym_section_filter(const GElf_Shdr *);
static void		sym_size_oct_print(const GElf_Sym *);
static void		sym_size_hex_print(const GElf_Sym *);
static void		sym_size_dec_print(const GElf_Sym *);
static void		sym_value_oct_print(const GElf_Sym *);
static void		sym_value_hex_print(const GElf_Sym *);
static void		sym_value_dec_print(const GElf_Sym *);
static void		usage(int);

struct nm_prog_info	nm_info;
struct nm_prog_options	nm_opts;

/*
 * Point to current sym_print_data to use portable qsort function.
 *  (e.g. There is no qsort_r function in NetBSD.)
 *
 * Using in sym_list_sort.
 */
struct sym_print_data	*nm_print_data;

static const struct option nm_longopts[] = {
	{ "debug-syms",		no_argument,		NULL,		'a'},
	{ "defined-only",	no_argument,		&nm_opts.def_only, 1 },
	{ "demangle",		optional_argument,	NULL,		'C' },
	{ "dynamic",		no_argument,		NULL,		'D' },
	{ "format",		required_argument,	NULL,		'F' },
	{ "help",		no_argument,		NULL,		'h' },
	{ "line-numbers",	no_argument,		NULL,		'l' },
	{ "no-demangle",	no_argument,		&nm_opts.no_demangle,
	  1 },
	{ "no-sort",		no_argument,		NULL,		'p' },
	{ "numeric-sort",	no_argument,		NULL,		'v' },
	{ "print-armap",	no_argument,		NULL,		's' },
	{ "print-file-name",	no_argument,		NULL,		'A' },
	{ "print-size",		no_argument,		NULL,		'S' },
	{ "radix",		required_argument,	NULL,		't' },
	{ "reverse-sort",	no_argument,		NULL,		'r' },
	{ "size-sort",		no_argument,		&nm_opts.sort_size, 1 },
	{ "undefined-only",	no_argument,		NULL,		'u' },
	{ "version",		no_argument,		NULL,		'V' },
	{ NULL,			0,			NULL,		0   }
};

static int
cmp_name(const void *l, const void *r)
{

	assert(l != NULL);
	assert(r != NULL);
	assert(((const struct sym_entry *)l)->name != NULL);
	assert(((const struct sym_entry *)r)->name != NULL);

	return (strcmp(((const struct sym_entry *)l)->name,
		((const struct sym_entry *)r)->name));
}

static int
cmp_none(const void *l, const void *r)
{

	UNUSED(l);
	UNUSED(r);

	return (0);
}

/* Size comparison. If l and r have same size, compare their name. */
static int
cmp_size(const void *lp, const void *rp)
{
	const struct sym_entry *l, *r;

	l = lp;
	r = rp;

	assert(l != NULL);
	assert(l->name != NULL);
	assert(l->sym != NULL);
	assert(r != NULL);
	assert(r->name != NULL);
	assert(r->sym != NULL);

	if (l->sym->st_size == r->sym->st_size)
		return (strcmp(l->name, r->name));

	return (l->sym->st_size - r->sym->st_size);
}

/* Value comparison. Undefined symbols come first. */
static int
cmp_value(const void *lp, const void *rp)
{
	const struct sym_entry *l, *r;
	int l_is_undef, r_is_undef;
	const char *ttable;

	l = lp;
	r = rp;

	assert(nm_print_data != NULL);
	ttable = nm_print_data->t_table;

	assert(l != NULL);
	assert(l->name != NULL);
	assert(l->sym != NULL);
	assert(r != NULL);
	assert(r->name != NULL);
	assert(r->sym != NULL);
	assert(ttable != NULL);

	l_is_undef = IS_UNDEF_SYM_TYPE(get_sym_type(l->sym, ttable)) ? 1 : 0;
	r_is_undef = IS_UNDEF_SYM_TYPE(get_sym_type(r->sym, ttable)) ? 1 : 0;

	assert(l_is_undef + r_is_undef >= 0);
	assert(l_is_undef + r_is_undef <= 2);

	switch (l_is_undef + r_is_undef) {
	case 0:
		/* Both defined */
		if (l->sym->st_value == r->sym->st_value)
			return (strcmp(l->name, r->name));

		return (l->sym->st_value - r->sym->st_value);
	case 1:
		/* One undefined */
		return (l_is_undef == 0 ? 1 : -1);
	case 2:
		/* Both undefined */
		return (strcmp(l->name, r->name));
	}
	/* NOTREACHED */

	return (l->sym->st_value - r->sym->st_value);
}

static void
filter_dest(void)
{
	struct filter_entry *e;

	while (!SLIST_EMPTY(&nm_out_filter)) {
		e = SLIST_FIRST(&nm_out_filter);
		SLIST_REMOVE_HEAD(&nm_out_filter, filter_entries);
		free(e);
	}
}

static int
filter_insert(fn_filter filter_fn)
{
	struct filter_entry *e;

	assert(filter_fn != NULL);

	if ((e = malloc(sizeof(struct filter_entry))) == NULL)
		return (0);

	e->fn = filter_fn;

	SLIST_INSERT_HEAD(&nm_out_filter, e, filter_entries);

	return (1);
}

static enum demangle
get_demangle_type(const char *org)
{

	if (org == NULL)
		return (DEMANGLE_NONE);

	if (is_cpp_mangled_ia64(org))
		return (DEMANGLE_GV3);

	if (is_cpp_mangled_gnu2(org))
		return (DEMANGLE_GV2);

	if (is_cpp_mangled_ARM(org))
		return (DEMANGLE_ARM);

	return (DEMANGLE_NONE);
}

static enum demangle
get_demangle_option(const char *opt)
{

	if (opt == NULL)
		return (DEMANGLE_AUTO);

	if (strncasecmp(opt, "gnu-v2", 6) == 0)
		return (DEMANGLE_GV2);

	if (strncasecmp(opt, "gnu-v3", 6) == 0)
		return (DEMANGLE_GV3);

	if (strncasecmp(opt, "arm", 3) == 0)
		return (DEMANGLE_ARM);

	errx(EX_USAGE, "unknown demangling style '%s'", opt);

	/* NOTREACHED */
	return (DEMANGLE_NONE);
}

/*
 * Get symbol information from elf.
 * param shnum Total section header number(ehdr.e_shnum).
 */
static int
get_sym(Elf *elf, struct sym_head *headp, int shnum,
    const Elf_Data *dynstr_data, const Elf_Data *strtab_data,
    const char *type_table)
{
	Elf_Scn *scn;
	Elf_Data *data;
	const Elf_Data *table;
	GElf_Shdr shdr;
	GElf_Sym sym;
	struct filter_entry *fep;
	int rtn;
	const char *sym_name;
	char type;
	bool filter;

	assert(elf != NULL);
	assert(headp != NULL);

	rtn = 0;
	for (int i = 1; i < shnum; ++i) {
		if ((scn = elf_getscn(elf, i)) == NULL)
			return (0);

		if (gelf_getshdr(scn, &shdr) == NULL)
			return (0);

		if (sym_section_filter(&shdr) != 1)
			continue;

		if (shdr.sh_type == SHT_DYNSYM && dynstr_data != NULL)
			table = dynstr_data;
		else if (shdr.sh_type == SHT_SYMTAB && strtab_data != NULL)
			table = strtab_data;
		else
			table = NULL;

		data = NULL;
		while ((data = elf_getdata(scn, data)) != NULL) {
			int j = 1;
			while (gelf_getsym(data, j++, &sym) != NULL) {
				sym_name = table == NULL ? "(null)" :
				    (char *)((char *)(table->d_buf) +
					sym.st_name);

				filter = false;
				type = get_sym_type(&sym, type_table);
				SLIST_FOREACH(fep, &nm_out_filter,
				    filter_entries)
				    if (fep->fn(type, &sym, sym_name) == 0) {
					    filter = true;

					    break;
				    }

				if (filter == false) {
					if (sym_list_insert(headp, sym_name,
						&sym) == 0)
						return (0);

					++rtn;
				}
			}
		}
	}

	return (rtn);
}

static char
get_sym_type(const GElf_Sym *sym, const char *type_table)
{
	bool is_local;

	if (sym == NULL || type_table == NULL)
		return ('?');

	is_local = sym->st_info >> 4 == STB_LOCAL;

	if (sym->st_shndx == SHN_ABS) /* absolute */
		return (is_local ? 'a' : 'A');

	if (sym->st_shndx == SHN_COMMON) /* common */
		return ('C');

	if ((sym->st_info) >> 4 == STB_WEAK) { /* weak */
		if ((sym->st_info & 0xf) == STT_OBJECT)
			return (sym->st_shndx == SHN_UNDEF ? 'v' : 'V');

		return (sym->st_shndx == SHN_UNDEF ? 'w' : 'W');
	}

	if (sym->st_shndx == SHN_UNDEF) /* undefined */
		return ('U');

	return (is_local == true && type_table[sym->st_shndx] != 'N' ?
	    TOLOWER(type_table[sym->st_shndx]) :
	    type_table[sym->st_shndx]);
}

static void
global_init(void)
{

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(1, "elf_version error");

	nm_info.name = "nm";
	nm_info.version = "1.0";
	nm_info.def_filename = "a.out";

	nm_opts.print_symbol = PRINT_SYM_SYM;
	nm_opts.print_name = PRINT_NAME_NONE;
	nm_opts.demangle_type = DEMANGLE_NONE;
	nm_opts.print_debug = false;
	nm_opts.print_armap = false;
	nm_opts.print_size = 0;

	nm_opts.debug_line = false;

	nm_opts.def_only = 0;
	nm_opts.undef_only = false;

	nm_opts.sort_size = 0;
	nm_opts.sort_reverse = false;
	nm_opts.no_demangle = 0;

	nm_opts.sort_fn = &cmp_name;
	nm_opts.elem_print_fn = &sym_elem_print_all;
	nm_opts.value_print_fn = &sym_value_dec_print;
	nm_opts.size_print_fn = &sym_size_dec_print;

	SLIST_INIT(&nm_out_filter);
}

static bool
is_file(const char *path)
{
	struct stat sb;

	if (path == NULL)
		return (false);

	if (stat(path, &sb) != 0) {
		warnx("'%s': No such file", path);

		return (false);
	}

	if (!S_ISLNK(sb.st_mode) && !S_ISREG(sb.st_mode)) {
		warnx("Warning: '%s' is not an ordinary file", path);

		return (false);
	}

	return (true);
}

static bool
is_sec_data(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return (((s->sh_flags & SHF_ALLOC) != 0) && s->sh_type != SHT_NOBITS);
}

static bool
is_sec_debug(const char *shname)
{
	const char *dbg_sec[] = {
		".debug",
		".gnu.linkonce.wi.",
		".line",
		".stab",
		NULL
	};
	const char **p;

	assert(shname != NULL && "shname is NULL");

	for(p = dbg_sec; *p; p++)
		if (strncmp(shname, *p, strlen(*p)) == 0)
			return (true);

	return (false);
}

static bool
is_sec_nobits(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return (s->sh_type == SHT_NOBITS);
}

static bool
is_sec_readonly(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return ((s->sh_flags & SHF_WRITE) == 0);
}

static bool
is_sec_text(GElf_Shdr *s)
{

	assert(s != NULL && "shdr is NULL");

	return ((s->sh_flags & SHF_EXECINSTR) != 0);
}

static void
print_ar_index(int fd, Elf *arf)
{
	Elf_Arsym *arsym;
	Elf_Cmd cmd;
	off_t start;
	size_t arsym_size;

	if (arf == NULL)
		return;

	if ((arsym = elf_getarsym(arf, &arsym_size)) == NULL)
		return;

	printf("\nArchive index:\n");

	start = arsym->as_off;
	cmd = ELF_C_READ;
	while (arsym_size > 1) {
		Elf *elf;
		Elf_Arhdr *arhdr;

		if (elf_rand(arf, arsym->as_off) == arsym->as_off &&
		    (elf = elf_begin(fd, cmd, arf)) != NULL) {
			if ((arhdr = elf_getarhdr(elf)) != NULL)
				printf("%s in %s\n",
				    arsym->as_name,
				    arhdr->ar_name != NULL ?
				    arhdr->ar_name : arhdr->ar_rawname);

			elf_end(elf);
		}
		++arsym;
		--arsym_size;
	}

	elf_rand(arf, start);
}

static void
print_demangle_name(const char *format, const char *name)
{
	enum demangle d;
	char *demangle;

	if (format == NULL || name == NULL)
		return;

	d = nm_opts.demangle_type == DEMANGLE_AUTO ?
	    get_demangle_type(name) : nm_opts.demangle_type;

	switch (d) {
	case DEMANGLE_GV2:
		{
			if ((demangle = cpp_demangle_gnu2(name)) == NULL)
				demangle = cpp_demangle_ARM(name);

			printf(format, demangle == NULL ? name : demangle);

			free(demangle);
		}
		break;
	case DEMANGLE_GV3:
		{
			demangle = cpp_demangle_ia64(name);

			printf(format, demangle == NULL ? name : demangle);

			free(demangle);
		}
		break;
	case DEMANGLE_ARM:
		{
			demangle = cpp_demangle_ARM(name);

			printf(format, demangle == NULL ? name : demangle);

			free(demangle);
		}

                break;
	case DEMANGLE_AUTO:
		/* NOTREACHED */
		/* FALLTHROUGH */
	case DEMANGLE_NONE:
	default:
		printf(format, name);
	}
}

static void
print_header(const char *file, const char *obj)
{

	if (file == NULL)
		return;

	if (nm_opts.elem_print_fn == &sym_elem_print_all_sysv) {
		printf("\n\n%s from %s",
		    nm_opts.undef_only == false ? "Symbols" :
		    "Undefined symbols", file);

		if (obj != NULL)
			printf("[%s]", obj);

		printf(":\n\n");

		printf("\
Name                  Value           Class        Type         Size             Line  Section\n\n");
	} else {
		/* archive file without -A option and POSIX */
		if (nm_opts.print_name != PRINT_NAME_FULL && obj != NULL) {
			if (nm_opts.elem_print_fn ==
			    sym_elem_print_all_portable)
				printf("%s[%s]:\n", file, obj);
			else if (nm_opts.elem_print_fn == sym_elem_print_all)
				printf("\n%s:\n", obj);
			/* multiple files(not archive) without -A option */
		} else if (nm_opts.print_name == PRINT_NAME_MULTI) {
			if (nm_opts.elem_print_fn == sym_elem_print_all)
				printf("\n");

			printf("%s:\n", file);
		}
	}
}

static void
print_version(void)
{

	printf("%s %s\n", nm_info.name, nm_info.version);

	exit(EX_OK);
}

/*
 * return -1 at error. 0 at false, 1 at true.
 */
static int
sym_section_filter(const GElf_Shdr *shdr)
{

	if (shdr == NULL)
		return (-1);

	if (nm_opts.print_debug == false &&
	    shdr->sh_type == SHT_PROGBITS &&
	    shdr->sh_flags == 0)
		return (1);

	if (nm_opts.print_symbol == PRINT_SYM_SYM &&
	    shdr->sh_type == SHT_SYMTAB)
		return (1);

	if (nm_opts.print_symbol == PRINT_SYM_DYN &&
	    shdr->sh_type == SHT_DYNSYM)
		return (1);

	/* In manual page, SHT_GNU_versym is also symbol section */
	return (0);
}

/*
 * Read elf file and collect symbol information, sort them, print.
 * Return 1 at failed, 0 at success.
 */
static int
read_elf(const char *filename)
{
	Elf_Data *dbg_abbrev, *dbg_info, *dbg_line, *dbg_rela_info;
	Elf_Data *dbg_rela_line, *dbg_str, *dynstr_data, *strtab_data;
	GElf_Shdr shdr;
	Elf_Arhdr *arhdr;
	struct sym_print_data p_data;
	struct sym_head list_head;
	size_t strndx, shnum, dbg_info_size, dbg_str_size, dbg_line_size;
	struct comp_dir_head *comp_dir;
	struct line_info_head *line_info;
	Elf *arf, *elf;
	Elf_Scn *scn;
	Elf_Cmd elf_cmd;
	Elf_Kind kind;
	int fd, rtn, e_err;
	GElf_Half i;
	const char *shname, *objname;
	char *type_table, **sec_table;
	void *dbg_info_buf, *dbg_str_buf, *dbg_line_buf;

	assert(filename != NULL && "filename is null");

	if (is_file(filename) == false)
		return (1);

	if ((fd = open(filename, O_RDONLY)) == -1) {
		warn("'%s'", filename);

		return (1);
	}

	elf_cmd = ELF_C_READ;
	if ((arf = elf_begin(fd, elf_cmd, (Elf *) NULL)) == NULL) {
		if ((e_err = elf_errno()) != 0)
			warnx("elf_begin error : %s", elf_errmsg(e_err));
		else
			warnx("elf_begin error");

		close(fd);

		return (1);
	}

	assert(arf != NULL && "arf is null.");

	rtn = 0;

	if ((kind = elf_kind(arf)) == ELF_K_NONE) {
		warnx("%s: File format not recognized", filename);

		rtn = 1;

		goto end_read_elf;
	}

	if (kind == ELF_K_AR) {
		if (nm_opts.print_name == PRINT_NAME_MULTI &&
		    nm_opts.elem_print_fn == sym_elem_print_all)
			printf("\n%s:\n", filename);

		if (nm_opts.print_armap == true)
			print_ar_index(fd, arf);
	}

	objname = NULL;

	/* Instead of TAILQ_HEAD_INITIALIZER to avoid warning */
	STAILQ_HINIT_AFTER(list_head);

	while ((elf = elf_begin(fd, elf_cmd, arf)) != NULL) {
		dbg_abbrev = NULL;
		dbg_info = NULL;
		dbg_line = NULL;
		dbg_rela_info = NULL;
		dbg_rela_line = NULL;
		dbg_str = NULL;
		dynstr_data = NULL;
		strtab_data = NULL;
		comp_dir = NULL;
		line_info = NULL;
		type_table = NULL;
		sec_table = NULL;
		dbg_info_buf = NULL;
		dbg_str_buf = NULL;
		dbg_line_buf = NULL;

		if (kind == ELF_K_AR) {
			if ((arhdr = elf_getarhdr(elf)) == NULL)
				goto next_cmd;

			objname = arhdr->ar_name != NULL ?
			    arhdr->ar_name : arhdr->ar_rawname;
		}

		if (elf_getshnum(elf, &shnum) == 0) {
			if ((e_err = elf_errno()) != 0)
				warnx("%s: %s",
				    objname == NULL ? filename : objname,
				    elf_errmsg(e_err));
			else
				warnx("%s: cannot get section number",
				    objname == NULL ? filename : objname);

			rtn = 1;

			goto next_cmd;
		}

		if (shnum == 0) {
			warnx("%s: has no section", objname == NULL ?
			    filename : objname);

			rtn = 1;

			goto next_cmd;
		}

		if (elf_getshstrndx(elf, &strndx) == 0) {
			warnx("%s: cannot get str index", objname == NULL ?
			    filename : objname);

			rtn = 1;

			goto next_cmd;
		}

		/* type_table for type determine */
		if ((type_table = malloc(sizeof(char) * shnum)) == NULL) {
			warn("%s", objname == NULL ? filename : objname);

			rtn = 1;

			goto next_cmd;
		}

		/* sec_table for section name to display in sysv format */
		if ((sec_table = malloc(sizeof(char *) * shnum)) == NULL) {
			warn("%s", objname == NULL ? filename : objname);

			rtn = 1;

			goto next_cmd;
		}

		/*
		 * Need to set NULL separately to free safely when failed in
		 * loop and goto cleaning area.
		 */
		for (i = 1; i< shnum; ++i)
			sec_table[i] = NULL;

		type_table[0] = 'U';
		if ((sec_table[0] = strdup("*UND*")) == NULL)
			goto next_cmd;

		for (i = 1; i < shnum; ++i) {
			type_table[i] = 'U';

			if ((scn = elf_getscn(elf, i)) == NULL) {
				if ((e_err = elf_errno()) != 0)
					warnx("%s: %s",
					    objname == NULL ?
					    filename : objname,
					    elf_errmsg(e_err));
				else
					warnx("%s: cannot get section",
					    objname == NULL ?
					    filename : objname);

				rtn = 1;

				goto next_cmd;
			}

			if (gelf_getshdr(scn, &shdr) == NULL)
				goto next_cmd;

			/*
			 * cannot test by type and attribute for dynstr,
			 * strtab
			 */
			if ((shname = elf_strptr(elf, strndx,
				 (size_t)shdr.sh_name)) != NULL) {
				if ((sec_table[i] = strdup(shname)) == NULL)
					goto next_cmd;

				if (strncmp(shname, ".dynstr", 7) == 0) {
					if ((dynstr_data = elf_getdata(scn,
						 NULL)) == NULL)
						goto next_cmd;
				}

				if (strncmp(shname, ".strtab", 7) == 0) {
					if ((strtab_data = elf_getdata(scn,
						 NULL)) == NULL)
						goto next_cmd;
				}

				/* not in SysV special sections,
				 * but has .debug_ stuff in DWARF.
				 */
				if (nm_opts.debug_line == true) {
					if (strncmp(shname, ".debug_info",
						11) == 0) {
						if ((dbg_info =
							elf_getdata(scn, NULL))
						    == NULL)
							goto next_cmd;
					}

					if (strncmp(shname, ".rela.debug_info",
						16) == 0) {
						if ((dbg_rela_info =
							elf_getdata(scn, NULL))
						    == NULL)
							goto next_cmd;
					}

					if (strncmp(shname, ".debug_abbrev",
						11) == 0) {
						if ((dbg_abbrev =
							elf_getdata(scn, NULL))
						    == NULL)
							goto next_cmd;
					}

					if (strncmp(shname, ".debug_str", 11) ==
					    0) {
						if ((dbg_str =
							elf_getdata(scn, NULL))
						    == NULL)
							goto next_cmd;
					}

					if (strncmp(shname, ".debug_line",
						11) == 0) {
						if ((dbg_line =
							elf_getdata(scn, NULL))
						    == NULL)
							goto next_cmd;
					}

					if (strncmp(shname, ".rela.debug_line",
						16) == 0) {
						if ((dbg_rela_line =
							elf_getdata(scn, NULL))
						    == NULL)
							goto next_cmd;
					}
				}
			} else if ((sec_table[i] = strdup("*UND*")) == NULL)
				goto next_cmd;

			if (is_sec_text(&shdr) == true)
				type_table[i] = 'T';
			else if (is_sec_data(&shdr) == true) {
				if (is_sec_readonly(&shdr) == true)
					type_table[i] = 'R';
				else
					type_table[i] = 'D';
			} else if (is_sec_nobits(&shdr) == true)
				type_table[i] = 'B';
			else if (is_sec_debug(shname) == true)
				type_table[i] = 'N';
			else if (is_sec_readonly(&shdr) == true &&
			    is_sec_nobits(&shdr) == false )
				type_table[i] = 'n';
		}

		print_header(filename, objname);

		if ((dynstr_data == NULL &&
			nm_opts.print_symbol == PRINT_SYM_DYN) ||
		    (strtab_data == NULL &&
			nm_opts.print_symbol == PRINT_SYM_SYM)) {
			warnx("%s: No symbols", objname == NULL ?
			    filename : objname);

			/* this is not error case */

			goto next_cmd;
		}

		STAILQ_INIT(&list_head);

		if (nm_opts.debug_line == true && dbg_info != NULL &&
		    dbg_abbrev != NULL && dbg_line != NULL) {

			if ((comp_dir =
				malloc(sizeof(struct comp_dir_head)))
			    != NULL) {
				SLIST_HINIT_AFTER(comp_dir);
				SLIST_INIT(comp_dir);

				if (dbg_rela_info == NULL) {
					dbg_info_buf = dbg_info->d_buf;
					dbg_info_size = dbg_info->d_size;
				} else {
					dbg_info_buf = relocate_sec(dbg_info,
					    dbg_rela_info, gelf_getclass(elf));
					dbg_info_size = dbg_info->d_size;
				}

				if (dbg_str == NULL) {
					dbg_str_buf = NULL;
					dbg_str_size = 0;
				} else {
					dbg_str_buf =
					    dbg_str->d_buf;
					dbg_str_size =
					    dbg_str->d_size;
				}

				if (dbg_info_buf == NULL ||
				    get_dwarf_info(dbg_info_buf, dbg_info_size,
					dbg_abbrev->d_buf, dbg_abbrev->d_size,
					dbg_str_buf, dbg_str_size,
					comp_dir) == 0) {
					comp_dir_dest(comp_dir);
					free(comp_dir);
					comp_dir = NULL;
				}
			}

			if ((line_info =
				malloc(sizeof(struct line_info_head)))
			    != NULL) {
				SLIST_HINIT_AFTER(line_info);
				SLIST_INIT(line_info);

				if (dbg_rela_line == NULL) {
					dbg_line_buf = dbg_line->d_buf;
					dbg_line_size = dbg_line->d_size;
				} else {
					dbg_line_buf = relocate_sec(dbg_line,
					    dbg_rela_line, gelf_getclass(elf));
					dbg_line_size = dbg_line->d_size;
				}

				if (dbg_line_buf == NULL ||
				    get_dwarf_line_info(dbg_line_buf,
					dbg_line_size,
					comp_dir,
					line_info) == 0) {

					line_info_dest(line_info);
					free(line_info);
					line_info = NULL;
				}
			}
		}

		p_data.list_num = get_sym(elf, &list_head, shnum, dynstr_data,
		    strtab_data, type_table);

		if (p_data.list_num == 0)
			goto next_cmd;

		p_data.headp = &list_head;
		p_data.sh_num = shnum;
		p_data.t_table = type_table;
		p_data.s_table = (const char **)sec_table;
		p_data.filename = filename;
		p_data.objname = objname;

		sym_list_print(&p_data, line_info);
next_cmd:
		if (nm_opts.debug_line == true) {
			if (dbg_rela_line != NULL)
				free(dbg_line_buf);

			if (dbg_rela_info != NULL)
				free(dbg_info_buf);

			if (line_info != NULL) {
				line_info_dest(line_info);
				free(line_info);
				line_info = NULL;
			}

			if (comp_dir != NULL) {
				comp_dir_dest(comp_dir);
				free(comp_dir);
				comp_dir = NULL;
			}
		}

		if (sec_table != NULL)
			for (i = 0; i < shnum; ++i)
				free(sec_table[i]);

		free(sec_table);
		free(type_table);

		sym_list_dest(&list_head);

		/*
		 * If file is not archive, elf_next return ELF_C_NULL and
		 * stop the loop.
		 */
		elf_cmd = elf_next(elf);
		elf_end(elf);
	}
end_read_elf:
	elf_end(arf);

	if (close(fd) == -1) {
		warn("%s: close error", filename);

		rtn |= 1;
	}

	return (rtn);
}

static unsigned char *
relocate_sec(Elf_Data *org, Elf_Data *rela, int class)
{
	GElf_Rela ra;
	Elf64_Sword add64;
	Elf32_Sword add32;
	int i;
	unsigned char *rtn;

	if (org == NULL || rela == NULL || class == ELFCLASSNONE)
		return (NULL);

	if (class != ELFCLASS32 && class != ELFCLASS64)
		return (NULL);

	if ((rtn = malloc(sizeof(unsigned char) * org->d_size)) == NULL)
		return (NULL);

	memcpy(rtn, org->d_buf, org->d_size);

	i = 0;
	while (gelf_getrela(rela, i, &ra) != NULL) {
		if (class == ELFCLASS32) {
			memcpy(&add32, rtn + ra.r_offset, sizeof(add32));
			add32 += (Elf32_Sword)ra.r_addend;
			memcpy(rtn + ra.r_offset, &add32, sizeof(add32));
		} else {
			memcpy(&add64, rtn + ra.r_offset, sizeof(add64));
			add64 += (Elf64_Sword)ra.r_addend;
			memcpy(rtn + ra.r_offset, &add64, sizeof(add64));
		}

		++i;
	}

	return (rtn);
}

static struct line_info_entry *
search_addr(struct line_info_head *l, GElf_Sym *g)
{
	struct line_info_entry *ep;

	if (l == NULL || g == NULL)
		return (NULL);

	SLIST_FOREACH(ep, l, entries) {
		if (ep->addr == g->st_value)
			return (ep);
	}

	return (NULL);
}

static void
set_opt_value_print_fn(enum radix t)
{

	switch (t) {
	case RADIX_OCT :
		nm_opts.value_print_fn = &sym_value_oct_print;
		nm_opts.size_print_fn = &sym_size_oct_print;

		break;
	case RADIX_HEX :
		nm_opts.value_print_fn = &sym_value_hex_print;
		nm_opts.size_print_fn = &sym_size_hex_print;

		break;
	case RADIX_DEC :
		nm_opts.value_print_fn = &sym_value_dec_print;
		nm_opts.size_print_fn = &sym_size_dec_print;

		break;
	case RADIX_DEFAULT :
	default :
		if (nm_opts.elem_print_fn == &sym_elem_print_all_portable) {
			nm_opts.value_print_fn = &sym_value_hex_print;
			nm_opts.size_print_fn = &sym_size_hex_print;
		} else {
			nm_opts.value_print_fn = &sym_value_dec_print;
			nm_opts.size_print_fn = &sym_size_dec_print;
		}
	}

	assert(nm_opts.value_print_fn != NULL &&
	    "nm_opts.value_print_fn is null");
}

static void
sym_elem_print_all(char type, const char *sec, const GElf_Sym *sym,
    const char *name)
{

	if (sec == NULL || sym == NULL || name == NULL ||
	    nm_opts.value_print_fn == NULL)
		return;

	if (IS_UNDEF_SYM_TYPE(type))
		printf("                ");
	else {
		switch ((nm_opts.sort_fn == & cmp_size ? 2 : 0) +
		    nm_opts.print_size) {
		case 3:
			if (sym->st_size != 0) {
				nm_opts.value_print_fn(sym);

				printf(" ");

				nm_opts.size_print_fn(sym);
			}

			break;
		case 2:
			if (sym->st_size != 0)
				nm_opts.size_print_fn(sym);

			break;
		case 1:
			nm_opts.value_print_fn(sym);
			if (sym->st_size != 0) {
				printf(" ");

				nm_opts.size_print_fn(sym);
			}

			break;
		case 0:
		default:
			nm_opts.value_print_fn(sym);
		}
	}

	printf(" %c ", type);

	print_demangle_name("%s", name);
}

static void
sym_elem_print_all_portable(char type, const char *sec, const GElf_Sym *sym,
    const char *name)
{

	if (sec == NULL || sym == NULL || name == NULL ||
	    nm_opts.value_print_fn == NULL)
		return;

	print_demangle_name("%s", name);

	printf(" %c ", type);

	if (!IS_UNDEF_SYM_TYPE(type)) {
		nm_opts.value_print_fn(sym);

		printf(" ");

		if (sym->st_size != 0)
			nm_opts.size_print_fn(sym);
	} else
		printf("        ");
}

static void
sym_elem_print_all_sysv(char type, const char *sec, const GElf_Sym *sym,
    const char *name)
{

	if (sec == NULL || sym == NULL || name == NULL ||
	    nm_opts.value_print_fn == NULL)
		return;

	print_demangle_name("%-20s|", name);

	if (IS_UNDEF_SYM_TYPE(type))
		printf("                ");
	else
		nm_opts.value_print_fn(sym);

	printf("|   %c  |", type);

	switch (sym->st_info & 0xf) {
	case STT_OBJECT:
		printf("%18s|", "OBJECT");

		break;
	case STT_FUNC:
		printf("%18s|", "FUNC");

		break;
	case STT_SECTION:
		printf("%18s|", "SECTION");

		break;
	case STT_FILE:
		printf("%18s|", "FILE");

		break;
	case STT_LOPROC:
		printf("%18s|", "LOPROC");

		break;
	case STT_HIPROC:
		printf("%18s|", "HIPROC");

		break;
	case STT_NOTYPE:
	default:
		printf("%18s|", "NOTYPE");
	};

	if (sym->st_size != 0)
		nm_opts.size_print_fn(sym);
	else
		printf("                ");

	printf("|     |%s", sec);
}

static int
sym_elem_def(char type, const GElf_Sym *sym, const char *name)
{

	assert(IS_SYM_TYPE(type));

	UNUSED(sym);
	UNUSED(name);

	return (!IS_UNDEF_SYM_TYPE(type));
}

static int
sym_elem_global(char type, const GElf_Sym *sym, const char *name)
{

	assert(IS_SYM_TYPE(type));

	UNUSED(sym);
	UNUSED(name);

	/* weak symbols resemble global. */
	return (isupper(type) || type == 'w');
}

static int
sym_elem_global_static(char type, const GElf_Sym *sym, const char *name)
{
	unsigned char info;

	assert(sym != NULL);

	UNUSED(type);
	UNUSED(name);

	info = sym->st_info >> 4;

	return (info == STB_LOCAL ||
	    info == STB_GLOBAL ||
	    info == STB_WEAK);
}

static int
sym_elem_nondebug(char type, const GElf_Sym *sym, const char *name)
{

	assert(sym != NULL);

	UNUSED(type);
	UNUSED(name);

	if (sym->st_value == 0 && (sym->st_info & 0xf) == STT_FILE)
		return (0);

	if (sym->st_name == 0)
		return (0);

	return (1);
}

static int
sym_elem_nonzero_size(char type, const GElf_Sym *sym, const char *name)
{

	assert(sym != NULL);

	UNUSED(type);
	UNUSED(name);

	return (sym->st_size > 0);
}

static int
sym_elem_undef(char type, const GElf_Sym *sym, const char *name)
{

	assert(IS_SYM_TYPE(type));

	UNUSED(sym);
	UNUSED(name);

	return (IS_UNDEF_SYM_TYPE(type));
}

static void
sym_list_dest(struct sym_head *headp)
{
	struct sym_entry *ep, *ep_n;

	if (headp == NULL)
		return;

	ep = STAILQ_FIRST(headp);
	while (ep != NULL) {
		ep_n = STAILQ_NEXT(ep, sym_entries);

		free(ep->sym);
		free(ep->name);
		free(ep);

		ep = ep_n;
	}
}

static int
sym_list_insert(struct sym_head *headp, const char *name, const GElf_Sym *sym)
{
	struct sym_entry *e;

	if (headp == NULL || name == NULL || sym == NULL)
		return (0);

	if ((e = malloc(sizeof(struct sym_entry))) == NULL)
		return (0);

	if ((e->name = strdup(name)) == NULL) {
		free(e);

		return (0);
	}

	if ((e->sym = malloc(sizeof(GElf_Sym))) == NULL) {
		free(e->name);
		free(e);

		return (0);
	}

	memcpy(e->sym, sym, sizeof(GElf_Sym));

	/* GNU nm display size instead value. */
	if (IS_COM_SYM(sym))
		e->sym->st_value = sym->st_size;

	STAILQ_INSERT_TAIL(headp, e, sym_entries);

	return (1);
}

/* If file has not .debug_info, line_info will be NULL */
static void
sym_list_print(struct sym_print_data *p, struct line_info_head *line_info)
{
	struct sym_entry *e_v;

	if (p == NULL || CHECK_SYM_PRINT_DATA(p))
		return;

	if ((e_v = sym_list_sort(p)) == NULL)
		return;

	if (nm_opts.sort_reverse == false)
		for (size_t i = 0; i != p->list_num; ++i)
			sym_list_print_each(&e_v[i], p, line_info);
	else
		for (int i = p->list_num - 1; i != -1; --i)
			sym_list_print_each(&e_v[i], p, line_info);

	free(e_v);
}

/* If file has not .debug_info, line_info will be NULL */
static void
sym_list_print_each(struct sym_entry *ep, struct sym_print_data *p,
    struct line_info_head *line_info)
{
	const char *sec;
	char type;

	if (ep == NULL || CHECK_SYM_PRINT_DATA(p))
		return;

	assert(ep->name != NULL);
	assert(ep->sym != NULL);

	type = get_sym_type(ep->sym, p->t_table);

	if (nm_opts.print_name == PRINT_NAME_FULL) {
		printf("%s", p->filename);

		if (nm_opts.elem_print_fn == &sym_elem_print_all_portable) {
			if (p->objname != NULL)
				printf("[%s]", p->objname);

			printf(": ");
		} else {
			if (p->objname != NULL)
				printf(":%s", p->objname);

			printf(":");
		}
	}

	switch (ep->sym->st_shndx) {
	case SHN_LOPROC:
		/* LOPROC or LORESERVE */
		sec = "*LOPROC*";

		break;
	case SHN_HIPROC:
		sec = "*HIPROC*";

		break;
	case SHN_LOOS:
		sec = "*LOOS*";

		break;
	case SHN_HIOS:
		sec = "*HIOS*";

		break;
	case SHN_ABS:
		sec = "*ABS*";

		break;
	case SHN_COMMON:
		sec = "*COM*";

		break;
	case SHN_HIRESERVE:
		/* HIRESERVE or XINDEX */
		sec = "*HIRESERVE*";

		break;
	default:
		if (ep->sym->st_shndx > p->sh_num)
			return;

		sec = p->s_table[ep->sym->st_shndx];
	};

	nm_opts.elem_print_fn(type, sec, ep->sym, ep->name);

	if (nm_opts.debug_line == true && line_info != NULL &&
	    !IS_UNDEF_SYM_TYPE(type)) {
		struct line_info_entry *lep;

		if ((lep = search_addr(line_info, ep->sym)) != NULL)
			printf("\t%s:%" PRIu64, lep->file, lep->line);
	}

	printf("\n");
}

static struct sym_entry	*
sym_list_sort(struct sym_print_data *p)
{
	struct sym_entry *ep, *e_v;
	int idx;

	if (p == NULL || CHECK_SYM_PRINT_DATA(p))
		return (NULL);

	if ((e_v = malloc(sizeof(struct sym_entry) * p->list_num)) == NULL)
		return (NULL);

	idx = 0;
	STAILQ_FOREACH(ep, p->headp, sym_entries) {
		if (ep->name != NULL && ep->sym != NULL) {
			e_v[idx].name = ep->name;
			e_v[idx].sym = ep->sym;

			++idx;
		}
	}

	assert((size_t)idx == p->list_num);

	if (nm_opts.sort_fn != &cmp_none) {
		nm_print_data = p;
		assert(nm_print_data != NULL);

		qsort(e_v, p->list_num, sizeof(struct sym_entry),
		    nm_opts.sort_fn);
	}

	return (e_v);
}

static void
sym_size_oct_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");

	printf("%016" PRIo64, sym->st_size);
}

static void
sym_size_hex_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");

	printf("%016" PRIx64, sym->st_size);

}

static void
sym_size_dec_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");

	printf("%016" PRId64, sym->st_size);
}

static void
sym_value_oct_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");

	printf("%016" PRIo64, sym->st_value);
}

static void
sym_value_hex_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");

	printf("%016" PRIx64, sym->st_value);
}

static void
sym_value_dec_print(const GElf_Sym *sym)
{

	assert(sym != NULL && "sym is null");

	printf("%016" PRId64, sym->st_value);
}

static void
usage(int exitcode)
{

	printf("Usage: %s [options] file ...\
\n  Display symbolic information in file.\
\n  Options : \
\n    -A, --print-file-name     Write the full pathname or library name of an\
\n                              object on each line.\
\n    -a, --debug-syms          Display all symbols include debugger-only\
\n                              symbols.", nm_info.name);
	printf("\
\n    -B                        Equivalent to specifying \"--format=bsd\".\
\n    -C, --demangle[=style]    Decode low-level symbol names.\
\n        --no-demangle         Do not demangle low-level symbol names.\
\n    -D, --dynamic             Display only dynamic symbols.\
\n    -e                        Display only global and static symbols.");
	printf("\
\n    -f                        Produce full output (default).\
\n    --format=format           Display output in specific format.  Allowed\
\n                              formats are: \"bsd\", \"posix\" and \"sysv\".\
\n    -g                        Display only global symbol information.\
\n    -h, --help                Show this help message.\
\n    -l, --line-numbers        Display filename and linenumber using\
\n                              debugging information.\
\n    -n, --numeric-sort        Sort symbols numerically by value.");
	printf("\
\n    -o                        Write numeric values in octal. Equivalent to\
\n                              specifying \"-t o\".\
\n    -p, --no-sort             Do not sort symbols.\
\n    -P                        Write information in a portable output format.\
\n                              Equivalent to specifying \"--format=posix\".\
\n    -r, --reverse-sort        Reverse the order of the sort.\
\n    -S, --print-size          Print symbol sizes instead values.\
\n    -s, --print-armap         Include an index of archive members.\
\n        --size-sort           Sort symbols by size.");
	printf("\
\n    -t, --radix=format        Write each numeric value in the specified\
\n                              format:\
\n                                 d   In decimal,\
\n                                 o   In octal,\
\n                                 x   In hexadecimal.");
	printf("\
\n    -u, --undefined-only      Display only undefined symbols.\
\n        --defined-only        Display only defined symbols.\
\n    -V, --version             Show the version number for %s.\
\n    -v                        Sort output by value.\
\n    -x                        Write numeric values in hexadecimal.\
\n                              Equivalent to specifying \"-t x\".",
	    nm_info.name);
	printf("\n\
\n  The default options are: output in bsd format, use a decimal radix,\
\n  sort by symbol name, do not demangle names.\n");

	exit(exitcode);
}

/*
 * Display symbolic information in file.
 * Return 0 at success, >0 at failed.
 */
int
main(int argc, char *argv[])
{
	int ch, rtn;
	enum radix t;

	assert(argc > 0);

	global_init();

	t = RADIX_DEFAULT;

	while ((ch = getopt_long(argc, argv, "ABCDSVPaefghlnoprst:uvx",
		    nm_longopts, NULL)) != -1) {
		switch (ch) {
		case 'A':
			nm_opts.print_name = PRINT_NAME_FULL;

			break;
		case 'B':
			nm_opts.elem_print_fn = &sym_elem_print_all;

			break;
		case 'C':
			nm_opts.demangle_type = get_demangle_option(optarg);

			break;
		case 'F':
			/* sysv, bsd, posix */
			switch (optarg[0]) {
			case 'B':
			case 'b':
				nm_opts.elem_print_fn = &sym_elem_print_all;

				break;
			case 'P':
			case 'p':
				nm_opts.elem_print_fn =
				    &sym_elem_print_all_portable;

				break;
			case 'S':
			case 's':
				nm_opts.elem_print_fn =
				    &sym_elem_print_all_sysv;
				break;

			default:
				warnx("%s: Invalid format", optarg);

				usage(EX_USAGE);

				/* NOTREACHED */
			}

			break;
		case 'D':
			nm_opts.print_symbol = PRINT_SYM_DYN;

			break;
		case 'S':
			nm_opts.print_size = 1;

			break;
		case 'V':
			print_version();

			/* NOTREACHED */
			break;
		case 'P':
			nm_opts.elem_print_fn = &sym_elem_print_all_portable;

			break;
		case 'a':
			nm_opts.print_debug = true;

			break;
		case 'f':
			break;
		case 'e':
			filter_insert(sym_elem_global_static);

			break;
		case 'g':
			filter_insert(sym_elem_global);

			break;
		case 'o':
			t = RADIX_OCT;

			break;
		case 'p':
			nm_opts.sort_fn = &cmp_none;

			break;
		case 'r':
			nm_opts.sort_reverse = true;

			break;
		case 's':
			nm_opts.print_armap = true;

			break;
		case 't':
			/* t require always argument to getopt_long */
			switch (optarg[0]) {
			case 'd':
				t = RADIX_DEC;

				break;
			case 'o':
				t = RADIX_OCT;

				break;
			case 'x':
				t = RADIX_HEX;

				break;
			default:
				warnx("%s: Invalid radix", optarg);

				usage(EX_USAGE);

				/* NOTREACHED */
			}

			break;
		case 'u':
			filter_insert(sym_elem_undef);
			nm_opts.undef_only = true;

			break;
		case 'l':
			nm_opts.debug_line = true;

			break;
		case 'n':
			/* FALLTHROUGH */
		case 'v':
			nm_opts.sort_fn = &cmp_value;

			break;
		case 'x':
			t = RADIX_HEX;

			break;
		case 0:
			if (nm_opts.sort_size != 0) {
				nm_opts.sort_fn = &cmp_size;

				filter_insert(sym_elem_def);
				filter_insert(sym_elem_nonzero_size);
			}

			if (nm_opts.def_only != 0)
				filter_insert(sym_elem_def);

			if (nm_opts.no_demangle != 0)
				nm_opts.demangle_type = DEMANGLE_NONE;

			break;
		case 'h':
			usage(EX_OK);

			/* NOTREACHED */
		default :
			usage(EX_USAGE);

			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	assert(nm_info.name != NULL && "nm_info.name is null");
	assert(nm_info.def_filename != NULL && "nm_info.def_filename is null");
	assert(nm_opts.sort_fn != NULL && "nm_opts.sort_fn is null");
	assert(nm_opts.elem_print_fn != NULL &&
	    "nm_opts.elem_print_fn is null");
	assert(nm_opts.value_print_fn != NULL &&
	    "nm_opts.value_print_fn is null");

	set_opt_value_print_fn(t);

	if (nm_opts.undef_only == true) {
		if (nm_opts.sort_fn == &cmp_size)
			errx(EX_USAGE, "--size-sort with -u is meaningless");

		if (nm_opts.def_only != 0)
			errx(EX_USAGE,
			    "-u with --defined-only is meaningless");
	}

	if (nm_opts.print_debug == false)
		filter_insert(sym_elem_nondebug);

	if (nm_opts.sort_reverse == true && nm_opts.sort_fn == cmp_none)
		nm_opts.sort_reverse = false;

	rtn = 0;
	if (argc == 0)
		rtn |= read_elf(nm_info.def_filename);
	else {
		if (nm_opts.print_name == PRINT_NAME_NONE && argc > 1)
			nm_opts.print_name = PRINT_NAME_MULTI;

		while (argc > 0) {
			rtn |= read_elf(*argv);

			--argc;
			++argv;
		}
	}

	filter_dest();

	return (rtn);
}
