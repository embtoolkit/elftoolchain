/*-
 * Copyright (c) 2009 Kai Wang
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
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <dwarf.h>
#include <libdwarf.h>
#include <libelftc.h>
#include <libgen.h>

static struct option longopts[] = {
	{"target" , required_argument, NULL, 'b'},
	{"demangle", no_argument, NULL, 'C'},
	{"exe", required_argument, NULL, 'e'},
	{"functions", no_argument, NULL, 'f'},
	{"basename", no_argument, NULL, 's'},
	{"help", no_argument, NULL, 'H'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

static int demangle, func, base;

static void
usage(void)
{

	fprintf(stderr, "usage: %s [-b target] [-Cfs] [-e exe] addr addr"
	    " ...\n", getprogname());
	exit(1);
}

static void
version(void)
{

	fprintf(stderr, "%s 1.0", getprogname());
	exit(0);
}

static void
search_func(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Addr addr,
    const char **rlt_func)
{
	Dwarf_Die ret_die;
	Dwarf_Error de;
	Dwarf_Half tag;
	Dwarf_Unsigned lopc, hipc;
	int ret;

	if (*rlt_func != NULL)
		return;

	if (dwarf_tag(die, &tag, &de)) {
		warnx("dwarf_tag: %s", dwarf_errmsg(&de));
		goto cont_search;
	}
	if (tag == DW_TAG_subprogram) {
		if (dwarf_attrval_unsigned(die, DW_AT_low_pc, &lopc, &de) ||
		    dwarf_attrval_unsigned(die, DW_AT_high_pc, &hipc, &de))
			goto cont_search;
		if (addr < lopc || addr >= hipc)
			goto cont_search;

		/* Found it! */
		if (dwarf_attrval_string(die, DW_AT_name, rlt_func, &de))
			*rlt_func = "??";

		return;
	}

cont_search:

	/* Search children. */
	ret = dwarf_child(die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		errx(1, "dwarf_child: %s", dwarf_errmsg(&de));
	else if (ret == DW_DLV_OK)
		search_func(dbg, ret_die, addr, rlt_func);

	/* Search sibling. */
	ret = dwarf_siblingof(dbg, die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		errx(1, "dwarf_siblingof: %s", dwarf_errmsg(&de));
	else if (ret == DW_DLV_OK)
		search_func(dbg, ret_die, addr, rlt_func);
}

static void
translate(Dwarf_Debug dbg, const char* addrstr)
{
	Dwarf_Die die;
	Dwarf_Line *lbuf;
	Dwarf_Error de;
	Dwarf_Half tag;
	Dwarf_Unsigned lopc, hipc, addr, lineno, plineno;
	Dwarf_Signed lcount;
	Dwarf_Addr lineaddr, plineaddr;
	const char *pfile, *file;
	const char *funcname;
	char demangled[1024];
	int i, ret;

	addr = strtoull(addrstr, NULL, 16);
	lineno = 0;
	file = "??";

	while ((ret = dwarf_next_cu_header(dbg, NULL, NULL, NULL, NULL, NULL,
	    &de)) ==  DW_DLV_OK) {
		die = NULL;
		while (dwarf_siblingof(dbg, die, &die, &de) == DW_DLV_OK) {
			if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
				warnx("dwarf_tag failed: %s",
				    dwarf_errmsg(&de));
				goto out;
			}
			/* XXX: What about DW_TAG_partial_unit? */
			if (tag == DW_TAG_compile_unit)
				break;
		}
		if (die == NULL) {
			warnx("could not find DW_TAG_compile_unit die");
			goto out;
		}
		if (!dwarf_attrval_unsigned(die, DW_AT_low_pc, &lopc, &de) &&
		    !dwarf_attrval_unsigned(die, DW_AT_high_pc, &hipc, &de)) {
			/*
			 * Check if the address falls into the PC range of
			 * this CU.
			 */
			if (addr < lopc || addr >= hipc)
				continue;
		}

		if (dwarf_srclines(die, &lbuf, &lcount, &de) != DW_DLV_OK) {
			warnx("dwarf_srclines: %s", dwarf_errmsg(&de));
			goto out;
		}

		plineaddr = ~0ULL;
		plineno = 0;
		pfile = "??";
		for (i = 0; i < lcount; i++) {
			if (dwarf_lineaddr(lbuf[i], &lineaddr, &de)) {
				warnx("dwarf_lineaddr: %s",
				    dwarf_errmsg(&de));
				goto out;
			}
			if (dwarf_lineno(lbuf[i], &lineno, &de)) {
				warnx("dwarf_lineno: %s",
				    dwarf_errmsg(&de));
				goto out;
			}
			if (dwarf_linesrc(lbuf[i], &file, &de)) {
				warnx("dwarf_linesrc: %s",
				    dwarf_errmsg(&de));
			}
			if (addr == lineaddr)
				goto out;
			else if (addr < lineaddr && addr > plineaddr) {
				lineno = plineno;
				file = pfile;
				goto out;
			}
			plineaddr = lineaddr;
			plineno = lineno;
			pfile = file;
		}
	}

out:
	funcname = NULL;
	if (ret == DW_DLV_OK && func)
		search_func(dbg, die, addr, &funcname);

	if (func) {
		if (funcname == NULL)
			funcname = "??";
		if (demangle &&
		    !elftc_demangle(funcname, demangled, sizeof(demangled), 0))
			printf("%s\n", demangled);
		else
			printf("%s\n", funcname);
	}
	printf("%s:%ju\n", base ? basename(file) : file, lineno);

	/*
	 * Reset internal CU pointer, so we will start from the first CU
	 * next round.
	 */
	while (ret != DW_DLV_NO_ENTRY) {
		if (ret == DW_DLV_ERROR)
			errx(1, "dwarf_next_cu_header: %s", dwarf_errmsg(&de));
		ret = dwarf_next_cu_header(dbg, NULL, NULL, NULL, NULL, NULL,
		    &de);
	}
}

int
main(int argc, char **argv)
{
	Dwarf_Debug dbg;
	Dwarf_Error de;
	const char *exe;
	char line[1024];
	int fd, i, opt;

	exe = NULL;
	while ((opt = getopt_long(argc, argv, "b:Ce:fsHV", longopts, NULL)) !=
	    -1) {
		switch (opt) {
		case 'b':
			/* ignored */
			break;
		case 'C':
			demangle = 1;
			break;
		case 'e':
			exe = optarg;
			break;
		case 'f':
			func = 1;
			break;
		case 's':
			base = 1;
			break;
		case 'H':
			usage();
		case 'V':
			version();
		default:
			usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (exe == NULL)
		exe = "a.out";

	if ((fd = open(exe, O_RDONLY)) < 0)
		err(1, "%s", exe);

	if (dwarf_init(fd, DW_DLC_READ, &dbg, &de))
		errx(1, "dwarf_init: %s", dwarf_errmsg(&de));


	if (argc > 0)
		for (i = 0; i < argc; i++)
			translate(dbg, argv[i]);
	else
		while (fgets(line, sizeof(line), stdin) != NULL)
			translate(dbg, line);

	dwarf_finish(&dbg, &de);

	exit(0);
}
