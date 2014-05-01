/* written by Alastair Poole and Sam Watkins, 2006 - 2009

   this version is released to the public domain.
*/

#define _XOPEN_SOURCE 600
#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>		/* for PATH_MAX in mingw */
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <libgen.h>

#include "arcs_backend.h"

#define ARCS_VERSION "0.9.9.6~wip"

#ifdef __MINGW32_MAJOR_VERSION
#include <windows.h>
#include <fcntl.h>
#define SLASH "\\"
#define S_ISLNK(mode) 0
#define lstat stat
#define sleep(n) (Sleep(1000*n))
/* or use Sleep() from winbase.h */
#define mkdir(path, mode) mkdir(path)
#define LLU_FMT "%I64u"
#define WIFEXITED(x) 1
#define WEXITSTATUS(x) x
#else
#include <sys/wait.h>
#define LLU_FMT "%llu"
#define SLASH "/"
#endif

#ifndef uint
typedef unsigned int uint;
#endif

#define FAILURE 0
#define SUCCESS 1

#define STATUS_PULLED_NOTHING 123

int scan_delay = 2;
int pull_interval = 10;
int safe_exit = 0;
int quiet_level = 1;
 // 0 verbose, 1 default quiet, 2 very quiet, 3 silent
int verbose = 0;
int VERBOSE = 0;
int init_new = 0;
int no_action = 0;
int rec_after_pull = 0;
int command_failed = 0;
time_t now;
int loop = 0;
int read_proj = 0;
int parallel = 0;
int parallel_max = 10;
double parallel_sleep = 0.1;
char *get_from = NULL;
char *get_to = NULL;
char *put_from = NULL;
char *put_to = NULL;
int peace_which = 0;
int go_up_to_find_project_dir = 1;
int no_dirs;
int use_link_tree = 1;
int arcs_shared = -1;

int do_rec = 1;
int do_rrec = 1;
int do_pull = 1;
int do_push = 1;
int do_push_always = 0;
//list_t *push_list = NULL;
//list_t *pull_list = NULL;
int do_setup_make = 0;
int do_edit_peers = 0;
int do_set_task = 0;
int do_detach_peers = 0;
int do_forget_history = 0;
int do_remove_arcs = 0;
int do_commit_all = 0;
int do_conflicts = 0;
int do_peace = 0;
int do_make = 1;
int do_diff = 0;
int do_patch = 0;
int do_unpatch = 0;
char *msg = NULL;

char *arcs_dir = ".arcs";
char *tree_dir = ".arcs" SLASH "tree";
char *db_file = ".arcs" SLASH "db.txt";
char *db_file_new = ".arcs" SLASH "db.txt.new";
char *peers_file = ".arcs" SLASH "peers";
char *make_file = ".arcs" SLASH "make";
char *task_file = ".arcs" SLASH "task";
char *htaccess_file = ".htaccess";
char *htaccess_content = "deny from all\n";
char *_projects_file = ".arcs_projects";
char projects_file[PATH_MAX];
char *need_push_file = ".arcs" SLASH "need_push";

typedef unsigned long long file_id_t;

typedef struct node_t node_t;
struct node_t {
	char *path;
	file_id_t file_id;
	unsigned int mtime;
	unsigned int exists;
	node_t *next;
};

typedef struct list_t list_t;
struct list_t {
	list_t *next;
	void *node;
};

typedef struct location_t location_t;
struct location_t {
	char *address;
	char *user;
	char *host;
	char *dir;
};

typedef struct project_t project_t;
struct project_t {
	char *path;
	node_t *database;
	list_t *peers;
	location_t *here;
	int changed;
};

char *Strcat(char *dest, const char *src)
{
	return strncat(dest, src, PATH_MAX);
}

void arcs_error(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, format, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(EXIT_FAILURE);
}

char *Getcwd(void)
{
	char buf[PATH_MAX];
	if (!getcwd(buf, sizeof(buf)))
		arcs_error("getcwd()");
	return strdup(buf);
}

char *abs_path(char *path)
{
	char buf[PATH_MAX];
	if (path[0] == '/')
		return strdup(path);
	if (path[0] == '.') {
		++path;
		while (path[0] == '/')
			++path;
	}
	path = Strcat(Strcat(strcpy(buf, Getcwd()), "/"), path);
	return strdup(path);
}

char *memlog_file = ".memlog";
FILE *memlog = NULL;

void Malloc_debug_open(void)
{
#ifdef MEMLOG
	memlog = fopen(memlog_file, "w");
	if (!memlog)
		arcs_error("cannot open memlog file");
#endif
}

void Malloc_debug_close(void)
{
#ifdef MEMLOG
	fclose(memlog);
#endif
}

#define Malloc(size) _Malloc(size, __FILE__, __LINE__)

#define Free(ptr) _Free(ptr, __FILE__, __LINE__)

void *_Malloc(size_t size, char *file, int line)
{
	void *tmp = malloc(size);
	if(!tmp)
		arcs_error("malloc()");
#ifdef MEMLOG
	fprintf(memlog, "malloc\t%08x\t%d\t%s:%d\n", (uint)tmp, size, file, line);
#else
	file = file; line = line;
#endif
	return tmp;
}

void _Free(void *ptr, char *file, int line)
{
#ifdef MEMLOG
	fprintf(memlog, "free\t%08x\t\t%s:%d\n", (uint)ptr, file, line);
#else
	file = file; line = line;
#endif
	free(ptr);
}

list_t *list_new(list_t *next, void *node)
{
	list_t *tmp = Malloc(sizeof(list_t));
	tmp->next = next;
	tmp->node = node;
	return tmp;
}

typedef void (*free_t)(void *);

void list_free_each(list_t *list, free_t free_member)
{
	list_t *cursor = list;
	list_t *next;

	while (cursor) {
		next = cursor->next;
		if (cursor->node)
			free_member(cursor->node);
		Free(cursor);
		cursor = next;
	}
}

void list_free(list_t *list)
{
	list_t *cursor = list;
	list_t *next;

	while (cursor) {
		next = cursor->next;
		if (cursor->node)
			Free(cursor->node);
		Free(cursor);
		cursor = next;
	}
}

static void help(char *program)
{
	char *i = strrchr(program, '/');
	if (i) program = i+1;
	i = strrchr(program, '\\');
	if (i) program = i+1;
	i = strstr(program, ".exe");
	if (i) *i = '\0';
	printf("Usage: %s [OPTIONS] [projdir ...]\n", program);

	printf("OPTIONS:\n");
	printf("    -h --help      this menu\n");
	printf("    -V --version   version information\n");
	printf("    -v             verbose mode (up to -v -v)\n");
	printf("    -q             quiet mode (up to -q -q)\n");
	printf("    -i             init new repository\n");
	printf("    --get fr [to]  import host:dir [dir] - get a repository\n");
	printf("    --put fr to    output dir host[:dir] - put a repository\n");
	printf("    -l             run in a loop\n");
	printf("    -s <secs>      sleep for secs between scans (2)\n");
	printf("    -S <scans>     sleep for number of scans between push/pull (10)\n");
	printf("    -z <secs>      small sleep between forking children (0.1)\n");
	printf("    -m <message>   commit message\n");
	printf("    -t <message>   set .arcs/task (persistent commit message)\n");
	printf("    -n             take no action, only show what would have happened\n");
	printf("    -N             no network, don't push out or pull in\n");
	printf("    -O             don't push out\n");
	printf("    -I             don't pull in\n");
	printf("    --push         push even if it seems unnecessary\n");
	printf("    -L             do not record local (can use to create an empty repository)\n");
	printf("    -R             do not record remote (with pull, implies -O, conflicts --put)\n");
	printf("    -M             don't run make\n");
	printf("    -a             work on multiple projects listed in ~/.arcs_projects or args\n");
#ifndef __MINGW32_MAJOR_VERSION
	printf("    -f [forks]     fast parallel mode (messier output)\n");
#endif
	printf("    -d             diff\n");
	printf("    -p             patch\n");
	printf("    -u             unpatch\n");
	printf("    -E             commit ELF files and .o .a .so .exe .dll\n");
	printf("    -T             (with -i) do not create link tree, for /etc\n");
	printf("    -c             cooperative - make shared repositories / checkouts\n");
	printf("    -C             not cooperative - make unshared repositories / checkouts\n");
	printf("    --log          show changelog\n");
	printf("    --hist         show change history\n");
	printf("    --make         setup Makefile to work with arcs\n");
	printf("    --peers        edit .arcs/peers\n");
	printf("    --projects     edit ~/.arcs/projects\n");
	printf("    --detach       clear .arcs/peers\n");
	printf("    --forget       forget all history here and elsewhere *eek*\n");
	printf("    --remove       remove .arcs and .git dirs from a module *eek*\n");
	printf("    --commit-all   commit all files, in case of problems\n");
	printf("    --conflicts    list files having conflicts in them\n");
	printf("    --peace 1|2 f* resolve conflicts (takes a list of files)\n");
	printf("    --rap          record changes after a pull (internal)\n");
	printf("\n");
	printf("By Sam Watkins and Alastair Poole\n");

	exit(EXIT_SUCCESS);
}

