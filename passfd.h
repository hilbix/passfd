/* PassFD: Pass FD to other programs using Unix Domain Socket
 *
 * This Works is placed under the terms of the Copyright Less License,
 * see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
 */

#define	_GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <poll.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#ifndef	PASSFD_VERSION
#define	PASSFD_VERSION	"-undef"
#endif

struct PFD_passfd;
#define	MERGESORT_USER_TYPE	struct PFD_passfd *
#include "mergesort.h"


/***********************************************************************
 * Interface
 **********************************************************************/

#define	P(X,Y,...)	static Y PFD_##X(struct PFD_passfd *_, ##__VA_ARGS__)

struct PFD_passfd
  {
    int			code;	/* exit code	*/
    const char		*arg0;
    int			sock;

    unsigned		done:1;

    unsigned		listen:1, accept:1, connect:1, onsuccess:1, onerror:1, dofork:1, keepfds:1, verbose:1;
    unsigned char	mode;

    int			retry;
    int			timeout;

    const char		*sockname;
    int			*fds, *waits, *uses, *recfds;
    char * const	*cmd;
    int			ret;

    int			afake;
    struct addrinfo	*as, *a;
  };


/***********************************************************************
 * Error processing
 **********************************************************************/

#define	PFD_NOTYET(X,...)	PFD_OOPS(_, "not yet implemented in %s:%d:%s: " X, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define	PFD_INTERNAL(X,...)	PFD_OOPS(_, "internal error in %s:%d:%s: " X, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define	PFD_FATAL(X,...)	do { if (X) PFD_OOPS(_, "fatal error in %s:%d:%s: " #X, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

P(exec, void, int);

/* Terminate impl.
 */
P(vOOPS, void, int e, const char *s, va_list list)
{
  fprintf(stderr, "OOPS: ");
  vfprintf(stderr, s, list);
  if (e)
    fprintf(stderr, ": %s", strerror(e));
  fprintf(stderr, "\n");
  if (_->onerror)
    PFD_exec(_, 0);
  exit(23); abort(); for (;;);
}

/* Terminate
 */
P(OOPS, void, const char *s, ...)
{
  va_list	list;

  va_start(list, s);
  PFD_vOOPS(_, errno, s, list);
  va_end(list);	/* never reached	*/
}

/* PFD_V/E/R impl
 */
P(vV, void, int e, const char *prefix, const char *s, va_list list)
{
  time_t	t;
  struct tm	tm;

  if (!_->verbose)
    return;

  time(&t);
  gmtime_r(&t, &tm);	/* always use UTC!	*/
  fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d [%d] ", 1900+tm.tm_year, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
  if (prefix)
    fprintf(stderr, "%s ", prefix);
  vfprintf(stderr, s, list);
  if (e)
    fprintf(stderr, ": %s", strerror(errno));
  fprintf(stderr, "\n");
  fflush(stderr);
}

/* PFD_V(_, ..) report verbose
 */
P(V, void, const char *s, ...)
{
  int		e = errno;
  va_list	list;

  va_start(list, s);
  PFD_vV(_, 0, NULL, s, list);
  va_end(list);

  errno	= e;
}

/* PFD_E(_, ..) report error if verbose
 */
P(E, void, const char *s, ...)
{
  int		e = errno;
  va_list	list;

  va_start(list, s);
  PFD_vV(_, e, "fail", s, list);
  va_end(list);

  errno	= e;
}

/* PFD_R(_, ret, ..) is PFD_V(_, ..) or PFD_E(_, ..) based on return code
 */
P(R, int, int ret, const char *s, ...)
{
  int		e = errno;
  va_list	list;

  va_start(list, s);
  PFD_vV(_, e, ret ? "fail" : NULL, s, list);
  va_end(list);

  errno	= e;
  return ret;
}


/***********************************************************************
 * Memory Management
 **********************************************************************/

P(realloc, void *, void *ptr, size_t len)
{
  ptr	= ptr ? realloc(ptr, len) : malloc(len);
  if (!ptr)
    PFD_OOPS(_, "out of memory");
  return ptr;
}

P(alloc, void *, size_t len)
{
  return PFD_realloc(_, NULL, len);
}

P(free, void, void *p)
{
  if (p)
    free(p);
}

P(dup, char *, const char *s)
{
  PFD_FATAL(!s, "string must not be NULL");
  char	*r = strdup(s);
  if (!r)
    PFD_OOPS(_, "out of memory");
  return r;
}

P(append, void, char *buf, size_t max, const char *s, ...)
{
  size_t	len;

  len	= strlen(buf);
  if (len<max)
    {
      va_list	list;
      int	put;

      va_start(list, s);
      put	= vsnprintf(buf+len, max-len, s, list);
      va_end(list);
      if (put < max-len)
        return;
    }
  if (max>=4)
    {
      buf[max-4] = '.';
      buf[max-3] = '.';
      buf[max-2] = '.';
    }
  if (max)
    buf[max-1] = 0;
}

