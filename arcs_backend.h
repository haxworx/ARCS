/* written by Alastair Poole and Sam Watkins, 2006 - 2009

   this version is released to the public domain.
*/

#ifndef __ARCS_BACKEND_H__
#define __ARCS_BACKEND_H__

typedef struct backend_t backend_t;
struct backend_t {
	const char *NAME;
	const char *REM;
	const char *ADD;
	const char *MOD;
	const char *DIR;
	const char *REC;
	const char *PUSH;
	const char *PULL;
	const char *MOVE;
	const char *INIT;
	const char *GET;
	const char *DIFF;
	const char *LOG;
	const char *HIST;
	const char *FIX;
};

extern backend_t git;
extern backend_t svn;
extern backend_t cvs;
extern backend_t rsync;
extern backend_t darcs;

extern backend_t *arcs_backend;

extern char *ARCS_RREC;

// ? FIXME:
#define ARCS_BACKEND (arcs_backend->NAME)
#define ARCS_REM (arcs_backend->REM)
#define ARCS_ADD (arcs_backend->ADD)
#define ARCS_MOD (arcs_backend->MOD)
#define ARCS_DIR (arcs_backend->DIR)
#define ARCS_REC (arcs_backend->REC)
#define ARCS_PUSH (arcs_backend->PUSH)
#define ARCS_PULL (arcs_backend->PULL)
#define ARCS_MOVE (arcs_backend->MOVE)
#define ARCS_INIT (arcs_backend->INIT)
#define ARCS_GET (arcs_backend->GET)
#define ARCS_DIFF (arcs_backend->DIFF)
#define ARCS_LOG (arcs_backend->LOG)
#define ARCS_HIST (arcs_backend->HIST)
#define ARCS_FIX (arcs_backend->FIX)

#endif