//	printf("    -o           push to all peers (default)\n");
//	printf("    -i           pull from all peers (default)\n");
//	printf("*   -o [peers]   don't push to these peers\n");
//	printf("*   -i [peers]   don't pull from these peers\n");
//	printf("    +o [peers]   push to only these peers\n");
//	printf("    +i [peers]   pull from only these peers\n");
//	printf("*   not working yet\n\n");

static int first_run_test(char *location)
{
	struct stat fstats;
	int status;

	status = stat(location, &fstats);
	if (status < 0)
		return SUCCESS;
	else
		return FAILURE;
}

#ifndef __MINGW32_MAJOR_VERSION
void link_add(char *path, file_id_t inode)
{
	char tmp[PATH_MAX];

	if (!use_link_tree)
		return;
	
	snprintf(tmp, PATH_MAX, "%s" SLASH LLU_FMT, tree_dir, inode);
	
	unlink(tmp);	
	link(path, tmp);	
}

void link_del(file_id_t inode)
{
	char tmp[PATH_MAX];
	
	if (!use_link_tree)
		return;
	
	snprintf(tmp, PATH_MAX, "%s" SLASH LLU_FMT, tree_dir, inode);
	
	unlink(tmp);	
}
#endif

static int System(const char *cmd)
{
	int status = 0;
	fflush(stdout);
	if(VERBOSE)
		fprintf(stderr, "  %s\n", cmd);
	if(!no_action) {
#ifdef __MINGW32_MAJOR_VERSION
		/* split on semicolons */
		char *buf = strdup(cmd);
		char *end = buf;
		int in_quoted = 0, in_squoted = 0;
		while (1) {
			if (*end == '\0') {
				status = system(buf);
				break;
			} else if (*end == '"') {
				in_quoted = !in_quoted;
				++end;
			} else if (*end == '\'') {
				in_squoted = !in_squoted;
				++end;
			} else if (!in_quoted && !in_squoted && *end != '\\' && end[1] == ';') {
				++end;
				*end = '\0';
				status = system(buf);
				++end; buf = end;
			} else {
				++end;
			}
		}
		Free(buf);
#else
		status = system(cmd);
#endif
		if (status == -1 || !WIFEXITED(status))
			arcs_error("could not run child process");
		status = WEXITSTATUS(status);
		/* XXX - it only checks final status.  this is consistent with shell but not what we really want for arcs */
		if (status != 0) {
			if (!rec_after_pull && status != STATUS_PULLED_NOTHING) {
	/*			fprintf(stderr, "Warning: sub-command failed.  Not updating database:\n  %s\n", cmd); */
				if (quiet_level < 3) {
					fprintf(stderr, "Warning: sub-command failed %d: %s\n", status, cmd);
				}
//				command_failed = 1;
//				making it fail and not update the db seems to cause more problems than not because it's usually a failure due to trying to commit nothing.
			}
		}
	}

	return status;
}