/***********************************************************************
 * Argument splitting
 **********************************************************************/

/* Initialize structure */
P(init, void, const char *arg0)
{
  memset(_, 0, sizeof *_);
  _->arg0	= arg0;
  _->sock	= -1;
}

/* Deallocate structure and return return code
 * XXX TODO XXX implement this correctly
 * Note that this (probably) is not reached if PFD_exec() is done.
 */
P(exit, int)
{
  /* XXX TODO XXX free everything	*/
  PFD_V(_, "return code %d", _->code);
  return _->code;
}

/* Remove name (safely)
 *
 * If fd given, it limit unlink() to the file presented in fd.
 *
 * Note that checking and removing allows for some race condition
 * as there apparently is no way to do this atomically.  Sadly.
 */
P(unlink, void, int fd, const char *name)
{
  struct stat	st1, st2;
  const char	*what;

  /* following two ifs are special to PFD only	*/
  if (*name == '@')	/* ignore Abstract Unix Domain Socket	*/
    return;
  if (isdigit(*name))	/* ignore FDs	*/
    return;

  /* We can come here for regular files, too, which is very sad.
   * Hence we first must check if it is really a Unix Domain Socket.
   * We also allow to remove empty files (like mktemp).
   */
  if (lstat(name, &st1))
    {
      if (errno == ENOENT)
        return;
      PFD_OOPS(_, "cannot stat %s", name);
    }
  what="socket";	/* this is PFD special, we do not operate on files	*/
  if (fd<0)
    {
      switch (st1.st_mode & S_IFMT)
        {
        default:
          PFD_OOPS(_, "incompatible file type: %s", name);
        case S_IFREG:
          if (st1.st_size || st1.st_blocks>1)
            PFD_OOPS(_, "refuse to remove nonempty file: %s", name);
          what="empty file";
        case S_IFSOCK:
          break;
        }
    }
  else if (fstat(fd, &st2)<0)
    PFD_OOPS(_, "cannot stat %d: %s", fd, name);
  else if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino)
    {
      PFD_V(_, "skip unlink: %d does not refer to %s", fd, name);
      return;
    }

  /* XXX TODO XXX prevent removing wrong socket
   * We (somehow) should check if it really is our socket we are removing here!
   *
   * Sadly, Unix has no atomic unlink() which fails if name does not refer to given fd
   */
  PFD_R(_, unlink(name), "unlink %s: %s", what, name);

  /* XXX TODO XXX what to do in error case?
   * Leave that to retry() for now.
   * However we perhaps could bail out more early in case of unresolvable errors.
   *
   * Note that manual intervention IS resolvable.
   */
}


/***********************************************************************
 * Argument splitting
 **********************************************************************/

struct PFD_split
  {
    int		argc;
    const char	**argv;
  };

P(split_add, void, struct PFD_split *p, const char *s)
{
  p->argv		= PFD_realloc(_, p->argv, (++p->argc)*sizeof *p->argv);
  p->argv[++p->argc]	= s ? PFD_dup(_, s) : s;
}

P(split_free, void, struct PFD_split *p)
{
  while (p->argc--)
    PFD_free(_, (void *)p->argv[p->argc]);
  PFD_free(_, p->argv);
  p->argv	= 0;
}

P(split_split, void, struct PFD_split *p, const char *s)
{
  char	*r = PFD_dup(_, s), *x = 0, *tmp;

  for (tmp=strtok_r(r, " ", &x); tmp; tmp=strtok_r(NULL, " ", &x))
    PFD_split_add(_, p, tmp);
  PFD_free(_, r);
}

P(split, struct PFD_split *, const char *s)
{
  struct PFD_split	*p;

  p		= PFD_alloc(_, sizeof *p);
  p->argc	= 0;
  p->argv	= 0;
  if (s)
    PFD_split_split(_, p, s);
  return p;
}


/***********************************************************************
 * Integer list
 * i[0]	number of integers
 * i[1] first integer
 * i[i[0]] last integer
 **********************************************************************/

P(intlist, char *, char *buf, size_t len, int *ints, int n)
{
  int	i;

  buf[0]	= 0;
  for (i=0; i<n; i++)
    PFD_append(_, buf, len, " %d", ints[i]);
  return buf;
}

/* -1 .. MAX_INT	*/
P(int, int, const char *s)
{
  char			*end;
  unsigned long	long u;
  int			i;

  u	= strtoll(s, &end, 10);
  i	= u;
  if (i<-1 || (unsigned long long)i != u || !end || *end)
    PFD_OOPS(_, "number overflow: %s", s);
  return i;
}

