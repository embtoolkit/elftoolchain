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

#include <assert.h>
#include <stdlib.h>
#include "_libdwarf.h"

static int
loclist_add_locdesc(Dwarf_Debug dbg, Dwarf_CU cu, Elf_Data *d, uint64_t *off,
    Dwarf_Locdesc *ld, uint64_t *ldlen, Dwarf_Unsigned *total_len,
    Dwarf_Error *error)
{
	uint64_t start, end;
	int i, len, ret;

	if (total_len != NULL)
		*total_len = 0;

	for (i = 0; *off < d->d_size; i++) {
		start = dbg->read(&d, off, cu->cu_pointer_size);
		end = dbg->read(&d, off, cu->cu_pointer_size);
		if (ld != NULL) {
			ld[i].ld_lopc = start;
			ld[i].ld_hipc = end;
		}

		if (total_len != NULL)
			*total_len += 2 * cu->cu_pointer_size;

		/* Check if it is the end entry. */
		if (start == 0 && end ==0) {
			i++;
			break;
		}

		/* Check if it is base-select entry. */
		if ((cu->cu_pointer_size == 4 && start == ~0U) ||
		    (cu->cu_pointer_size == 8 && start == ~0ULL))
			continue;

		/* Otherwise it's normal entry. */
		len = dbg->read(&d, off, 2);
		if (*off + len > d->d_size) {
			DWARF_SET_ERROR(error, DWARF_E_INVALID_LOCLIST);
			return (DWARF_E_INVALID_LOCLIST);
		}

		if (total_len != NULL)
			*total_len += len;

		if (ld != NULL) {
			ret = loc_fill_locdesc(dbg, &ld[i],
			    (uint8_t *)d->d_buf + *off, len,
			    cu->cu_pointer_size, error);
			if (ret != DWARF_E_NONE)
				return (ret);
		}

		*off += len;
	}

	if (ldlen != NULL)
		*ldlen = i;

	return (DWARF_E_NONE);
}

int
loclist_find(Dwarf_Debug dbg, uint64_t lloff, Dwarf_Loclist *ret_ll)
{
	Dwarf_Loclist ll;

	TAILQ_FOREACH(ll, &dbg->dbg_loclist, ll_next)
		if (ll->ll_offset == lloff)
			break;

	if (ll == NULL)
		return (DWARF_E_NO_ENTRY);

	if (ret_ll != NULL)
		*ret_ll = ll;

	return (DWARF_E_NONE);
}

int
loclist_add(Dwarf_Debug dbg, Dwarf_CU cu, uint64_t lloff, Dwarf_Error *error)
{
	Elf_Data *d;
	Dwarf_Loclist ll, tll;
	uint64_t ldlen;
	int ret;

	ret = DWARF_E_NONE;

	d = dbg->dbg_s[DWARF_debug_loc].s_data;

	/* First we search if we have already added this loclist. */
	if (loclist_find(dbg, lloff, NULL) != DWARF_E_NO_ENTRY)
		return (ret);

	if ((ll = malloc(sizeof(struct _Dwarf_Loclist))) == NULL) {
		DWARF_SET_ERROR(error, DWARF_E_MEMORY);
		return (DWARF_E_MEMORY);
	}

	ll->ll_offset = lloff;

	/* Get the number of locdesc the first round. */
	ret = loclist_add_locdesc(dbg, cu, d, &lloff, NULL, &ldlen, NULL,
	    error);
	if (ret != DWARF_E_NONE) {
		free(ll);
		return (ret);
	}

	ll->ll_ldlen = ldlen;
	if ((ll->ll_ldlist = calloc(ldlen, sizeof(Dwarf_Locdesc))) == NULL) {
		free(ll);
		DWARF_SET_ERROR(error, DWARF_E_MEMORY);
		return (DWARF_E_MEMORY);
	}

	lloff = ll->ll_offset;

	/* Fill in locdesc. */
	ret = loclist_add_locdesc(dbg, cu, d, &lloff, ll->ll_ldlist, NULL,
	    &ll->ll_length, error);
	if (ret != DWARF_E_NONE) {
		free(ll->ll_ldlist);
		free(ll);
		return (ret);
	}

	/* Insert to the queue. Sort by offset. */
	TAILQ_FOREACH(tll, &dbg->dbg_loclist, ll_next)
		if (tll->ll_offset > ll->ll_offset) {
			TAILQ_INSERT_BEFORE(tll, ll, ll_next);
			break;
		}
		
	if (tll == NULL)
		TAILQ_INSERT_TAIL(&dbg->dbg_loclist, ll, ll_next);

	return (ret);
}