/* Quoting arguments for the shell */
/* The "to" buffer must have enough space, allow 2 * strlen(from)+1. */
/* It adds a null and returns the address of the null. */
#ifndef __MINGW32_MAJOR_VERSION
static char *sh_quote(const char *from, char *to)
{
	char c;
	while (1) {
		c = *from;
		if (c == '\0')
			break;
		if ((c >= 'A' && c <= 'Z') ||
		    (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || strchr("-_./", c) != NULL) {
			/* doesn't need escaping */
			*to = c;
			++to;
		} else {
			to[0] = '\\';
			to[1] = c;
			to += 2;
		}
		++from;
	}
	*to = '\0';
	return to;
}
#else
static char *sh_quote(const char *from, char *to)
{
	int quote = strchr(from, ' ') ? 1 : 0;
	if (quote)
		*to++ = '"';
	strcpy(to, from);
	to += strlen(from);
	if (quote)
		*to++ = '"';
	return to;
}
#endif

static int vcommand(const char *cmd, va_list ap)
{
	char tmpstr[4096];
	char *args[10];
	int arg_count = 0;

	const char *in = cmd;
	char *out = tmpstr;

	while (*in) {
		if (*in == '$' && in[1] >= '1' && in[1] <= '9') {
			char *arg;
			int which_arg;
			++in;
			which_arg = *in - '1';	/* starts from 1 */
			++in;
			while (arg_count <= which_arg) {
				args[arg_count++] = va_arg(ap, char *);
			}
			arg = args[which_arg];
			if (out - tmpstr + strlen(arg) * 2 + 1 >
			    sizeof(tmpstr)) {
				arcs_error("vcommand: command too long");
			}
			out = sh_quote(arg, out);
		} else if (*in == '$' && in[1] == 'M') {
			in+=2;
			if (!do_make) {
				*out = '\0';
				strcat(out, "-M");
				out+=2;
			}
		} else if (*in == '$' && in[1] == 'q') {
			in+=2;
			int i;
			for (i=1; i<quiet_level; ++i) {
				*out = '\0';
				strcat(out, "-q ");
				out+=3;
			}
			--out;
		} else if (*in == '$' && in[1] == 'v') {
			in+=2;
			int i;
			for (i=1; i>quiet_level; --i) {
				*out = '\0';
				strcat(out, "-v ");
				out+=3;
			}
			--out;
		} else if (*in == '$' && in[1] == 'Q') {
			in+=2;
#ifndef __MINGW32_MAJOR_VERSION
			if (quiet_level > 0) {
				*out = '\0';
				strcat(out, ">/dev/null 2>&1");
				out+=strlen(out);
			}
#endif
		} else if (*in == '$' && in[1] == 'R') {
			in+=2;
			if (rec_after_pull) {
				*out = '\0';
				strcat(out, "|| true");
				out+=strlen(out);
			}
		} else {
			if ((unsigned)(out - tmpstr + 1) > sizeof(tmpstr)) {
				arcs_error("vcommand: command too long");
			}
			*out = *in;
			++in;
			++out;
		}
	}
	*out = '\0';

	return System(tmpstr);
}

static int command(const char *cmd, ...)
{
	int status;
	va_list ap;
	va_start(ap, cmd);
	status = vcommand(cmd, ap);
	va_end(ap);
	return status;
}

static int lcommand(char * const*argv)
{
	char tmpstr[4096];
	char *end = tmpstr+sizeof(tmpstr);
	char *i = tmpstr;
	while (*argv) {
		if (end - i < (int)(2*strlen(*argv)+2))
			arcs_error("lcommand: command too long");
		if (i > tmpstr)
			*i++ = ' ';
		i = sh_quote(*argv, i);
		++argv;
	}
	return System(tmpstr);
}

static int record(char *task)
{
	return command(ARCS_REC, task);
}

static void arcs_rrec(char *peer)
{
	// remote record
	if (verbose)
		fprintf(stderr, "rrec\n");
	command(ARCS_RREC, peer);
}

static void push(char *peer, char *base)
{
	if (verbose)
		fprintf(stderr, "push\n");
	command(ARCS_PUSH, peer, base);
}

static int pull(char *peer)
{
	if (verbose)
		fprintf(stderr, "pull\n");
	return command(ARCS_PULL, peer);
}

static void make(void)
{
	char make_command[PATH_MAX];
#ifdef __MINGW32_MAJOR_VERSION
	strcpy(make_command, "make");
#else
	strcpy(make_command, make_file);
#endif
	if (quiet_level > 1)
		Strcat(make_command, " $Q");
	if (verbose)
		fprintf(stderr, "make\n");
	command(make_command);
}

void chomp(char *str)
{
	while (*str) {
		if (*str == '\n') {
			*str = '\0';
			return;
		}

		str++;
	}
}

/* This one's a bit nasty...*/

static int tokenize(char *line, char *path, file_id_t *file_id, int *mtime)
{
	char *p;
	const char delim[] = "\t";

	p = strtok(line, delim);
	if (p) {
		strcpy(path, p);
		p = strtok(NULL, delim);
		if (p) {
			*file_id = atoll(p);
			p = strtok(NULL, "\0");
			if (p) {
				*mtime = atoi(p);
				return SUCCESS;
			} else
				return FAILURE;
		} else
			return FAILURE;

	} else
		return FAILURE;
}


static node_t *node_alloc(void)
{
	node_t *tmp = Malloc(sizeof(node_t));

	tmp->next = NULL;
	tmp->path = NULL;
	tmp->exists = 0;

	return tmp;
}

#ifdef __MINGW32_MAJOR_VERSION
static file_id_t get_create_time(char *path)
{
	HANDLE fh = CreateFile(path, FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (fh == INVALID_HANDLE_VALUE) {
// apparently we can't read directory create time with this :/
//		fprintf(stderr, "Error: Could not CreateFile (open to check file attributes) %s", path);
		return 0;
		/* FIXME use GetLastError, FormatError ... ? */
	}
	FILETIME ctime;
	if (!GetFileTime(fh, &ctime, NULL, NULL)) {
		arcs_error("Error: Could not GetFileTime %s", path);
	}

/*	printf("dwHighDateTime dwLowDateTime are %d %d\n", ctime.dwHighDateTime, ctime.dwLowDateTime); */

	if (!CloseHandle(fh)) {
		arcs_error("Error: Could not CloseHandle %s", path);
	}

	file_id_t file_id = ctime.dwLowDateTime;
	file_id |= ((file_id_t)ctime.dwHighDateTime) << 32;

/*	printf("file_id is " LLU_FMT "\n", file_id); */
	
	return file_id;
}

static void set_create_time(char *path, file_id_t file_id)
{
	HANDLE fh = CreateFile(path, FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (fh == INVALID_HANDLE_VALUE) {
		arcs_error("Error: Could not CreateFile (open to check file attributes) %s", path);
		/* FIXME use GetLastError, FormatError ... ? */
	}
	FILETIME ctime;
	ctime.dwLowDateTime = (unsigned int)file_id;
	ctime.dwHighDateTime = (unsigned int)(file_id >> 32);
	if (!SetFileTime(fh, &ctime, NULL, NULL)) {
		arcs_error("Error: Could not SetFileTime %s", path);
	}

	if (!CloseHandle(fh)) {
		arcs_error("Error: Could not CloseHandle %s", path);
	}
}
#endif

static void node_add(node_t * database, char *path, file_id_t file_id,
	 unsigned int mtime)
{
	node_t *cursor = database;

#ifdef __MINGW32_MAJOR_VERSION
	file_id_t file_id_orig = file_id;
#endif

	while (cursor->next) {
		cursor = cursor->next;
#ifdef __MINGW32_MAJOR_VERSION
		if (file_id == cursor->file_id) {
			/* try again if we find a duplicate ctime */
/*			file_id -= 1e5;  */  /* 10 milli seconds / 100 nano secs */
			file_id -= 2e7;      /* 2 seconds / 100 nano secs */
			cursor = database;
		}
#endif
	}

#ifdef __MINGW32_MAJOR_VERSION
	if (file_id != file_id_orig) {
		set_create_time(path, file_id);
		file_id_t test = get_create_time(path);
		if (test == file_id) {
			if (verbose)
				fprintf(stderr, "changed file_id of %s from " LLU_FMT " to " LLU_FMT "\n",
				       path, file_id_orig, file_id);
		} else {
			arcs_error("Error: Could not change file_id (create_time) of %s from " LLU_FMT " to " LLU_FMT, path, file_id_orig, file_id);
		}
	}
#endif

	if (!cursor->next) {
		cursor->next = node_alloc();
		cursor = cursor->next;
		cursor->path = strdup(path);
		cursor->file_id = file_id;
		cursor->mtime = mtime;
	} else 
		arcs_error("node_add: Should not be here!");
}

static int test_elf(char *path)
{
	char buf[4];
	int ret = FAILURE;

	FILE *fp = fopen(path, "r");
	if(!fp) 
		arcs_error("test_elf: fopen()");

	fread(buf, sizeof(buf), 1, fp);

	if(!memcmp(buf, "\177ELF", sizeof(buf)))
		ret = SUCCESS;

	fclose(fp);

	return ret;
}


static int test_bin_file(char *path)
{
	struct stat fstats;

	if(lstat(path, &fstats)<0)
		arcs_error("test_bin_file: lstat()");

	if (S_ISREG(fstats.st_mode)) {
		char *start = strrchr(path, '.');
		if (start)
			if(!strcasecmp(start, ".o") || !strcasecmp(start, ".a") || !strcmp(start, ".so") || !strcasecmp(start, ".exe") || !strcasecmp(start, ".dll") || !strcasecmp(start, ".class"))
				return SUCCESS;
		if ((fstats.st_mode & S_IXUSR) && test_elf(path)) {
			return SUCCESS;
		}
	}
	
	return FAILURE;					
}

static void node_free(node_t * database)
{
	node_t *cursor;
	node_t *forward;

	cursor = database;

	while (cursor) {
		forward = cursor->next;
		if (cursor->path)
			Free(cursor->path);
		Free(cursor);
		cursor = forward;
	}
}

static node_t *node_find(node_t * testdb, file_id_t file_id)
{
	node_t *cursor = testdb->next;
	while (cursor) {
		if (cursor->file_id == file_id) {
			return cursor;
		}
		cursor = cursor->next;
	}
	return NULL;
}

static node_t *node_find_by_path(node_t * testdb, char *path)
{
	node_t *cursor = testdb->next;
	while (cursor) {
		if (!strcmp(cursor->path, path)) {
			return cursor;
		}
		cursor = cursor->next;
	}
	return NULL;
}

static int elf_repellant = 1;

static void find_files(char *location, FILE * fp, node_t * database, int write_file)
{
	DIR *dir;
	struct stat fstats;
	struct dirent *tmp = NULL;
	char *directories[8192] = { NULL };	/* Is this silly? */
	char fullpath[PATH_MAX];
	int i = 0;

	lstat(location, &fstats);
	if (S_ISLNK(fstats.st_mode))
		return;

	dir = opendir(location);
	if (!dir)
		return;

	while ((tmp = readdir(dir)) != NULL) {
		if ((strncmp(tmp->d_name, ".", 1))
		    && (strncmp(tmp->d_name, "_", 1))
		    && (!strchr(tmp->d_name, '~'))
		    && (strcmp(tmp->d_name, ".."))
		    && (strcmp(tmp->d_name, "CVS"))
		    && (strcmp(tmp->d_name, "RCS"))) {

			if (!strcmp(location, SLASH))
				Strcat(strcpy(fullpath, SLASH), tmp->d_name);
			else if (!strcmp(location, "."))
				strcpy(fullpath, tmp->d_name);
			else
				Strcat(Strcat
				       (strcpy(fullpath, location), SLASH),
				       tmp->d_name);

			if (elf_repellant && test_bin_file(fullpath))
				continue;


			lstat(fullpath, &fstats);

			file_id_t file_id;

			if (fstats.st_ino != 0) {
				file_id = fstats.st_ino;
				file_id |= ((file_id_t)fstats.st_dev) << 32;
			} else {
				/* for win32 fat/ntfs under mingw, st_ino is 0, use "create time"
				 * and make sure it's not duplicated */

#ifdef __MINGW32_MAJOR_VERSION
				file_id = get_create_time(fullpath);
#else
				arcs_error("ARCS does not yet support filesystems without inodes (e.g. FAT, NTFS) on UNIX!");
#endif
			}

			if (S_ISDIR(fstats.st_mode))
				directories[i++] = strdup(fullpath);
#ifndef __MINGW32_MAJOR_VERSION
			else if (fstats.st_nlink == 1)
				link_add(fullpath, file_id);
#endif

			time_t mtime;
//#ifdef __MINGW32_MAJOR_VERSION
			mtime = fstats.st_mtime;
//#else
//			mtime = fstats.st_ctime;
//#endif
			if (mtime >= now) {
				if (verbose)
					fprintf(stderr, "reduced mtime recorded for %s from %d to %d\n", fullpath, (int)mtime, (int)(now - 1));
				mtime = now - 1;
				/* this is in case the file is still being
				 * written during the currect second,
				 * so it will definitely be detected as
				 * modified again in the next scan */

				/* TODO should we in fact not commit any change
				 * that is not at least 1 second old? */
			}
			if (write_file)
				fprintf(fp, "%s\t" LLU_FMT "\t%d\n", fullpath,
					file_id,
					(unsigned int) mtime);
			node_add(database, fullpath, file_id,
				 mtime);
		}
	}

	i = 0;

	while (directories[i] != NULL) {
		find_files(directories[i], fp, database, write_file);
		Free(directories[i++]);
	}

	closedir(dir);
}

static int generate_database(char *root, char *location, node_t * database,
		  int write_file)
{
	FILE *fp;
	if (write_file) {
		remove(location);
		fp = fopen(location, "w");
		if (!fp) {
			arcs_error("Error: Could not open %s for use as database", location);
		}
	}
	
	find_files(root, fp, database, write_file);

	if (write_file)
		fclose(fp);

	return SUCCESS;
}

static int populate_database(char *location, node_t * database)
{
	FILE *fp;
	int status = 0;
	char buf[PATH_MAX+512];
	char *p;
	int mtime;
	file_id_t file_id;
	char path[PATH_MAX];

	fp = fopen(location, "r");
	if (!fp) {
		arcs_error("Error: Unable to open %s to read database", location);
	}

	while ((p = fgets(buf, sizeof(buf), fp)) != NULL) {
		chomp(buf);

		status = tokenize(buf, path, &file_id, &mtime);
		if (!status) 
			arcs_error("Database broken");

		node_add(database, path, file_id, mtime);
	}

	fclose(fp);

	return SUCCESS;
}

typedef enum { FT_FILE, FT_DIR, FT_LNK, FT_OBJ } file_type;
char *file_type_str[4] = { "file", "dir", "link", "obj" };

file_type get_file_type(char *path)
{
	struct stat stats;
	file_type type = FT_OBJ;
	if (!lstat(path, &stats)) {
		if (S_ISREG(stats.st_mode))
			type = FT_FILE;
		else if (S_ISDIR(stats.st_mode))
			type = FT_DIR;
		else if (S_ISLNK(stats.st_mode))
			type = FT_LNK;
	}
	return type;
}

static int compare_databases(node_t * database, node_t * testdb)
{
	node_t *cursor = database->next;
	node_t *tmp;
	int changed = 0;
	file_type type;

	for (; cursor; cursor = cursor->next) {
		type = FT_OBJ;
		tmp = node_find(testdb, cursor->file_id);

		if (tmp) {
			type = get_file_type(tmp->path);

			tmp->exists = 1;
			if (strcmp(tmp->path, cursor->path)) {
				if (quiet_level < 2) {
					printf("move  %4s  %s -> %s\n",
					       file_type_str[type], cursor->path, tmp->path);
				}
				command(ARCS_MOVE, cursor->path, tmp->path);
				changed = 1;
			}
		} else {
			tmp = node_find_by_path(testdb, cursor->path);
			if (tmp) {
				type = get_file_type(tmp->path);
				tmp->exists = 1;
				/* file_id numbers must be different */
				if (quiet_level < 2) {
					printf("renum %4s  %s\n",
					       file_type_str[type], tmp->path);
				}
#ifndef __MINGW32_MAJOR_VERSION
				link_del(tmp->file_id);
				link_add(cursor->path, cursor->file_id);
#endif
			}
		}

		if (tmp) {
			type = get_file_type(tmp->path);
			if (tmp->mtime != cursor->mtime && type != FT_DIR) {
				if (quiet_level < 2) {
					printf("mod   %4s  %s\n",
					       file_type_str[type], tmp->path);
				}
				changed = 1;
				command(ARCS_MOD, tmp->path);
			}
		} else {
			if (quiet_level < 2) {
				printf("del   %4s  %s\n",
				       file_type_str[type], cursor->path);
			}
			changed = 1;
			if (!(no_dirs && type == FT_DIR))
				command(ARCS_REM, cursor->path);
#ifndef __MINGW32_MAJOR_VERSION
			link_del(cursor->file_id);
#endif
		}
	}

	/* test for new files */

	cursor = testdb->next;

	while (cursor) {
		if (!cursor->exists) {
			type = get_file_type(cursor->path);
			if (quiet_level < 2) {
				printf("add   %4s  %s\n",
				       file_type_str[type], cursor->path);
			}
			if (!(no_dirs && type == FT_DIR))
				command(ARCS_ADD, cursor->path);
#ifndef __MINGW32_MAJOR_VERSION
			link_add(cursor->path, cursor->file_id);
#endif
			changed = 1;
		}

		cursor = cursor->next;
	}

	return changed;
}

//static void catch_signal(int sig)
//{
//	if(sig) {
//		fprintf(stderr, "caught signal - exiting\n");
//		safe_exit = 1;
//	}
//}
//
//typedef void (*sighandler_t)(int);
//
//static sighandler_t old_int_handler;
//static sighandler_t old_term_handler;
//
//static void dont_kill_me(void)
//{
//	old_int_handler = signal(SIGINT, catch_signal);
//	old_term_handler = signal(SIGTERM, catch_signal);
//}
//
//static void ok_I_can_die_now_jk(void)
//{
//	signal(SIGINT, old_int_handler);
//	signal(SIGTERM, old_term_handler);
//}

void arcs_here_i_am(location_t *here)
{
	/* No need to include the time */
 
	if (quiet_level < 2) {
		fprintf(stderr, "ARCS %s %s at %s %s\n", ARCS_VERSION, ARCS_BACKEND, here->address, arcs_shared ? "shared" : "");
	}
}

void create_file(char *file, char *content)
{
	if (no_action)
		return;
	struct stat fstats;
	if (stat(file, &fstats) != 0) {
		FILE *fp = fopen(file, "a");
		if(!fp)
			arcs_error("can't open / create file: %s", file);
		fprintf(fp, "%s", content);
		fclose(fp);
	}
}

void arcs_init(char *arcs_dir, location_t *here)
{
	char buf[PATH_MAX];
	char *user;

	struct stat fstats;
	if (stat(arcs_dir, &fstats) != 0) {
		if (init_new && !no_action) {
			mkdir(arcs_dir, 0777); 
			if (use_link_tree)
				mkdir(tree_dir, 0777);
		} else {
			arcs_error("arcs_init: %s does not exist, and -i not passed", arcs_dir);
		}
	}
	if (stat(tree_dir, &fstats) != 0) {
		use_link_tree = 0;
	}

	create_file(peers_file, "");

	create_file(task_file, ".");

	user = getenv("USER");
	if (!user)
		user = getenv("USERNAME");
	if (!user)
		arcs_error("arcs_init: getenv(\"USER\")");
	here->user = strdup(user);

	if (gethostname(buf, sizeof(buf))) {
		char *tmp = getenv("COMPUTERNAME");
		if (!tmp)
			tmp = "localhost";
		here->host = strdup(tmp);
	} else {
		here->host = strdup(buf);
	}

	here->dir = Getcwd();

	snprintf(buf, sizeof(buf), "%s@%s:%s",
		 here->user, here->host, here->dir);
	here->address = strdup(buf);

	if (stat(ARCS_DIR, &fstats) < 0) {
		if (ARCS_INIT == NULL) 
			arcs_error("This backend cannot be used to create, you must first pull a repository");
		command(ARCS_INIT);
	}

	char *htaccess_file_1 = ".arcs" SLASH ".htaccess";
	char htaccess_file_2[PATH_MAX];
	Strcat(Strcat(strcpy(htaccess_file_2, ARCS_DIR), SLASH), ".htaccess");

	create_file(htaccess_file_1, htaccess_content);
	create_file(htaccess_file_2, htaccess_content);
}

void version_info(void)
{
	printf("%s\n", ARCS_VERSION);
	exit(EXIT_SUCCESS);
}

list_t *reverse_list(list_t *list)
{
	list_t *prev = NULL;
	list_t *next;
	while (list) {
		next = list->next;
		list->next = prev;
		prev = list;
		list = next;
	}
	return prev;
}

char * /*M*/	path_abs(char *rel)
{
	if (rel[0] == SLASH[0])
		return strdup(rel);

	char buf[PATH_MAX];
	char *cwd = Getcwd();
	snprintf(buf, PATH_MAX, "%s" SLASH "%s", cwd, rel);
	Free(cwd);
	return strdup(buf);
}

char *	path_rel_to_if_under(char *path /*M*/, char *base)
{
	int i = strlen(base);
	
	if (strncmp(path, base, i) == 0 && path[i] == SLASH[0]) {
		char *t = strdup(path + i+1);
		Free(path);
		path = t;
	}
	return path;
}

list_t *read_peers(char *peers_file, location_t *here)
{
	char buf[PATH_MAX];
	list_t *peers = NULL;
	char *peer;

	char *home_relative_here_dir = path_rel_to_if_under(path_abs(here->dir), getenv("HOME")); /* M */

	FILE *fp = fopen(peers_file, "r");
	if(!fp) 
	arcs_error("%s read_peers: fopen()", here->address);
	
	while (fgets(buf, sizeof(buf), fp)) {
		chomp(buf);
		if (strlen(buf) == 0 || buf[0] == '#')
			continue;
		if (strchr(buf, ':') == NULL) {
			Strcat(Strcat(buf, ":"), home_relative_here_dir);
		}
		if (strchr(buf, '@') == NULL) {
			int len = strlen(here->user);
			memmove(buf + len+1, buf, strlen(buf)+1);
			strcpy(buf, here->user);
			buf[len] = '@';
		}
		peer = strdup(buf);
		peers = list_new(peers, peer);
	}

	fclose(fp);

	free(home_relative_here_dir);

	return reverse_list(peers);
}

int group_can_write(char *path)
{
	(void)path;
#ifndef __MINGW32_MAJOR_VERSION
	struct stat stats;
	if (!lstat(path, &stats)) {
		return (stats.st_mode & S_IWGRP) && 1;
	}
#endif
	return 0;
}

#ifdef __MINGW32_MAJOR_VERSION
int setenv(const char *name, const char *value, int overwrite)
{
	if (overwrite || !getenv(name)) {
		char *kv = malloc(strlen(name)+strlen(value)+2);
		if (!kv)
			return -1;
		strcpy(kv, name);
		strcat(kv, "=");
		strcat(kv, value);
		if (putenv(kv) == -1)
			return -1;
	}
	return 0;
}
#endif

void project_chdir(project_t *project)
{
	char dir[PATH_MAX];
	char *home = getenv("HOME");
	if (chdir(home))
		arcs_error("can't chdir $HOME");
	if (chdir(project->path)) {
		if (init_new && !no_action) {
			if (mkdir(project->path, 0777) || chdir(project->path))
				arcs_error("can't create project: %s", project->path);
		} else {
			arcs_error("can't chdir to project path: %s", project->path);
		}
	}

	if (arcs_shared == -1)
		arcs_shared = group_can_write(".");

	// TODO make the checkout shared or not?

	if (arcs_shared)
		umask(0002);  /* we want arcs to work for groups */
	 else
		umask(0022);

	setenv("SSHC_SHARED", arcs_shared ? "1" : "0", 1);

	if (go_up_to_find_project_dir) {
		struct stat fstats;
		while (!init_new && stat(arcs_dir, &fstats)) {
			int finished = 0;
			if (getcwd(dir, PATH_MAX) == NULL) {
				finished = 1;
			} else {
				int c = dir[strlen(dir)-1];
				if (c == '/' || c == '\\') {
					finished = 1;
				}
			}
			if (finished) {
				arcs_error("cannot find arcs project directory at or above: %s", project->path);
			}
			
			chdir("..");
		}
	}
}

void location_free(location_t *location)
{
	location_t *cursor = location;

	if (!cursor)
		return;

	if (cursor->address)
		Free(cursor->address);
	if (cursor->user)
		Free(cursor->user);
	if (cursor->host)
		Free(cursor->host);
	if (cursor->dir)
		Free(cursor->dir);

	Free(cursor);	
}

void project_free(project_t *project)
{
	Free(project->path);
	location_free(project->here);
	list_free(project->peers);
	node_free(project->database);
	Free(project);
}

project_t *project_new(char *path)
{
	project_t *project = Malloc(sizeof(project_t));

	project->path = path;
	project->database = node_alloc();

	project_chdir(project);

	project->here = Malloc(sizeof(location_t));  /* could alloc it direct in the struct */

	arcs_init(arcs_dir, project->here);

	project->peers = read_peers(peers_file, project->here);

	struct stat fstats;
	if (stat(ARCS_DIR, &fstats) < 0) {
		arcs_error("Not a valid repository: %s", path);
	}
	if (stat("build-stamp", &fstats) == 0) {
		arcs_error("You need to run `fakeroot debian/rules clean` in: %s", path);
	}

	int status = first_run_test(db_file);
	if (!status)
		populate_database(db_file, project->database);

	return project;
}

list_t *read_projects(char *projects_file, int *count, int argc, char **argv)
{
	char buf[PATH_MAX];
	list_t *projects = NULL;

	if (!*argv) {
		argv = &projects_file;
		argc = 1;
	}

	char *home = getenv("HOME");
	if (chdir(home))
		arcs_error("can't chdir $HOME");

	int ok = 1;
	int i;
	for(i=0; i<argc; ++i)
		argv[i] = abs_path(argv[i]);
	for(i=0; i<argc; ++i) {
		char *this_projects_file = argv[i];
		if (verbose)
			fprintf(stderr, "reading projects list from %s:\n", this_projects_file);

		FILE *fp = fopen(this_projects_file, "r");
		if (!fp)
			return NULL;

		while(fgets(buf, sizeof(buf), fp)) {
			char *path;
			struct stat fstats;
			chomp(buf);
		
			if(strlen(buf) == 0 || buf[0] == '#')
				continue;
			if (verbose)
				fprintf(stderr, "\t%s\n", buf);

			path = strdup(buf);
			if (stat(path, &fstats) != 0) {
				fprintf(stderr, "project path does not exist: %s\n", path);
				ok = 0;
			} else if (!S_ISDIR(fstats.st_mode)) {
				fprintf(stderr, "project path is not a directory: %s\n", path);
				ok = 0;
			}
			projects = list_new(projects, path);
			++ *count;
		}

		fclose(fp);
	}

	if (!ok)
		exit(1);

	projects = reverse_list(projects);
	
	list_t *p;
	for(p = projects; p; p=p->next) {
		char *path = p->node;
		p->node = project_new(path);
	}

	return projects;
}

char *	arcs_basename(char *url)
{
	char *bn1, *bn2;
	bn1 = strrchr(url, '/');
	bn2 = strrchr(url, '\\');
	if ((uint)(bn1 - bn2) > 0)
		return bn1+1;
	if (bn2)
		return bn2+1;
	return url;
}

void	add_peer(char *peer)
{
	char buf[PATH_MAX];
	mkdir(arcs_dir, 0777);
	FILE *fh  = fopen(peers_file, "r");
	int already = 0;
	if (fh != NULL) {
		while(fgets(buf, sizeof(buf), fh)) {
			chomp(buf);
		
			if(strlen(buf) == 0 || buf[0] == '#')
				continue;
			if (!strcmp(buf, peer)) {
				already = 1;
				break;
			}
		}
		fclose(fh);
	}
	if (already) {
		fprintf(stderr, "warning: peer %s already exists in %s\n", peer, peers_file);
	} else {
		FILE *fh  = fopen(peers_file, "a");
		if (fh == NULL)
			arcs_error("can't write to .arcs/peers");
		fprintf(fh, "%s\n", peer);
		fclose(fh);
	}
}

void	add_project(char *project_dir)
{
	char *_project_dir = path_abs(project_dir);
	char *home = getenv("HOME");
	char buf[PATH_MAX];
	int already = 0;

	_project_dir = path_rel_to_if_under(_project_dir, home);

	FILE *fp = fopen(projects_file, "r");
	if (!fp)
		fp = fopen(projects_file, "r+");
	if (!fp)
		arcs_error("cannot open %s", projects_file);

	while(fgets(buf, sizeof(buf), fp)) {
		chomp(buf);
	
		if(strlen(buf) == 0 || buf[0] == '#')
			continue;
		if (!strcmp(buf, _project_dir)) {
			already = 1;
			break;
		}
	}

	fclose(fp);

	if (!already) {
		FILE *fp = fopen(projects_file, "a");
		if (!fp)
			arcs_error("cannot open %s", projects_file);
		fprintf(fp, "%s\n", _project_dir);
		fclose(fp);
	}

	Free(_project_dir);
}

void	arcs_get(char *get_from, char *get_to)
{
	if (!get_to) {
		char *dir = strchr(get_from, ':');
		if (!dir)
			arcs_error("invalid get from url?");
		++dir;
		get_to = arcs_basename(dir);
	}

	if (do_rrec)
		arcs_rrec(get_from);
	command(ARCS_GET, get_from, get_to);

	if (chdir(get_to))
		arcs_error("could not get?");

	char *project_dir = Getcwd();

	add_peer(get_from);
	add_project(project_dir);

	Free(project_dir);
}

void	arcs_put(char *put_from, char *put_to)
{
	char _put_to[PATH_MAX];

	if (!strchr(put_to, ':')) {
		char *dir = arcs_basename(put_from);
		Strcat(Strcat(strcpy(_put_to, put_to), ":"), dir);
		put_to = _put_to;
	}

	mkdir(put_from, 0777);
	if (chdir(put_from))
		arcs_error("put: can't chdir to local directory");

	char *project_dir = Getcwd();

	arcs_rrec(put_to);
	pull(put_to);
	push(put_to, put_from);

	add_peer(put_to);
	add_project(project_dir);

	Free(project_dir);
}

size_t Fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t count = fread(ptr, size, nmemb, stream);
	if(count < nmemb && ferror(stream))
		arcs_error("failed: fread");
	return count;
}

void Fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t count = fwrite(ptr, size, nmemb, stream);
	if(count != nmemb)
		arcs_error("failed: fwrite");
}

void Fclose(FILE *fp)
{
	if(fclose(fp) == EOF)
		arcs_error("failed: fclose");
}

void fcp(FILE *in, FILE *out)
{
	char buf[4096];
	size_t len;
	while((len = Fread(buf, 1, sizeof(buf), in)) != 0)
		Fwrite(buf, 1, len, out);
}

void chmod_add(const char *path, mode_t add_mode)
{
	struct stat s;
	if (stat(path, &s) || chmod(path, s.st_mode | add_mode))
		arcs_error("chmod_add %s failed", path);
}

#ifndef __MINGW32_MAJOR_VERSION
void cx(const char *path)
{
	chmod_add(path, S_IXUSR | S_IXGRP | S_IXOTH);
}
#endif

void	setup_make(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		char *mf1 = ".Makefile.1";
		FILE *out = fopen(mf1, "w");
		if (!out)
			arcs_error("cannot create file %s", mf1);
		FILE *in = fopen("Makefile", "r");
		if (!in) {
			if (quiet_level < 3)
				fprintf(stderr, "Warning: Creating null Makefile");
			fputs("build:\n\t:\n\n", out);
		} else {
			char s[3];
			size_t c = Fread(s, 1, 2, in);
			s[c] = '\0';
			if (c == 2 && strcmp(s, "#!"))
				fprintf(out, "#!/usr/bin/make -f\n\n%s", s);
			else
				fputs(s, out);
			fcp(in, out);
			Fclose(in);
		}
		Fclose(out);
		rename("Makefile", ".Makefile.old");
		if (rename(mf1, "Makefile"))
			arcs_error("cannot move %s to Makefile", mf1);

		unlink(".arcs/make");
#ifndef __MINGW32_MAJOR_VERSION
		if (symlink("../Makefile", ".arcs/make"))
			arcs_error("symlink failed");

		cx("Makefile");
#else
		fclose(fopen(".arcs/make", "w"));
#endif
		// TODO a small script to run make for mingw?
	}
}