P(getint, int, int **i, const char *s)
{
  if (!s)
    return 1;
  if (*s=='$')
    {
      const char	*env, **argv;
      struct PFD_split	*split;

      env	= getenv(s+1);
      if (!env)
        return 0;
      split	= PFD_split(_, env);
      for (argv=split->argv; *argv; argv++)
        if (*argv && PFD_getint(_, i, *argv))
          PFD_OOPS(_, "Parsing env var %s failed", s);
      PFD_split_free(_, split);
      return 0;
    }

  if (!isdigit(*s) && strcmp(s, "-1"))	/* -1 is of special value	*/
    return 1;

  *i		= PFD_realloc(_, *i, (1 + ++(**i)) * sizeof **i);
  i[0][**i]	= PFD_int(_, s);
  return 0;
}

P(getints, char * const *, char * const * argv, int **i)
{
  if (!*i)
    {
      *i	= PFD_alloc(_, sizeof **i);
      **i	= 0;
    }
  for (;; argv++)
    if (PFD_getint(_, i, *argv))
      return argv;
}

P(ints, int, int *list, int **ret)
{
  static int	fd0;
  int		n;

  n	= 0;
  if (list)
    {
      *ret	= list+1;
      n		= list[0];
    }
  if (!n)
    {
      fd0	= 0;
      *ret	= &fd0;
      n		= 1;
    }
  return n;
}

/***********************************************************************
 * File helpers
 **********************************************************************/

P(close, void, int sock, const char *name)
{
  for (;;)
    {
      if (!close(sock))
        {
          PFD_V(_, "close %d: %s", sock, name);
          if (sock == _->sock)
            _->sock	= -1;
          return;
        }
      if (errno != EINTR)
        PFD_OOPS(_, "close %d fail: %s", sock, name);
      PFD_V(_, "close %d interrupted: %s", sock, name);
    }
}


/***********************************************************************
 * Child execution
 **********************************************************************/

/* Map _->recfds according to _->fds into space
 * returning FD which represents previous FD2
 */
P(map, int)
{
  int	fd2	= 2;
  int	i, n0, n1;

  n0	= _->fds[0];
  n1	= _->recfds[0];
  if (n1 < n0)
    PFD_OOPS(_, "too few FDs received, got %d, expected at least %d", n1, n0);
  for (i=0; ++i <= n0; )
    {
      int	fd0 = _->fds[i];
      int	fd1 = _->recfds[i];

      if (fd0 == fd2)
        fd2	= dup(fd2);
      if (fd0 != fd1)
        {
          dup2(fd1, fd0);
          PFD_close(_, fd1, "(mapped fd)");
          _->recfds[i]	= fd0;
        }
    }
  return fd2;
}

P(recfds, void, int *fds)
{
  if (_->recfds)
    PFD_free(_, _->recfds);
  _->recfds	= fds;
}

P(waitpid, void, pid_t pid)
{
  int	brute	= 1000000;
  do
    {
      int		st;
      pid_t		ret;
      const char	*s;

      ret = waitpid((pid_t)-1, &st, 0);
      if (ret == (pid_t)-1)
        {
          if (errno != EINTR)
            PFD_OOPS(_, "waitpid() error");
          continue;
        }
      if (WIFEXITED(st))
        {
          _->ret	= WEXITSTATUS(st);
          s		= "exit";	/* set it here to shutup compiler	*/
        }
      else if (WIFSIGNALED(st))
        {
          _->ret	= WTERMSIG(st);
          PFD_FATAL(!_->ret, "waitpid() termination signal is 0");
          s		= WCOREDUMP(st) ? "coredump" : "signal";
        }
      else if (WIFSTOPPED(st))
        {
          /* should not happen	*/
          PFD_V(_, "child %d stopped by signal %d", (int)ret, WSTOPSIG(st));
          continue;
        }
      else if (WIFCONTINUED(st))
        {
          /* should not happen	*/
          PFD_V(_, "child %d continues", (int)ret);
          continue;
        }
      else
        {
          PFD_V(_, "child %d unknown status", (int)ret);
          continue;
        }
      PFD_V(_, "child %d: %s status %d", (int)ret, s, _->ret);
      if (ret == pid)
        return;
    } while (--brute);
  PFD_OOPS(_, "waitpid(): too many loops");
}

/* dofork>0: fork() and return as child
 * dofork==0: exec (no fork())
 * dofork<0: fork() and return as parent
 */
