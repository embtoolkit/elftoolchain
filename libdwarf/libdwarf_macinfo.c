/*-
 * Copyright (c) 2009, 2010 Kai Wang
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

#include "_libdwarf.h"

static int
_dwarf_macinfo_parse(Dwarf_Debug dbg, Dwarf_Section *ds, uint64_t *off,
    Dwarf_Macro_Details *dmd, Dwarf_Unsigned *cnt, Dwarf_Error *error)
{
	Dwarf_Unsigned lineno, fileindex;
	char *p;
	int i, type;

	i = 0;
	while (*off < ds->ds_size) {

		if (dmd != NULL)
			dmd[i].dmd_offset = *off;

		type = dbg->read(ds->ds_data, off, 1);

		if (dmd != NULL) {
			dmd[i].dmd_type = type;
			dmd[i].dmd_fileindex = -1;
		}

		switch (type) {
		case 0:
			break;
		case DW_MACINFO_define:
		case DW_MACINFO_undef:
		case DW_MACINFO_vendor_ext:
			lineno = _dwarf_read_uleb128(ds->ds_data, off);
			p = (char *) ds->ds_data;
			if (dmd != NULL) {
				dmd[i].dmd_lineno = lineno;
				dmd[i].dmd_macro = p + *off;

			}
			while (p[(*off)++] != '\0')
				;
			break;
		case DW_MACINFO_start_file:
			lineno = _dwarf_read_uleb128(ds->ds_data, off);
			fileindex = _dwarf_read_uleb128(ds->ds_data, off);
			if (dmd != NULL) {
				dmd[i].dmd_lineno = lineno;
				dmd[i].dmd_fileindex = fileindex;
			}
			break;
		case DW_MACINFO_end_file:
			break;
		default:
			DWARF_SET_ERROR(error, DWARF_E_INVALID_MACINFO);
			return (DWARF_E_INVALID_MACINFO);
		}

		i++;

		if (type == 0)
			break;
	}

	if (cnt != NULL)
		*cnt = i;

	return (DW_DLE_NONE);
}

void
_dwarf_macinfo_cleanup(Dwarf_Debug dbg)
{
	Dwarf_MacroSet ms, tms;

	if (STAILQ_EMPTY(&dbg->dbg_mslist))
		return;

	STAILQ_FOREACH_SAFE(ms, &dbg->dbg_mslist, ms_next, tms) {
		STAILQ_REMOVE(&dbg->dbg_mslist, ms, _Dwarf_MacroSet, ms_next);
		if (ms->ms_mdlist)
			free(ms->ms_mdlist);
		free(ms);
	}
}

int
_dwarf_macinfo_init(Dwarf_Debug dbg, Dwarf_Section *ds, Dwarf_Error *error)
{
	Dwarf_MacroSet ms;
	Dwarf_Unsigned cnt;
	uint64_t offset, entry_off;
	int ret;

	offset = 0;
	while (offset < ds->ds_size) {

		entry_off = offset;

		ret = _dwarf_macinfo_parse(dbg, ds, &offset, NULL, &cnt, error);
		if (ret != DW_DLE_NONE)
			return (ret);

		if (cnt == 0)
			break;

		if ((ms = calloc(1, sizeof(struct _Dwarf_MacroSet))) == NULL) {
			DWARF_SET_ERROR(error, DW_DLE_MEMORY);
			ret = DW_DLE_MEMORY;
			goto fail_cleanup;
		}
		STAILQ_INSERT_TAIL(&dbg->dbg_mslist, ms, ms_next);

		if ((ms->ms_mdlist = calloc(cnt, sizeof(Dwarf_Macro_Details)))
		    == NULL) {
			DWARF_SET_ERROR(error, DW_DLE_MEMORY);
			ret = DW_DLE_MEMORY;
			goto fail_cleanup;
		}

		ms->ms_cnt = cnt;

		offset = entry_off;

		ret = _dwarf_macinfo_parse(dbg, ds, &offset, ms->ms_mdlist,
		    NULL, error);

		if (ret != DW_DLE_NONE) {
			DWARF_SET_ERROR(error, DW_DLE_MEMORY);
			ret = DW_DLE_MEMORY;
			goto fail_cleanup;
		}
	}

	return (DW_DLE_NONE);

fail_cleanup:

	_dwarf_macinfo_cleanup(dbg);

	return (ret);
}

int
_dwarf_macinfo_gen(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_P_Section ds;
	Dwarf_Macro_Details *md;
	int i, ret;

	if (dbg->dbgp_mdcnt == 0)
		return (DW_DLE_NONE);

	/* Create .debug_frame section. */
	RCHECK(_dwarf_section_init(dbg, &ds, ".debug_macinfo", 0, error));

	/* Write the list of Dwarf_Macro_Details. */
	for (i = 0; (Dwarf_Unsigned) i < dbg->dbgp_mdcnt; i++) {
		md = &dbg->dbgp_mdlist[i];
		md->dmd_offset = ds->ds_size;
		RCHECK(WRITE_VALUE(md->dmd_type, 1));
		switch (md->dmd_type) {
		case DW_MACINFO_define:
		case DW_MACINFO_undef:
		case DW_MACINFO_vendor_ext:
			RCHECK(WRITE_ULEB128(md->dmd_lineno));
			assert(md->dmd_macro != NULL);
			RCHECK(WRITE_STRING(md->dmd_macro));
			break;
		case DW_MACINFO_start_file:
			RCHECK(WRITE_ULEB128(md->dmd_lineno));
			RCHECK(WRITE_ULEB128(md->dmd_fileindex));
			break;
		case DW_MACINFO_end_file:
			break;
		default:
			assert(0);
			break;
		}
	}
	RCHECK(WRITE_VALUE(0, 1));

	/* Inform application the creation of .debug_macinfo ELF section. */
	RCHECK(_dwarf_section_callback(dbg, ds, SHT_PROGBITS, 0, 0, 0, error));

	return (DW_DLE_NONE);

gen_fail:
	_dwarf_section_free(dbg, &ds);

	return (ret);
}