void	edit_peers(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		if (!no_action)
			command("editor $1", peers_file);
	}
}

void	edit_projects(void)
{
	command("editor $1", projects_file);
}


void	set_task(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		if (!no_action) {
			FILE *fp = fopen(task_file, "w");
			if(!fp)
				arcs_error("set_task: can't open / create task file");
			fprintf(fp, "%s", msg);
			fclose(fp);
		}
	}
}

#ifdef __MINGW32_MAJOR_VERSION
int truncate(const char *path, off_t length)
{
	int ret;
	int fd = open(path, O_RDWR);
	if (fd == -1)
		return -1;
	ret = ftruncate(fd, length);
	if (ret) {
		close(fd);
		return -1;
	}
	ret = close(fd);
	if (ret == -1)
		return -1;
	return 0;
		
}
#endif

void	detach_peers(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		if (!no_action)
			if (truncate(peers_file, 0))
				arcs_error("failed: truncate");
	}
}

void	forget_history(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		command("rm -rf .git ; rm -rf .arcs/tree ; rm -f $1", db_file);
		list_t *peers = project->peers;
		list_t *ptr2;
		
		if (do_push) {
			for (ptr2 = peers; ptr2; ptr2=ptr2->next) {
				char *peer = ptr2->node;
				command("sshc $1 arcs --forget -N", peer);
			}
		}
	}
}