P(exec, void, int dofork)
{
  if (_->done)
    return;
  _->done	= 1;
  if (!_->cmd)
    return;

  if (dofork)
    {
      /* forking is done before we have received FDs
       * on d: see below
       * on i: just pass everything as is (passed fds are closed if not option 'keep', see PFD_main_i())
       * on o: just pass everything as is
       * on p: just pass everything as is (plus socketpair() created in PFD_fork())
       */
      pid_t	pid;

      pid	= fork();
      if (pid == (pid_t)-1)
        PFD_OOPS(_, "fork() failed");
      if (dofork>0)
        {
          if (!pid)
            {
              /* we return as child, as we terminate later on, but the forked command may stay */
              PFD_V(_, "forked %d: %s", (int)pid, _->cmd[0]);
              return;
            }
        }
      else if (pid)
        {
          PFD_V(_, "running %d: %s", (int)pid, _->cmd[0]);
          PFD_waitpid(_, pid);
          return;
        }
    }
  if (dofork<0)
    {
      /* forking is done after socket established
       * on d: pass the socket
       * all others: cannot happen
       */
      int	*fds;

      fds	= PFD_alloc(_, 2*sizeof (*fds));
      fds[0]	= 1;
      fds[1]	= _->sock;
      PFD_recfds(_, fds);
    }
  if (dofork<=0)
    {
      /* exec is done after fds are possibly received
       * on d: pass the socket (which is in _->recfds)
       * on i: just pass everything as is (passed fds are closed if not option 'keep', see PFD_main_i())
       * on o: pass the received FDs as listed
       * on p: pass the received FDs as listed
       */
      if (_->recfds)
        PFD_map(_);
      PFD_V(_, "exec %s", _->cmd[0]);
    }

  execvp(_->cmd[0], _->cmd);
  PFD_OOPS(_, "exec failure: %s", _->cmd[0]);
}

P(fork, void)
{
  if (!_->dofork)
    {
      if (_->onsuccess)
        return;
      switch (_->mode)
        {
        default:	PFD_INTERNAL("fork() %02x", _->mode);
        case 'd':
        case 'o':	return;
        case 'i':	if (_->connect) return;
        case 'p':
          000;
          /* XXX TODO XXX add socketpair()	*/
          break;
        }
    }
  PFD_exec(_, 1);
}


/***********************************************************************
 * Socket functions
 **********************************************************************/

P(cloexec, void, int fd, int keep)
{
  int	flag;

  flag  = fcntl(fd, F_GETFD, 0);
  if (keep)
    flag	&= ~FD_CLOEXEC;
  else
    flag	|= FD_CLOEXEC;
  if (flag<0 || fcntl(fd, F_SETFD, flag)<0)
    PFD_OOPS(_, "fcntl() fail on %d", fd);
}

P(nonblock, void, int fd)
{
  int	flag;

  flag  = fcntl(fd, F_GETFL, 0);
  if (flag<0 || fcntl(fd, F_SETFL, flag|O_NONBLOCK)<0)
    PFD_OOPS(_, "fcntl() fail on %d", fd);
}

P(blocking, void, int fd)
{
  int	flag;

  flag  = fcntl(fd, F_GETFL, 0);
  if (flag<0 || fcntl(fd, F_SETFL, flag&~O_NONBLOCK)<0)
    PFD_OOPS(_, "fcntl() fail on %d", fd);
}

P(poll, void, int fd, int flag)
{
  struct pollfd pfd;

  pfd.fd	= fd;
  pfd.events	= flag;
  poll(&pfd, (nfds_t)1, _->timeout ? _->timeout : 10000);
}

P(bind, int, struct sockaddr_un *un, socklen_t max)
{
  if (!bind(_->sock, (struct sockaddr *)un, max))
    {
      PFD_V(_, "bind %d: %s", _->sock, _->sockname);
      return 0;
    }
  if (errno != EADDRINUSE)
    PFD_OOPS(_, "cannot bind to socket address: %s", _->sockname);

  PFD_E(_, "bind %d: %s", _->sock, _->sockname);
  if (!un->sun_path[0] || !_->listen)
    return 1;
  if (!_->listen)
    return 1;

  PFD_unlink(_, -1, _->sockname);
  return 1;
}

P(listen, void)
{
  if (listen(_->sock, 1))
    PFD_OOPS(_, "listen() error: %s", _->sockname);
  PFD_V(_, "listen %d: %s", _->sock, _->sockname);
}

P(accept, void)
{
  PFD_nonblock(_, _->sock);
  for (;;)
    {
      int	fd;

      PFD_V(_, "accept %d: %s", _->sock, _->sockname);
      PFD_poll(_, _->sock, POLLIN);
      fd	= accept(_->sock, NULL, NULL);
      if (fd>=0)
        {
          PFD_unlink(_, _->sock, _->sockname);
          PFD_close(_, _->sock, _->sockname);
          PFD_cloexec(_, fd, 0);
          PFD_V(_, "accepted %d", fd);
          _->sock	= fd;
          return;
        }
      if (errno != EINTR)
        {
          PFD_unlink(_, _->sock, _->sockname);	/* do not forget to cleanup!	*/
          PFD_close(_, _->sock, _->sockname);
          PFD_OOPS(_, "accept() error: %s", _->sockname);
        }
    }
}

/* Using connect() this way tastes odd.
 *
 * Due to nonblocking and a connected socket cannot be connected again,
 * we can happily loop over this until it succeeds.
 * So even with timeout 0 this should succeed eventually in a future retry.
 */
