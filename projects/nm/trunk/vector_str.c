/*-
 * Copyright (c) 2008 Hyogeol Lee <hyogeollee@gmail.com>
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vector_str.h"

/**
 * @file vector_str.c
 * @brief Dynamic vector data for string implementation.
 *
 * Resemble to std::vector<std::string> in C++.
 */

static size_t	get_strlen_sum(const struct vector_str *v);
static bool	vector_str_grow(struct vector_str *v);

static size_t
get_strlen_sum(const struct vector_str * v)
{
	size_t len = 0;

	if (v == NULL)
		return (0);

	for (size_t i = 0; i < v->size; ++i)
		len += strlen(v->container[i]);

	return (len);
}

void
vector_str_dest(struct vector_str *v)
{

	if (v == NULL)
		return;

	for (size_t i = 0; i < v->size; ++i)
		free(v->container[i]);

	free(v->container);
}

int
vector_str_find(const struct vector_str *v, const char *o, size_t l)
{

	if (v == NULL || o == NULL)
		return (-1);

	for (size_t i = 0; i < v->size; ++i)
		if (strncmp(v->container[i], o, l) == 0)
			return (1);

	return (0);
}

char *
vector_str_get_flat(const struct vector_str *v, size_t *l)
{
	ssize_t elem_pos, elem_size, rtn_size;
	char *rtn;

	if (v == NULL || v->size == 0)
		return (NULL);

	if ((rtn_size = get_strlen_sum(v)) == 0)
		return (0);

	if ((rtn = malloc(sizeof(char) * (rtn_size + 1))) == NULL)
		return (NULL);

	elem_pos = 0;
	for (size_t i = 0; i < v->size; ++i) {
		elem_size = strlen(v->container[i]);

		memcpy(rtn + elem_pos, v->container[i], elem_size);

		elem_pos += elem_size;
	}

	rtn[rtn_size] = '\0';

	if (l != NULL)
		*l = rtn_size;

	return (rtn);
}

static bool
vector_str_grow(struct vector_str *v)
{
	size_t tmp_cap;
	char **tmp_ctn;

	if (v == NULL)
		return (false);

	tmp_cap = v->capacity * BUFFER_GROWFACTOR;

	if ((tmp_ctn = malloc(sizeof(char *) * tmp_cap)) == NULL)
		return (false);

	for (size_t i = 0; i < v->size; ++i)
		tmp_ctn[i] = v->container[i];

	free(v->container);

	v->container = tmp_ctn;
	v->capacity = tmp_cap;

	return (true);
}

bool
vector_str_init(struct vector_str *v)
{

	if (v == NULL)
		return (false);

	v->size = 0;
	v->capacity = VECTOR_DEF_CAPACITY;

	if ((v->container = malloc(sizeof(char *) * v->capacity)) == NULL)
		return (false);

	assert(v->container != NULL);

	return (true);
}

bool
vector_str_pop(struct vector_str *v)
{

	if (v == NULL)
		return (false);

	if (v->size == 0)
		return (true);

	--v->size;

	free(v->container[v->size]);
	v->container[v->size] = NULL;

	return (true);
}

bool
vector_str_push(struct vector_str *v, const char *str, size_t len)
{

	if (v == NULL || str == NULL)
		return (false);

	if (v->size == v->capacity && vector_str_grow(v) == false)
		return (false);

	if ((v->container[v->size] = malloc(sizeof(char) * (len + 1))) == NULL)
		return (false);

	snprintf(v->container[v->size], len + 1, "%s", str);

	++v->size;

	return (true);
}

bool
vector_str_push_vector_head(struct vector_str *dst, struct vector_str *org)
{
	size_t tmp_cap;
	char **tmp_ctn;

	if (dst == NULL || org == NULL)
		return (false);

	tmp_cap = (dst->size + org->size) * BUFFER_GROWFACTOR;

	if ((tmp_ctn = malloc(sizeof(char *) * tmp_cap)) == NULL)
		return (false);

	for (size_t i = 0; i < org->size; ++i)
		if ((tmp_ctn[i] = strdup(org->container[i])) == NULL) {
			for (size_t j = 0; j < i; ++j)
				free(tmp_ctn[j]);

			free(tmp_ctn);

			return (false);
		}

	for (size_t i = 0; i < dst->size; ++i)
		tmp_ctn[i + org->size] = dst->container[i];

	free(dst->container);

	dst->container = tmp_ctn;
	dst->capacity = tmp_cap;
	dst->size += org->size;

	return (true);
}

char *
vector_str_substr(const struct vector_str *v, size_t begin, size_t end,
    size_t *r_len)
{
	size_t cur, len;
	char *rtn;

	if (v == NULL || begin > end)
		return (NULL);

	len = 0;
	for (size_t i = begin; i < end + 1; ++i)
		len += strlen(v->container[i]);

	if ((rtn = malloc(sizeof(char) * (len + 1))) == NULL)
		return (NULL);

	if (r_len != NULL)
		*r_len = len;

	cur = 0;
	for (size_t i = begin; i < end + 1; ++i) {
		len = strlen(v->container[i]);
		memcpy(rtn + cur, v->container[i], len);
		cur += len;
	}
	rtn[cur] = '\0';

	return (rtn);
}