void	remove_arcs(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		command("rm -rf $1 $2", arcs_dir, ARCS_DIR);
	}
	// TODO remove from projects file?
}

void	commit_all(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	int idx;
	if (!ARCS_FIX)
		arcs_error("commit_all: not implemented for this backend %s", ARCS_BACKEND);
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		command(ARCS_FIX);
	}
}

void	conflicts(list_t *projects, int proj_count)
{
	list_t *ptr = projects;
	if (proj_count != 1)
		arcs_error("conflicts: cannot work with multiple projects");
	int idx;
	for (idx = 0; idx < proj_count; idx++) {
		project_t *project = ptr->node;
		ptr = ptr->next;

		project_chdir(project);

		command("conflicts");
	}
}

void	peace(char **argv, int argc)
{
	char which[2];
	char *argv1[argc+3];
	int i;
	snprintf(which, 2, "%0d", peace_which);
	argv1[0] = "pacify";
	argv1[1] = which;
	for(i=0; i<argc; ++i)
		argv1[i+2] = argv[i];
	argv1[i+2] = NULL;
	lcommand(argv1);
}

int arcs_rec(project_t *project)
{
	node_t *testdb = NULL;
	char *root = ".";

	if (verbose)
		fprintf(stderr, "rec\n");

	testdb = node_alloc();

	generate_database(root, db_file_new, testdb, 1);
	project->changed = compare_databases(project->database, testdb);

	if (project->changed && do_rec) {
		char *task = NULL;
		struct stat fstats;

		if (msg != NULL) 
			task = strdup(msg);
		else if (stat(task_file, &fstats) == 0) {
			FILE *fp = fopen(task_file, "r");
			if (!fp)
				arcs_error("Can't open task file");
			
			// TODO put me in a function! use a sane way?
			task = Malloc(fstats.st_size + 1);
			char *ptr = task;
			char *end = task + fstats.st_size;
			while (ptr != end) {
				int bytes_read = fread(ptr, 1, end-ptr, fp);
				if (bytes_read <= 0)
					break;
				ptr += bytes_read;
			}
			*ptr = '\0';
			fclose(fp);
		} else {
			task = strdup(".");
		}

		if (record(task)) {
			// if the record fails, maybe because an empty commit,
			// no need to push
			project->changed = 0;
		}

		Free(task);

		if (safe_exit) return 0;
	}

	if (command_failed || no_action) {
		command_failed = 0;
	} else {
		unlink(db_file); // TODO maybe, rename to db_file_old  ?
		if (rename(db_file_new, db_file))
			arcs_error("can't replace database");
	}
	node_free(project->database);
	project->database = testdb;

	return project->changed;
}