P(connect, int, struct sockaddr_un *un, socklen_t max)
{
  PFD_nonblock(_, _->sock);
  if (PFD_R(_, connect(_->sock, (struct sockaddr *)un, max), "connect %d: %s", _->sock, _->sockname) && errno != EISCONN)
    {
      int	err;
      socklen_t	len;

      if (errno != EINPROGRESS && errno != EALREADY)
        return 1;

      /* This code path is untested.
       * EINPROGRESS seems to be impossible with Unix Domain Sockets
       */
      PFD_poll(_, _->sock, POLLIN);	/* obey timeout	*/

      len	= sizeof(err);
      if (PFD_R(_, getsockopt(_->sock, SOL_SOCKET, SO_ERROR, &err, &len) || err, "connect %d: %s", _->sock, _->sockname))
        return 1;
    }
  PFD_blocking(_, _->sock);
  return 0;
}

/* to init, just assign {0} */
struct PFD_retry
  {
    int		inits, count, max, back, ms, incr, limit, total;
  };

P(retry_init, void, struct PFD_retry *r)
{
  /* not DRY, but how to improve this?	*/
  r->inits++;
  /* keep ->count	*/
  r->max	= _->waits && _->waits[0]>0 ? _->waits[1] : 1000;
  r->back	= _->waits && _->waits[0]>1 ? _->waits[2] : 10;
  r->ms		= _->waits && _->waits[0]>2 ? _->waits[3] : 0;
  r->incr	= _->waits && _->waits[0]>3 ? _->waits[4] : 20;
  r->limit	= _->waits && _->waits[0]>4 ? _->waits[5] : 2000;
  r->total	= 0;
}

P(retry, int, struct PFD_retry *r)
{
  if (!r->inits)
    PFD_retry_init(_, r);

  if (r->count > _->retry && _->retry >= 0)
    return 1;	/* number retries exeeded	*/

  if (r->ms)
    {
      unsigned u;

      u	= r->ms;			/* r->ms<0 gives 128s	*/
      if (u > 128000)
        u	= 128000;		/* capped at 128s	*/
      PFD_V(_, "sleep %ums (total %u)", u, r->total);

      /* usleep() cannot sleep more than 1s	*/
      if (u >= 1000)
        sleep(u / 1000);		/* ignore EINTR	*/
      if (u % 1000)
        usleep(1000 * (u % 1000));	/* ignore EINTR	*/

      r->total	+= u;
    }
  else
    r->total++;		/* Evade some edge cases	*/

  if (r->back>0)
    r->ms	+= r->back;
  r->back	+= r->incr;

  if (r->ms > r->max)
    r->ms	= r->max;
  else if (r->total < r->limit)
    return 0;	/* we are in the micro-retries	*/

  if (_->retry < 0)
    PFD_V(_, "retry %u (unlimited)", r->count);
  else
    PFD_V(_, "retry %u of %d", r->count, _->retry);

  r->count++;	/* increment here, because "retry 1 of 0" looks too wrong	*/
  PFD_retry_init(_, r);
  return 0;
}

P(getsockname, void)
{
  const char		*name;

  name	= _->sockname;
  if (name)
    {
      if (!strcmp(name, "-"))
        name	= "0";
      else if (*name=='$')
        name	= getenv(name+1);
    }
  if (!name || !*name)	PFD_INTERNAL("missing socket name: %s", _->sockname);
  if (_->sockname != name)
    {
      name		= PFD_dup(_, name);
      PFD_free(_, (void *)_->sockname);
      _->sockname	= name;
    }

}

/* Open a socket given as numeric argument
 */
P(open_nr, void, int create)
{
  _->sock	= PFD_int(_, _->sockname);

#ifdef SO_DOMAIN
  if (create>=0)
    {
      /* Must be Unix Domain Socket if mode is not 'd' (create==-1 <=> mode=='d')	*/
      int	dom;
      socklen_t	len;

      len	= sizeof dom;
      if (getsockopt(_->sock, SOL_SOCKET, SO_DOMAIN, &dom, &len))
        PFD_OOPS(_, "socket option failure on fd=%d", _->sock);
      if (dom != AF_UNIX)
        PFD_OOPS(_, "socket not AF_UNIX (fd=%d)", _->sock);
    }
#endif

  if (_->listen)
    PFD_listen(_);
  PFD_fork(_);
  if (_->accept || _->listen || (!_->connect && create>0))
    PFD_accept(_);
}

/* Open a socket given as Unix Domain Socket path
 */
P(open_unix, void, int create)
{
  struct sockaddr_un	sun;
  int			max;
  struct PFD_retry	retry = {0};

  max			= strlen(_->sockname);
  if (max > (int)sizeof(sun.sun_path))
    PFD_OOPS(_, "socket path too long: %s", _->sockname);

  sun.sun_family	= AF_UNIX;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
  strncpy(sun.sun_path, _->sockname, sizeof(sun.sun_path));
#pragma GCC diagnostic pop
  if (_->sockname[0]=='@')
    sun.sun_path[0]     = 0;    /* Abstract Linux Socket        */

  _->sock	= socket(sun.sun_family, SOCK_STREAM, 0);
  if (_->sock<0)
    PFD_OOPS(_, "socket() error");
  PFD_cloexec(_, _->sock, 0);

  max		+= offsetof(struct sockaddr_un, sun_path);

  do
    {
      _->done	= 0;	/* allow exec() again	*/

      if (_->accept || _->listen || (!_->connect && create==1))
        {
          if (PFD_bind(_, &sun, max))
            continue;
          PFD_listen(_);
          if (create>=0)
            PFD_fork(_);
          PFD_accept(_);
        }
      else if (PFD_connect(_, &sun, max))
        continue;
      else if (create>=0)
        PFD_fork(_);
      if (create>=0)
        return;

      PFD_exec(_, -1);
      if (!_->ret)
        return;

    } while (!PFD_retry(_, &retry));
  PFD_OOPS(_, "socket open error: %s", _->sockname);
}

