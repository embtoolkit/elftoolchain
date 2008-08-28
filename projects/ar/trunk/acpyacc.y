%{
/*-
 * Copyright (c) 2008 Kai Wang
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
__FBSDID("$FreeBSD$");

#include <sys/mman.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ar.h"

#define TEMPLATE "arscp.XXXXXXXX"

struct list {
	char		*str;
	struct list	*next;
};


extern int	yylex(void);
extern int	yyparse(void);

static void	yyerror(const char *);
static void	arscp_addlib(char *archive, struct list *list);
static void	arscp_addmod(struct list *list);
static void	arscp_clear(void);
static int	arscp_copy(int ifd, int ofd);
static void	arscp_create(char *in, char *out);
static void	arscp_delete(struct list *list);
static void	arscp_delete_dir(const char *dir);
static void	arscp_dir(char *archive, struct list *list, char *rlt);
static void	arscp_dir2argv(const char *dir);
static void	arscp_end(void);
static void	arscp_extract(struct list *list);
static void	arscp_free_argv(void);
static void	arscp_free_mlist(struct list *list);
static void	arscp_list(void);
static struct list *arscp_mlist(struct list *list, char *str);
static void	arscp_mlist2argv(struct list *list);
static int	arscp_mlist_len(struct list *list);
static void	arscp_open(char *fname);
static void	arscp_prompt(void);
static void	arscp_replace(struct list *list);
static void	arscp_save(void);
static char	*arscp_strcat(const char *path, const char *name);
static int	arscp_target_exist(void);

extern int		 lineno;

static struct bsdar	*bsdar;
static char		*target;
static char		*tmpac;
static int		 interactive;
static int		 verbose;

%}

%token ADDLIB
%token ADDMOD
%token CLEAR
%token CREATE
%token DELETE
%token DIRECTORY
%token END
%token EXTRACT
%token LIST
%token OPEN
%token REPLACE
%token VERBOSE
%token SAVE
%token LP
%token RP
%token COMMA
%token EOL
%token <str> FNAME
%type <list> mod_list

%union {
	char		*str;
	struct list	*list;
}

%%

begin
	: { arscp_prompt(); } ar_script
	;

ar_script
	: cmd_list
	|
	;

mod_list
	: FNAME { $$ = arscp_mlist(NULL, $1); }
	| mod_list separator FNAME { $$ = arscp_mlist($1, $3); }
	;

separator
	: COMMA
	|
	;

cmd_list
	: rawcmd
	| cmd_list rawcmd
	;

newline
	: EOL { arscp_prompt(); }
	;

rawcmd
	: cmd newline
	| newline
	;

cmd
	: addlib_cmd
	| addmod_cmd
	| clear_cmd
	| create_cmd
	| delete_cmd
	| directory_cmd
	| end_cmd
	| extract_cmd
	| list_cmd
	| open_cmd
	| replace_cmd
	| verbose_cmd
	| save_cmd
	| FNAME { yyerror(NULL); }
	;

addlib_cmd
	: ADDLIB FNAME LP mod_list RP { arscp_addlib($2, $4); }
	| ADDLIB FNAME { arscp_addlib($2, NULL); }
	;

addmod_cmd
	: ADDMOD mod_list { arscp_addmod($2); }
	;

clear_cmd
	: CLEAR { arscp_clear(); }
	;

create_cmd
	: CREATE FNAME { arscp_create(NULL, $2); }
	;

delete_cmd
	: DELETE mod_list { arscp_delete($2); }
	;

directory_cmd
	: DIRECTORY FNAME LP mod_list RP { arscp_dir($2, $4, NULL); }
	| DIRECTORY FNAME LP mod_list RP FNAME { arscp_dir($2, $4, $6); }
	;

end_cmd
	: END { arscp_end(); }
	;

extract_cmd
	: EXTRACT mod_list { arscp_extract($2); }
	;

list_cmd
	: LIST { arscp_list(); }
	;

open_cmd
	: OPEN FNAME { arscp_open($2); }
	;

replace_cmd
	: REPLACE mod_list { arscp_replace($2); }
	;

save_cmd
	: SAVE { arscp_save(); }
	;

verbose_cmd
	: VERBOSE { verbose = !verbose; }
	;
%%

/* ARGSUSED */
static void
yyerror(const char *s)
{

	(void) s;
	printf("Syntax error in archive script, line %d\n", lineno);
}

/*
 * arscp_open first open an archive and check its validity. If the archive
 * format is valid, it calls arscp_create to create a temporary copy of
 * the archive.
 */
static void
arscp_open(char *fname)
{
	struct archive		*a;
	struct archive_entry	*entry;
	int			 r;

	if ((a = archive_read_new()) == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, 0, "archive_read_new failed");
	archive_read_support_compression_all(a);
	archive_read_support_format_all(a);
	AC(archive_read_open_file(a, fname, DEF_BLKSZ));
	if ((r = archive_read_next_header(a, &entry)))
		bsdar_warnc(bsdar, 0, "%s", archive_error_string(a));
	AC(archive_read_close(a));
	AC(archive_read_finish(a));
	if (r != ARCHIVE_OK)
		return;
	arscp_create(fname, fname);
}

/*
 * Create archive. in != NULL indicate it's a OPEN cmd, and resulting
 * archive is based on modification of an existing one. If in == NULL,
 * we are in CREATE cmd and a new empty archive will be created.
 */
static void
arscp_create(char *in, char *out)
{
	struct archive		*a;
	int			 ifd, ofd;

	/* Delete previously created temporary archive, if any. */
	if (tmpac) {
		if (unlink(tmpac) < 0)
			bsdar_errc(bsdar, EX_IOERR, errno, "unlink failed");
		free(tmpac);
	}

	tmpac = strdup(TEMPLATE);
	if (tmpac == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
	if ((ofd = mkstemp(tmpac)) < 0)
		bsdar_errc(bsdar, EX_IOERR, errno, "mkstemp failed");

	if (in) {
		/*
		 * Command OPEN creates a temporary copy of the
		 * input archive.
		 */
		if ((ifd = open(in, O_RDONLY)) < 0) {
			bsdar_warnc(bsdar, errno, "open failed");
			return;
		}
		if (arscp_copy(ifd, ofd)) {
			bsdar_warnc(bsdar, 0, "arscp_copy failed");
			return;
		}
		close(ifd);
		close(ofd);
	} else {
		/*
		 * Command CREATE creates an "empty" archive.
		 * (archive with only global header)
		 */
		if ((a = archive_write_new()) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, 0,
			    "archive_write_new failed");
		archive_write_set_format_ar_svr4(a);
		AC(archive_write_open_fd(a, ofd));
		AC(archive_write_close(a));
		AC(archive_write_finish(a));
	}

	/* Override previous target, if any. */
	if (target)
		free(target);

	target = out;
	bsdar->filename = tmpac;
}

/* A file copying implementation using mmap. */
static int
arscp_copy(int ifd, int ofd)
{
	struct stat		 sb;
	char			*buf, *p;
	ssize_t			 w;
	size_t			 bytes;

	if (fstat(ifd, &sb) < 0) {
		bsdar_warnc(bsdar, errno, "fstate failed");
		return (1);
	}
	if ((p = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, ifd,
	    (off_t)0)) == MAP_FAILED) {
		bsdar_warnc(bsdar, errno, "mmap failed");
		return (1);
	}
	for (buf = p, bytes = sb.st_size; bytes > 0; bytes -= w) {
		w = write(ofd, buf, bytes);
		if (w <= 0) {
			bsdar_warnc(bsdar, errno, "write failed");
			break;
		}
	}
	if (munmap(p, sb.st_size) < 0)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "munmap failed");
	if (bytes > 0)
		return (1);

	return (0);
}



