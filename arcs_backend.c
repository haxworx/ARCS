/* written by Alastair Poole and Sam Watkins, 2006 - 2009

   this version is released to the public domain.
*/

/* Only the git backend has been used / tested much. */

#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arcs_backend.h"

char *ARCS_RREC	= 	"sshc $1 arcs $q $v -M -N -i";

backend_t git = {
	.NAME	=	"git",
	.DIR	=	".git",
	.REM	=	"git update-index --remove -- $1",
	.ADD	=	"git update-index --add -- $1",
	.MOD	=	"git update-index -- $1",
	.REC	=	"git commit $q -m $1 $Q $R",
//	.PUSH	=	"git push --all $1 $Q ; sshc $1 sh -c 'mkdir -p .arcs/tmp && mv * .arcs/tmp && git checkout $q -f && cp -v --no-dereference --preserve=mode * .arcs/tmp && rm -rf * && mv .arcs/tmp/* . && sleep 1 && arcs $q $v $M --rap -N'",  // this is 'a bit' bogus / maybe dangerous now! blame git!
	.PUSH	=	"git push --all $1 $Q ; sshc $1 sh -c 'git checkout $q -f ; sleep 1 ; arcs $q $v $M --rap -N'",  // this is 'a bit' bogus / maybe dangerous now! blame git!
//	.PUSH	=	"git push --all $1 $Q ; sshc $1 sh -c 'git checkout $q -f ; sleep 1 ; arcs -q $q $v $M --rap -N'",  // a bit bogus
	  // this `sleep 1` is so mtime < now when we run arcs
	/* use a hook? locking? */
//	.PUSH = "sshc $1 git pull $2"
//	.PUSH = "sshc $1 arcs $3 -N $2"
//	.PUSH = "git sh $1"
	.PULL	=	"git pull $q $1 master $Q | perl -ne \"print; exit 123 if /Already up-to-date/\" $Q",
	.MOVE	=	"git update-index --add --remove -- $1 $2",
	.INIT	=	"git init-db $q --shared ; for A in '\\.*' '_*' '*~*' 'CVS' 'RCS' '*.[oa]' '*.exe' '*.so' '*.dll'; do echo \"$A\"; done >> .git/info/exclude; touch ... ; git update-index --add ... ; git commit $q -m ... $Q ; rm ... ; git update-index --remove ... ; git commit $q -m ... $Q",
	.GET	=	"git clone $q $1 $2 ; cd $2 && git branch -d origin",
	.DIFF	=	"git diff | cat",
	.LOG	=	"git log | cat",
	.HIST	=	"git log -p | cat",
	.FIX    =   "git_commit_all",
};

/* TODO add $q to other backends */

backend_t svn = {
	.NAME	=	"svn",
	.DIR	=	".svn",
	.REM	=	"svn remove -- $1",
	.ADD	=	"svn add -- $1",
	.MOD	=	"echo",
	.REC	=	"svn commit --force-log -m $1",
	.PUSH	=	"echo",
	.PULL	=	"svn update",
	.MOVE	=	"[ -e $1 ] && mv -- $1 .arcs_tmp ; mv -- $2 $1 ; svn mv -- $1 $2 ; [ -e .arcs_tmp ] && mv -- .arcs_tmp $1",
	.INIT	=	NULL,
	.GET	=	"svn checkout $1 $2",
	.DIFF	=	"svn diff -u",
	.LOG	=	"svn log",
	.HIST	=	"echo no svn hist yet",
	.FIX    =   NULL,
  // ?
};

backend_t cvs = {
	.NAME	=	"cvs",
	.DIR	=	"CVS",
	.REM	=	"cvs remove -- $1",
	.ADD	=	"cvs add -- $1",
	.MOD	=	"echo",
	.REC	=	"cvs commit -m $1",
	.PUSH	=	"echo",
	.PULL	=	"cvs update",
	.MOVE	=	"cvs remove -- $1 ; cvs add -- $2",
	.INIT	=	NULL,
	.GET	=	"cvs checkout $1 $2",
	.DIFF	=	"cvs diff -u",
	.LOG	=	"cvs log",
	.HIST	=	"echo no cvs hist yet",
	.FIX    =   NULL,
  // ?
};

/*

NOTE: This is a bit too dodgy yet, even for ARCS!

backend_t rsync = {
	.NAME	=	"rsync",
	.DIR	=	".rsync",
	.REM	=	"echo",
	.ADD	=	"echo",
	.MOD	=	"echo",
	.REC	=	"echo",
	.PUSH	=	"rsync -uavz --delete --exclude '.*' --exclude '_*' --exclude '*~*' ./ $1/",
	.PULL	=	"rsync -uavz --delete --exclude '.*' --exclude '_*' --exclude '*~*' $1/ ./",
	.MOVE	=	"sshc $PEER mv $1 $2",
	.INIT	=	"echo",
	.GET	=	"rsync -uazv --exclude '.*' --exclude '_*' --exclude '*~*' ./ $1/ $2/",
	.LOG	=	"echo no rsync log yet",
	.HIST	=	"echo no rsync hist yet",
	.FIX    =   NULL,
};

*/

backend_t darcs = {
	.NAME	=	"darcs",
	.DIR	=	"_darcs",
	.REM	=	"darcs remove -- $1",
	.ADD	=	"darcs add -- $1",
	.MOD	=	"echo",
	.REC	=	"darcs record -m$1 -a",
	.PUSH	=	"darcs push -a $1",
	.PULL	=	"darcs pull -a $1",
	.MOVE	=	"[ -e $1 ] && mv -- $1 .arcs_tmp ; mv -- $2 $1 ; darcs mv -- $1 $2 ; [ -e .arcs_tmp ] && mv -- .arcs_tmp $1",
//	.MOVE	=	"darcs mv -- $1 $2",    /* this may be dodgy, should do like SVN_MOVE unfortunately */
	.INIT	=	"darcs init",
  /* NOTE: first darcs record asks an email address,
   * push or pull won't work until one done with a repository name first */
	.GET	=	"darcs get $1 $2",
	.DIFF	=	"darcs diff -u",
	.LOG	=	"darcs changes",
	.HIST	=	"echo no cvs hist yet",
	.FIX    =   NULL,
  // ?
};

backend_t *arcs_backend;