P(open_tcp, void, int create)
{
  PFD_OOPS(_, "not yet implemented.  Please use 'bash passfd <>/dev/tcp/$host/$port': %s", _->sockname);
}

/* create==0:	connect to Unix Domain Socket
 * create >0:	create Unix Domain Socket and wait for connection
 * create <0:	connect to some SOCK_STREAM
 * These default action of 'create' can be overwritten by options:
 * ->listen	use listen+accept()
 * ->accept	use accept() only (for existing sockets)
 * ->connect	use connect()
 */
P(open, void, int create)
{
  PFD_getsockname(_);

  /* Numeric socket: use given FD	*/
  if (isdigit(_->sockname[0]))
    return PFD_open_nr(_, create);

  if (create<0)
    switch (_->sockname[0])
      {
      default:	return PFD_open_tcp(_, create);

      case '@':
      case '/':
      case '.':
        break;
      }
  return PFD_open_unix(_, create);
}


/***********************************************************************
 * Commandline parsing
 **********************************************************************/

P(Swait, char * const *, char * const * argv)
{
  struct PFD_retry	r;

  if (_->waits)
    PFD_OOPS(_, "multiple option w");
  argv	= PFD_getints(_, argv+1, &_->waits);
  if (*_->waits > 5)
    PFD_OOPS(_, "too many wait arguments");
  PFD_retry_init(_, &r);
  PFD_V(_, "wait set to max=%d backoff=%d ms=%d increment=%d limit=%d", r.max, r.back, r.ms, r.incr, r.limit);
  return argv;
}

/* multiple retry args are just added
 *
 * Note: retry 5 5 could mean retry 5 times 5ms, which implies multiplication.  We do not do that.
 */
P(S_int, char * const *, char * const * argv, int *ptr, const char *what)
{
  int		*n = 0;
  int		inc;
  const char	*s;

  inc	= 0;
  for (s= *argv; *s; s++)
    if (*s == *what)
      inc++;		/* count number of 'r' in argv[0]	*/

  argv	= PFD_getints(_, argv+1, &n);
  if (n[0])
    while (n[0])
      *ptr	+= n[n[0]--];	/* ignore overflow	*/
  else
    *ptr	+= inc;
  PFD_free(_, n);

  if (*ptr < 0)
    PFD_V(_, "%s set to unlimited", what);
  else
    PFD_V(_, "%s set to %d", what, *ptr);
  return argv;
}

P(Stmeout, char * const *, char * const * argv)
{
  return PFD_S_int(_, argv, &_->timeout, "timeout");
}

P(Sretry, char * const *, char * const * argv)
{
  return PFD_S_int(_, argv, &_->retry, "retry");
}

P(Suse, char * const *, char * const * argv)
{
  /* XXX TODO XXX verbose?	*/
  return PFD_getints(_, argv+1, &_->uses);
}

P(usage, void)
{
  fprintf(stderr, "Usage (v" PASSFD_VERSION " " __DATE__ "): %s options.. mode socket fds.. -- cmd args..\n"
        "options (only first letter is important):\n"
        "	help	show this usage"
        "	accept	create accepting socket (socket must not exist)\n"
        "	listen	create listening socket (socket overwritten if exist)\n"
        "	timeout	timeout (in ms) for accept/connect, default 10000ms\n"
        "	connect	connect to socket\n"
        "	retry	retry connect if fails, default: -1 (or number of 'r's if nr missing)\n"
        "	wait	retry wait: max backoff ms increment limit: default 1000 10 0 20 2000\n"
        "	success	exec cmd on success\n"
        "	error	exec cmd on error\n"
        "	fork	exec cmd after socket established (default for d)\n"
        "	use	use the given FDs for passing (the other) FDs (d and p).  Default: 0\n"
        "	keep	keep passed FDs open for forked cmd ('i' only)\n"
        "	verbose	enable additional output to STDERR\n"
        "mode:\n"
        "	direct	connect to socket, exec cmd with FD, if ok pass socket to 'use'\n"
        "	in	create new socket, wait for conn, remove socket, pass FDs, terminat\n"
        "	out	connect to socket, receive FDs, exec cmd with args and received FDs\n"
        "	pass	connect to socket, receive FDs, sort FDs, pass FDs to 'use'\n"
        "socket:\n"
        "	'-' same as 0, number, @abstract, path\n"
        "	for 'd' it can also be [host]:port[@bind] (path must start with . or /)\n"
        "notes:\n"
        "	-1 is a special value, used for undefined/unlimited etc.\n"
        , _->arg0);
}