/*
 * Add all modules of archive to current archive, if list != NULL,
 * only those modules speicifed in 'list' will be added.
 */
static void
arscp_addlib(char *archive, struct list *list)
{
	const char	 *tmpdir;
	char		 *cwd, *dir, *path_ac, *path_tmpac;

	/*
	 * ADDLIB cmd is simulated by firstly extracting all members of
	 * archive into a temporary directory and inserting all or part
	 * of those into current archive later.
	 */

	/* Get absolute pathnames for 'archive' and 'tmpac'. */
	if ((cwd = getcwd(NULL, 0)) == NULL)
		bsdar_errc(bsdar, EX_IOERR, errno, "getcwd failed");
	path_ac = arscp_strcat(cwd, archive);
	path_tmpac = arscp_strcat(cwd, tmpac);

	/* Respect TMPDIR environment variable. */
	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || *tmpdir == '\0')
		tmpdir = ".";
	if (chdir(tmpdir) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "chdir %s failed", tmpdir);

	dir = strdup(TEMPLATE);
	if (dir == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
	if ((dir = mkdtemp(dir)) == NULL)
		bsdar_errc(bsdar, EX_IOERR, errno, "mkdtemp failed");
	if (chdir(dir) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "chdir %s failed", dir);

	/* Extract 'archive'. */
	bsdar->filename = path_ac;
	bsdar->argc = 0;
	bsdar->argv = NULL;
	ar_mode_x(bsdar);

	/*
	 * Build argv. If 'list' == NULL, all modules under dir will be
	 * added. If 'list' != NULL, only modules specified by 'list'
	 * will be added.
	 */
	bsdar->filename = path_tmpac;
	if (list)
		arscp_mlist2argv(list);
	else
		arscp_dir2argv(".");
	ar_mode_q(bsdar);
	arscp_free_argv();

	/* Cleanup */
	if (chdir("..") == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "chdir %s failed", tmpdir);
	arscp_delete_dir(dir);
	if (chdir(cwd) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno,
		    "return to working dir failed");
	free(dir);
	free(path_ac);
	free(path_tmpac);
	free(cwd);
	free(archive);
	if (list)
		arscp_free_mlist(list);
	bsdar->filename = tmpac;
}