void one_step(project_t *project, int push_pull_this_scan)
{
	location_t *here = project->here;
	list_t *peers = project->peers;
	int changed1 = 0;
	int changed2 = 0;

	struct stat fstats;
	int need_push = stat(need_push_file, &fstats) == 0;
	if (!no_action) {
		FILE *s = fopen(need_push_file, "w");
		if (!s)
			arcs_error("failed: fopen %s", need_push_file);
		fclose(s);
	}

	now = time(NULL);
	arcs_here_i_am(here);

//	dont_kill_me();
	if (do_rec)
		changed1 = arcs_rec(project);
//	ok_I_can_die_now_jk();

	if (safe_exit) return;


	list_t *ptr;

	if (push_pull_this_scan) {
		int need_rec = 0;
		for (ptr = peers; ptr; ptr=ptr->next) {
			char *peer = ptr->node;
			if ((do_pull || do_push) && do_rrec)
				arcs_rrec(peer);
			if (safe_exit) return;
			if (do_pull)
				if (!pull(peer))
					need_rec = 1;
			if (safe_exit) break;
		}
		if (need_rec) {
			int tmp = rec_after_pull;
			rec_after_pull = 1;
			if (VERBOSE)
				fprintf(stderr, "sleep 1\n");
			if (!no_action)
		 		sleep(1);
			now = time(NULL);
			if (do_rec)
				changed2 = arcs_rec(project);
			rec_after_pull = tmp;
		}
		if (safe_exit) return;

		if (changed1 || changed2 || need_push || do_push_always || !do_rec) {
			if (do_push) {
				for (ptr = peers; ptr; ptr=ptr->next) {
					char *peer = ptr->node;
					push(peer, here->address);
					if (safe_exit) return;
				}
			}
		}
		if (do_push && !no_action)
			remove(need_push_file);

		if (do_make) {
			struct stat fstats;
			if (stat(make_file, &fstats) == 0)
				make();
		}
		if (safe_exit) return;
	}
}