/* options.. mode socket fd -- cmd args..
 * ^^^^^^^^^^^^^^
 */
P(setopt, char * const *, char * const * argv)
{
  for (;;)
    {
      if (!*argv)
        PFD_OOPS(_, "missing mode.  One of: d i o p  (Use h for help)");
      switch (**argv)
        {
        default:	PFD_OOPS(_, "invalid/unknown argument: %c", **argv);
        case 0:		PFD_OOPS(_, "empty argument encountered");
        case 'h':	PFD_usage(_);

        case 'l':	_->listen	= 1;			/*fallthru*/
        case 'a':	_->accept	= 1;			break;
        case 'c':	_->connect	= 1;			break;
        /*d*/
        case 'e':	_->onerror	= 1;			break;
        case 'f':	_->dofork	= 1;			break;
        /*hi*/
        case 'k':	_->keepfds	= 1;			break;
        /*lop*/
        case 'r':	argv		= PFD_Sretry(_, argv);	continue;
        case 's':	_->onsuccess	= 1;			break;
        case 't':	argv		= PFD_Stmeout(_, argv);	continue;
        case 'u':	argv		= PFD_Suse(_, argv);	continue;
        case 'w':	argv 		= PFD_Swait(_, argv);	continue;
        case 'v':	_->verbose	= 1;			break;

        /* mode	*/
        case 'd':
        case 'i':
        case 'o':
        case 'p':
          _->mode	= **argv++;
          return argv;
        }
      argv++;
    }
}

/* options.. mode socket fds.. -- cmd args..
 * -------------- ^^^^^^
 */
P(setsock, char * const *, char * const * argv)
{
  _->sockname	= PFD_dup(_, *argv ? *argv++ : "0");
  return argv;
}

/* options.. mode socket fds.. -- cmd args..
 * --------------------- ^^^^^
 */
P(setfds, char * const *, char * const * argv)
{
  return PFD_getints(_, argv, &_->fds);
}


/* options.. mode socket fds.. -- cmd args..
 * ------------------------------ ^^^^^^^^^^
 */
P(setcmd, void, char * const * argv)
{
  _->cmd	= argv;
}


/***********************************************************************
 * Implementation
 **********************************************************************/

/* Options sanity checks
 */
P(check, const char *)
{
  if (_->accept && _->connect)
    return "cannot use connect and accept at the same time";
  if ((_->onsuccess || _->onerror) && _->dofork)
    return "Option f cannot be used together with s or e";
  /* TODO XXX TODO missing additional tests here	*/
  return 0;
}

P(args, void, char * const *argv)
{
  const char	*err;

  argv	= PFD_setopt(_, argv);
  argv	= PFD_setsock(_, argv);
  argv	= PFD_setfds(_, argv);
  if (*argv)
    {
      if (strcmp(*argv++, "--"))
        PFD_OOPS(_, "Missing -- to separate cmd, seen: %s", *--argv);
      PFD_setcmd(_, argv);
    }

  err	= PFD_check(_);
  if (err)
    PFD_OOPS(_, "%s", err);
}


union PFD_pass
    {
      struct cmsghdr		align;
      char			buf[0];
    };

P(sendfd, void, int sock, int *list)
{
  struct iovec		io = { 0 };
  struct msghdr		msg = { 0 };
  struct cmsghdr	*cmsg;
  union PFD_pass	*u;
  uint32_t		mbuf;
  size_t		pl, tot;
  int			*fds, n;
  char			buf[80];

  n			= PFD_ints(_, list, &fds);

  pl			= n * sizeof(int);
  tot			= CMSG_SPACE(pl);

  u			= alloca(tot);

  mbuf			= n;			/* send the number of FDs which will be passed	*/
  io.iov_base		= &mbuf;		/* data to send	*/
  io.iov_len		= sizeof mbuf;		/* length to send	*/

  /* man 3 cmsg	*/
  msg.msg_iov		= &io;			/* iovs to pass	*/
  msg.msg_iovlen	= 1;			/* iov count	*/
  msg.msg_control	= u;			/* real control to pass	*/
  msg.msg_controllen	= tot;			/* length of the control	*/

  cmsg			= CMSG_FIRSTHDR(&msg);	/* fill the control	*/
  cmsg->cmsg_level	= SOL_SOCKET;
  cmsg->cmsg_type	= SCM_RIGHTS;
  cmsg->cmsg_len	= CMSG_LEN(pl);
  memcpy(CMSG_DATA(cmsg), fds, pl);

  PFD_V(_, "sending %d fds:%s", n, PFD_intlist(_, buf, sizeof buf, fds, n));
  if (sendmsg(sock, &msg, 0)<0)
    PFD_OOPS(_, "sendmsg() error socket %d", sock);
}