/* Add modules into current archive. */
static void
arscp_addmod(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_q(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Delete modules from current archive. */
static void
arscp_delete(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_d(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Extract modules from current archive. */
static void
arscp_extract(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_x(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* List modules of archive. (Simple Mode) */
static void
arscp_list()
{

	if (!arscp_target_exist())
		return;
	bsdar->argc = 0;
	bsdar->argv = NULL;
	/* Always verbose. */
	bsdar->options |= AR_V;
	ar_mode_t(bsdar);
	bsdar->options &= ~AR_V;
}

/* List modules of archive. (Advance Mode) */
static void
arscp_dir(char *archive, struct list *list, char *rlt)
{
	FILE	*out;

	/* If rlt != NULL, redirect output to it */
	out = NULL;
	if (rlt) {
		out = stdout;
		if ((stdout = fopen(rlt, "w")) == NULL)
			bsdar_errc(bsdar, EX_IOERR, errno,
			    "fopen %s failed", rlt);
	}

	bsdar->filename = archive;
	arscp_mlist2argv(list);
	if (verbose)
		bsdar->options |= AR_V;
	ar_mode_t(bsdar);
	bsdar->options &= ~AR_V;

	if (rlt) {
		if (fclose(stdout) == EOF)
			bsdar_errc(bsdar, EX_IOERR, errno,
			    "fclose %s failed", rlt);
		stdout = out;
		free(rlt);
	}
	free(archive);
	bsdar->filename = tmpac;
	arscp_free_argv();
	arscp_free_mlist(list);
}


/* Replace modules of current archive. */
static void
arscp_replace(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_r(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Rename the temporary archive to the target archive. */
static void
arscp_save()
{

	if (target) {
		if (rename(tmpac, target) < 0)
			bsdar_errc(bsdar, EX_IOERR, errno, "rename failed");
		free(tmpac);
		free(target);
		tmpac = NULL;
		target= NULL;
		bsdar->filename = NULL;
	} else
		bsdar_warnc(bsdar, 0, "no open output archive");
}

/*
 * Discard changes since last SAVE. This is simulated by executing
 * OPEN or CREATE command on the same target one more time.
 */
static void
arscp_clear()
{
	struct stat	 sb;
	char		*new_target;

	if (target) {
		new_target = strdup(target);
		if (new_target == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
		if (stat(tmpac, &sb) != 0) {
			if (errno != ENOENT)
				bsdar_errc(bsdar, EX_IOERR, errno,
				    "stat %s failed", target);
			arscp_create(NULL, new_target);
		} else
			arscp_open(new_target);
	}
}

/*
 * Quit ar(1). Note that END cmd will not SAVE current archive
 * before exit.
 */
static void
arscp_end()
{

	if (target)
		free(target);
	if (tmpac) {
		if (unlink(tmpac) == -1)
			bsdar_errc(bsdar, EX_IOERR, errno, "unlink %s failed",
			    tmpac);
		free(tmpac);
	}

	exit(EX_OK);
}

/*
 * Check if target spcified, i.e, whether OPEN or CREATE has been
 * issued by user.
 */
static int
arscp_target_exist()
{

	if (target)
		return (1);

	bsdar_warnc(bsdar, 0, "no open output archive");
	return (0);
}

/* Construct module list. */
static struct list *
arscp_mlist(struct list *list, char *str)
{
	struct list *l;

	l = malloc(sizeof(*l));
	if (l == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
	l->str = str;
	l->next = list;

	return (l);
}

/* Calculate the length of a mlist. */
static int
arscp_mlist_len(struct list *list)
{
	int len;

	for(len = 0; list; list = list->next)
		len++;

	return (len);
}

/* Free the space allocated for mod_list. */
static void
arscp_free_mlist(struct list *list)
{
	struct list *l;

	/* Note that list->str was freed in arscp_free_argv. */
	for(; list; list = l) {
		l = list->next;
		free(list);
	}
}

/* Convert mlist to argv array. */
static void
arscp_mlist2argv(struct list *list)
{
	char	**argv;
	int	  i, n;

	n = arscp_mlist_len(list);
	argv = malloc(n * sizeof(*argv));
	if (argv == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");

	/* Note that module names are stored in reverse order in mlist. */
	for(i = n - 1; i >= 0; i--, list = list->next) {
		if (list == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "invalid mlist");
		argv[i] = list->str;
	}

	bsdar->argc = n;
	bsdar->argv = argv;
}

/* Build argv from entries under 'dir'. */
static void
arscp_dir2argv(const char *dir)
{
	struct dirent	 *dp;
	struct stat	  sb;
	DIR		 *dirp;
	char		**argv;
	int		  dfd, i, n;

	dirp = opendir(dir);
	if (dirp == NULL)
		bsdar_errc(bsdar, EX_IOERR, errno, "opendir %s failed", dir);

	dfd = dirfd(dirp);
	if (fstat(dfd, &sb) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "fstat %s failed", dir);

	/* estimate array size */
	n = sb.st_size / 24;
	argv = malloc(n * sizeof(*argv));
	if (argv == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");

	i = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		if (i >= n) {
			n += 10;
			argv = realloc(argv, n * sizeof(*argv));
			if (argv == NULL)
				bsdar_errc(bsdar, EX_SOFTWARE, errno,
				    "realloc failed");
		}
		argv[i] = strdup(dp->d_name);
		if (argv[i] == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
		i++;
	}
	if (closedir(dirp) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "closedir %s failed", dir);

	bsdar->argc = i;
	bsdar->argv = argv;
}

/* Free space allocated for argv array and its elements. */
static void
arscp_free_argv()
{
	int i;

	for(i = 0; i < bsdar->argc; i++)
		free(bsdar->argv[i]);

	free(bsdar->argv);
}

/*
 * Remove contents of 'dir' and 'dir' itself, assuming that 'dir'
 * only contains regular files.
 */
static void
arscp_delete_dir(const char *dir)
{
	struct dirent	*dp;
	DIR		*dirp;
	int		 cur;

	if ((cur = open(".", O_RDONLY)) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "open working dir failed");

	if (chdir(dir) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "chdir %s failed", dir);

	dirp = opendir(".");
	if (dirp == NULL)
		bsdar_errc(bsdar, EX_IOERR, errno, "opendir %s failed", dir);

	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		if (unlink(dp->d_name) == -1)
			bsdar_errc(bsdar, EX_IOERR, errno, "unlink %s failed",
			    dp->d_name);
	}
	if (closedir(dirp) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "closedir %s failed", dir);

	if (fchdir(cur) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno,
		    "return to working dir failed");
	close(cur);

	if (rmdir(dir) == -1)
		bsdar_errc(bsdar, EX_IOERR, errno, "rmdir %s failed", dir);
}

/* Concat two strings with "/". */
static char *
arscp_strcat(const char *path, const char *name)
{
	char	*str;
	size_t	 slen;

	slen = strlen(path) + strlen(name) + 2;
	str = malloc(slen);
	if (str == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");

	snprintf(str, slen, "%s/%s", path, name);

	return (str);
}

/* Show a prompt if we are in interactive mode */
static void
arscp_prompt()
{

	if (interactive) {
		printf("AR >");
		fflush(stdout);
	}
}

/* Main function for ar script mode. */
void
ar_mode_script(struct bsdar *ar)
{

	bsdar = ar;
	interactive = isatty(fileno(stdin));
	while(yyparse()) {
		if (!interactive)
			exit(1);
	}
}