int	get_options(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		char *opt = argv[i];
		if (!strcmp(opt, "-h") || !strcmp(opt, "--help") || !strcmp(opt, "-help"))
			help(argv[0]);

		else if (!strcmp(opt, "-l")) 
			loop = 1;
		else if (!strcmp(opt, "-v"))
			--quiet_level;
		else if (!strcmp(opt, "-q"))
			++quiet_level;
		else if (!strcmp(opt, "-i")) 
			init_new = 1;
		else if (!strcmp(opt, "-n"))
			no_action = 1;
		else if (!strcmp(opt, "-a"))
			read_proj = 1;
		else if (!strcmp(opt, "-f")) {
#ifdef __MINGW32_MAJOR_VERSION
			arcs_error("sorry, -f (parallel mode) does not work on windows");
#endif
			parallel = 1;
			if (argv[i+1] && argv[i+1][0] >= '0' && argv[i+1][0] <= '9') {
				i++;
				parallel_max = atoi(argv[i]);
			}
		}
		else if (!strcmp(opt, "-E"))
			elf_repellant = 0;
		else if (!strcmp(opt, "-T"))
			use_link_tree = 0;
		else if (!strcmp(opt, "-c"))
			arcs_shared = 1;
		else if (!strcmp(opt, "-C"))
			arcs_shared = 0;
		else if ((!strcmp(opt, "-m")) && (i < (argc - 1))) {
			i++;
			msg = opt;
		}
		else if ((!strcmp(opt, "-s")) && (i < (argc - 1))) {
			i++;
			scan_delay = atoi(argv[i]);
		}
		else if ((!strcmp(opt, "-S")) && (i < (argc - 1))) {
			i++;
			pull_interval = atoi(argv[i]);
		}
		else if ((!strcmp(opt, "-z")) && (i < (argc - 1))) {
			i++;
			parallel_sleep = atof(argv[i]);
		}
		else if (!strcmp(opt, "-O"))
			do_push = 0;
		else if (!strcmp(opt, "-I"))
			do_pull = 0;
		else if (!strcmp(opt, "--push"))
			do_push_always = 1;
		else if (!strcmp(opt, "-N")) {
			do_push = do_pull = 0;
		}
		else if (!strcmp(opt, "--rap")) {
			rec_after_pull = 1;
		}
//		else if (!strcmp(opt, "+o")) {
//			push_all = 0;
//			while (argv[i+1] && argv[i+1][0] != '-' && argv[i+1][0] != '+') {
//				++i;
//				push_list = list_new(push_list, strdup(opt));
//			}
//		}
//		else if (!strcmp(opt, "+i")) {
//			pull_all = 0;
//			while (argv[i+1] && argv[i+1][0] != '-' && argv[i+1][0] != '+') {
//				++i;
//				pull_list = list_new(pull_list, strdup(opt));
//			}
//		}


/*
		else if (!strcmp(opt, "-o")) {
			push_all = 1;
			while (argv[i+1] && argv[i+1][0] != '-' && argv[i+1][0] != '+') {
				++i;
				push_list = list_new(push_list, strdup(opt));
			}
			// FIXME doesn't do anything yet
		}
		else if (!strcmp(opt, "-i")) {
			pull_all = 1;
			while (argv[i+1] && argv[i+1][0] != '-' && argv[i+1][0] != '+') {
				++i;
				pull_list = list_new(pull_list, strdup(opt));
			}
			// FIXME doesn't do anything yet
		}
*/

		else if (!strcmp(opt, "--get")) {
			if (!argv[i+1]) {
				arcs_error("usage: arcs --get from [to]");
			} else {
				get_from = argv[i+1];
				if (argv[i+2] && argv[i+2][0] != '-') {
					get_to = argv[i+2];
				}
			}
			do_push = 0;
			do_pull = 0;
		}
		else if (!strcmp(opt, "--put")) {
			if (!argv[i+1] || !argv[i+2]) {
				arcs_error("usage: arcs --put from to");
			} else {
				put_from = argv[i+1];
				put_to = argv[i+2];
			}
		}
		else if(!strcmp(opt, "-L")) {
			do_rec = 0;
		}
		else if(!strcmp(opt, "-R")) {
			do_rrec = 0;
			do_push = 0;
		}
		else if (!strcmp(opt, "--log")) {
			// FIXME not here!
			command(ARCS_LOG);
			exit(EXIT_SUCCESS);
		}
		else if (!strcmp(opt, "--hist")) {
			// FIXME not here!
			command(ARCS_HIST);
			exit(EXIT_SUCCESS);
		}
		else if (!strcmp(opt, "--make")) {
			do_setup_make = 1;
		}
		else if (!strcmp(opt, "--peers")) {
			do_edit_peers = 1;
		}
		else if (!strcmp(opt, "--projects")) {
			edit_projects();
			exit(EXIT_SUCCESS);
		}
		else if ((!strcmp(opt, "-t")) && (i < (argc - 1))) {
			i++;
			msg = opt;
			do_set_task = 1;
		}
		else if (!strcmp(opt, "--detach")) {
			do_detach_peers = 1;
		}
		else if (!strcmp(opt, "--forget")) {
			do_forget_history = 1;
		}
		else if (!strcmp(opt, "--remove")) {
			do_remove_arcs = 1;
		}
		else if (!strcmp(opt, "--commit-all")) {
			do_commit_all = 1;
		}
		else if (!strcmp(opt, "--conflicts")) {
			do_conflicts = 1;
		}
		else if (!strcmp(opt, "--peace")) {
			do_peace = 1;
			i++;
			if (!argv[i])
				arcs_error("usage: arcs --peace [1|2] file...");
			peace_which = atoi(argv[i]);
		}
		else if (!strcmp(opt, "-M")) {
			do_make = 0;
		}
		else if(!strcmp(opt, "-d")) {
			do_diff = 1;
		}
		else if(!strcmp(opt, "-p")) 
			do_patch = 1;
		else if(!strcmp(opt, "-u")) 
			do_unpatch = 1;
		else if(!strcmp(opt, "--version") || !strcmp(opt, "-V"))
			version_info();
		else if (!strcmp(opt, "--")) {
			++i;
			break;
		} else if (opt[0] == '-')
			arcs_error("unknown option %s", opt);
		else
			break;
	}

	if (put_to && !do_rrec)
		arcs_error("-R conflicts with --put");

	verbose = (quiet_level <= 0);
	VERBOSE = (quiet_level < 0);

	return i;
}