/* /usr/include/X11/Xtrans/Xtranssock.c
 */
P(recvfd, void)
{
  struct msghdr msg	= {0};
  struct iovec	io	= {0};
  struct cmsghdr	*cmsg;
  union PFD_pass	*u;
  uint32_t		mbuf;
  size_t		pl, tot;
  ssize_t		sz;
  char			buf[80];

  pl			= 255 * sizeof(int);
  tot			= CMSG_SPACE(pl);

  u			= alloca(tot);

  io.iov_base		= &mbuf;
  io.iov_len		= sizeof mbuf;

  msg.msg_iov		= &io;
  msg.msg_iovlen	= 1;
  msg.msg_control	= u;
  msg.msg_controllen	= tot;

  cmsg			= CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level	= SOL_SOCKET;
  cmsg->cmsg_type	= SCM_RIGHTS;
  cmsg->cmsg_len	= CMSG_LEN(pl);

  for (;;)
    {
      sz		= recvmsg(_->sock, &msg, 0);
      if (sz>0)
        break;
      if (errno != EINTR)
        PFD_OOPS(_, "recvmsg() error");
    }
  if (sz != sizeof mbuf)
    PFD_OOPS(_, "recvmsg() %d bytes expedted but %d bytes got", (int)sizeof mbuf, (int)sz);

  cmsg	= CMSG_FIRSTHDR(&msg);
  if (!cmsg)
    PFD_OOPS(_, "recvmsg() no control message (no FDs?)");
      int	n, *fds;

  if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    PFD_OOPS(_, "recvmsg() control message not SCM_RIGHTS");

  n	= cmsg->cmsg_len - CMSG_LEN(0);
  if (n % sizeof *fds)
    PFD_OOPS(_, "recvmsg() control message wrongly padded: %d", (int)n);
  if (mbuf != n/sizeof *fds)
    PFD_OOPS(_, "recvmsg() control message size mismatch: %d expected %u", (int)n, (unsigned)mbuf);

  fds		= PFD_alloc(_, n+sizeof (*fds));
  fds[0]	= mbuf;
  memcpy(fds+1, CMSG_DATA(cmsg), n);
  _->recfds	= fds;

  cmsg	= CMSG_NXTHDR(&msg, cmsg);
  if (cmsg)
    PFD_OOPS(_, "unexpected multiple control messages");

  PFD_V(_, "received %d fds:%s", mbuf, PFD_intlist(_, buf, sizeof buf, fds+1, fds[0]));
}

P(icmp, int, int a, int b)
{
  int	n, m;

  n	= a<_->fds[0] ? _->fds[a+1] : a;
  m	= b<_->fds[0] ? _->fds[b+1] : b;
  return n - m;
}

P(iswap, void, int a, int b)
{
  int	tmp;

  tmp		= _->fds[a];
  _->fds[a]	= _->fds[b];
  _->fds[b]	= tmp;

  tmp		= _->recfds[a];
  _->recfds[a]	= _->recfds[b];
  _->recfds[b]	= tmp;
}

/* Sort *list according to *sort
 *
 * Both are fdlists.
 */
P(sorter, void)
{
  int n, m;

  m	= _->fds[0];
  n	= _->recfds[0];
  if (n < m)
    PFD_OOPS(_, "too few FDs received, got %d, expected at least %d", n, m);
  mergesort(_, n, PFD_icmp, PFD_iswap, PFD_alloc, PFD_free);
}

P(sendfds, void)
{
  int	*fds, n;

  for (n=PFD_ints(_, _->uses, &fds); --n>=0; )
    PFD_sendfd(_, *fds++, _->recfds);
}

P(main_d, void)
{
  PFD_V(_, "pass: direct");
  PFD_open(_, -1);
  PFD_sendfds(_);
}

P(main_i, void)
{
  int	n, *fds;

  for (n=PFD_ints(_, _->fds, &fds); --n>=0; )
    PFD_cloexec(_, fds[n], _->keepfds);
  PFD_V(_, "pass: in");
  PFD_open(_, 1);
  PFD_sendfd(_, _->sock, _->fds);
}

P(main_o, void)
{
  PFD_V(_, "pass: out");
  PFD_open(_, 0);
  PFD_recvfd(_);
}

P(main_p, void)
{
  PFD_V(_, "pass: proxy");
  PFD_open(_, 0);
  PFD_recvfd(_);

  PFD_sorter(_);
  PFD_sendfds(_);
}

P(main, void)
{
  switch (_->mode)
    {
    default:	PFD_INTERNAL("mode not d i o p: %c (%02x)", _->mode, _->mode);
    case 'd':	PFD_main_d(_);	break;
    case 'i':	PFD_main_i(_);	break;
    case 'o':	PFD_main_o(_);	break;
    case 'p':	PFD_main_p(_);	break;
    }
  PFD_exec(_, 0);
}

#undef P