#ifndef __MINGW32_MAJOR_VERSION
void rtime_to_timespec(double rtime, struct timespec *ts)
{
	ts->tv_sec = (long)rtime;
	ts->tv_nsec = (long)((rtime - ts->tv_sec) * 1e9);
}

void rsleep(double time)
{
	if(time <= 0)
		return;
	struct timespec delay;
	rtime_to_timespec(time, &delay);
	nanosleep(&delay, &delay);
}
#endif

int main(int argc, char **argv)
{
	int i;
	char *backend;
	list_t *projects = NULL;

	Malloc_debug_open();

	char *ssh = getenv("ARCS_SSH");
	if (!ssh) { ssh = getenv("SSH"); }
	if (ssh && !getenv("GIT_SSH"))
		setenv("GIT_SSH", ssh, 1);

	char *home = getenv("HOME");
	Strcat(Strcat(strcpy(projects_file, home), SLASH), _projects_file);

	// at the moment, only one backend at a time!

	if (!(backend = getenv("ARCS")))
		/* default backend */
		arcs_backend = &git;
	else if (!strcasecmp(backend, "GIT"))
		arcs_backend = &git;
	else if (!strcasecmp(backend, "SVN"))
		arcs_backend = &svn;
	else if(!strcasecmp(backend, "CVS"))
		arcs_backend = &cvs;
	else if(!strcasecmp(backend, "DARCS"))
		arcs_backend = &darcs;
	else
		arcs_error("Unknown backend");

	no_dirs = arcs_backend == &git;

	i = get_options(argc, argv);

	/* commands --get, --put */

	if(get_from) {
		arcs_get(get_from, get_to);
		exit(EXIT_SUCCESS);
	}
	if(put_to) {
		arcs_put(put_from, put_to);
		exit(EXIT_SUCCESS);
	}
	if(do_peace) {
		peace(argv+i, argc-i);
		exit(EXIT_SUCCESS);
	}

	/* And so, the bogosity begins...*/

	int proj_count = 0;
	int idx;

	if (read_proj) {
		projects = read_projects(projects_file, &proj_count, argc-i, argv+i);
	} else if (argv[i] != NULL) {
		int j;
		for(j=i; argv[j]; ++j)
			argv[j] = abs_path(argv[j]);
		for(j=i; argv[j]; ++j) {
			project_t *project = project_new(argv[j]);
			projects = list_new(projects, project);
			++proj_count;
		}
		projects = reverse_list(projects);
	} else {
	 	proj_count = 1;
		project_t *project = project_new(Getcwd());
		projects = list_new(NULL, project);
	}

	if(do_setup_make) {
		setup_make(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if(do_edit_peers) {
		edit_peers(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if(do_set_task) {
		set_task(projects, proj_count);
	}

	if(do_detach_peers) {
		detach_peers(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if(do_forget_history) {
		forget_history(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if(do_remove_arcs) {
		remove_arcs(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if(do_commit_all) {
		commit_all(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if(do_conflicts) {
		conflicts(projects, proj_count);
		exit(EXIT_SUCCESS);
	}

	if (do_diff || do_patch || do_unpatch) {
		int status;
		if (proj_count != 1 && (do_patch || do_unpatch))
			arcs_error("can't patch / unpatch multiple projects at once");
		list_t *ptr = projects;
		for (idx = 0; idx < proj_count; idx++) {
			project_t *project = ptr->node;
			ptr = ptr->next;
			location_t *here = project->here;

			project_chdir(project);

			now = time(NULL);
			arcs_here_i_am(here);

			if (do_patch == 1) {
				status = command("patch -p1 -N");
				if (status)
					arcs_error("patch failed");
			}

			if (do_unpatch == 1) {
				status = command("patch -p1 -R");
				if (status)
					arcs_error("unpatch failed");
			}

			if (do_diff == 1) {
				int tmp = no_action;
				int changed1;
				no_action = 1;
				changed1 = arcs_rec(project);
				no_action = tmp;
				fflush(stdout);
				if (changed1) {
					no_action = 0;
					status = command(ARCS_DIFF);
					no_action = tmp;
					if (status)
						arcs_error("diff failed");
						// some diffs may return 0 or 1 for same / different..?
					no_action = tmp;
				}
			}
		}

		exit(EXIT_SUCCESS);
	}

	long scan_count = 0;

	if (!loop)
		pull_interval = 10;

	for (;;) {
		if (loop && verbose)
			fprintf(stderr, "scan %ld\n", scan_count);

		list_t *ptr = projects;

		int push_pull_this_scan = scan_count % pull_interval == 0;

#ifndef __MINGW32_MAJOR_VERSION
		int parallel_this_scan = parallel && push_pull_this_scan;
		int child_count = 0;
#endif

		for (idx = 0; idx < proj_count; idx++) {
			project_t *project = ptr->node;
			ptr = ptr->next;

			project_chdir(project);

#ifndef __MINGW32_MAJOR_VERSION
			if (!parallel_this_scan || fork() == 0) {
#endif
				one_step(project, push_pull_this_scan);

#ifndef __MINGW32_MAJOR_VERSION
				if (parallel_this_scan)
					exit(safe_exit ? 2 : 0);
#endif
				if (safe_exit)
					break;
#ifndef __MINGW32_MAJOR_VERSION
			}
			++child_count;
			if (parallel_this_scan)
				rsleep(parallel_sleep);
			if (parallel_this_scan && child_count == parallel_max) {
				int status;
				if (wait(&status) >= 0) {
					if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
						safe_exit = 1;
					--child_count;
				}
			}
#endif
		}
		if (safe_exit)
			break;
#ifndef __MINGW32_MAJOR_VERSION
		if (parallel_this_scan) {
			int status;
			while (wait(&status) >= 0) {
				if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
					safe_exit = 1;
				--child_count;
			}
			if (child_count != 0 && quiet_level < 3)
				fprintf(stderr, "Warning: child accounting does not balance! %d", child_count);
		}
#endif

		scan_count++;

		// TODO check for certain no change situation, and in that case don't run the scan twice

		if (!loop) break;

		if (loop) {
			if (VERBOSE)
				fprintf(stderr, "sleep %d\n", scan_delay);
			sleep(scan_delay);
			if (verbose)
				fprintf(stderr, "\n");
		}
	}

	list_free_each(projects, (free_t)&project_free);

	Malloc_debug_close();

	return EXIT_SUCCESS;
}
