/* PassFD: Pass FD to other programs using Unix Domain Socket
 *
 * This Works is placed under the terms of the Copyright Less License,
 * see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>


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

    const char		*sockname;
    int			*fds, *waits, *uses, *recfd;
    char * const	*cmd;
  };


/***********************************************************************
 * Error processing
 **********************************************************************/

#define	PFD_NOTYET(_,X,...)	PFD_OOPS(_, "not yet implemented in %s:%d:%s: " X, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define	PFD_INTERNAL(_,X,...)	PFD_OOPS(_, "internal error in %s:%d:%s: " X, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

P(exec, void);

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
    PFD_exec(_);
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
P(vV, void, int e, const char *s, va_list list)
{
  time_t	t;
  struct tm	tm;

  time(&t);
  gmtime_r(&t, &tm);	/* always use UTC!	*/
  fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d ", tm.tm_year, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
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

  if (!_->verbose)
    return;
  va_start(list, s);
  PFD_vV(_, 0, s, list);
  va_end(list);

  errno	= e;
}

/* PFD_E(_, ..) report error if verbose
 */
P(E, void, const char *s, ...)
{
  int		e = errno;
  va_list	list;

  if (!_->verbose)
    return;
  fprintf(stderr, "fail: ");
  va_start(list, s);
  PFD_vV(_, e, s, list);
  va_end(list);

  errno	= e;
}

/* PFD_R(_, ret, ..) is PFD_V(_, ..) or PFD_E(_, ..) based on return code
 */
P(R, int, int ret, const char *s, ...)
{
  int		e = errno;
  va_list	list;

  if (!_->verbose)
    return ret;
  if (ret)
    fprintf(stderr, "fail: ");
  va_start(list, s);
  PFD_vV(_, e, s, list);
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

P(free, void, const void *p)
{
  if (p)
    free((void *)p);
}

P(dup, char *, const char *s)
{
  char	*r = strdup(s);
  if (!r)
    PFD_OOPS(_, "out of memory");
  return r;
}


/***********************************************************************
 * Argument splitting
 **********************************************************************/

/* Initialize structure */
P(init, void, const char *arg0)
{
  memset(_, 0, sizeof *_);
  _->arg0	= arg0;
}

/* This does the last step, like exec() command, etc.
 * THIS DOES NOT RETURN IF exec() IS DONE
 * else returns the proper exit code
 */
P(exit, int)
{
  /* XXX TODO XXX free everything	*/
  PFD_V(_, "return code %d", _->code);
  return _->code;
}

P(unlink, void, const char *name)
{
  struct stat	st;
  const char	*what;

  /* We can come here for regular files, too, which is very sad.
   * Hence we first must check if it is really a Unix Domain Socket
   * or an empty file (mktemp).
   */
  if (lstat(name, &st))
    {
      if (errno == ENOENT)
        return;
      PFD_OOPS(_, "cannot stat %s", name);
    }

  what="socket";
  switch (st.st_mode & S_IFMT)
    {
    default:
      PFD_OOPS(_, "incompatible file type: %s", name);
    case S_IFREG:
      if (st.st_size || st.st_blocks)
        PFD_OOPS(_, "refuse to remove nonempty file: %s", name);
      what="empty file";
    case S_IFSOCK:
        break;
    }

  PFD_R(_, unlink(name), "unlink %s: %s", what, name);

  /* XXX TODO XXX what to do in error case?	*/
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
    PFD_free(_, p->argv[p->argc]);
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


/***********************************************************************
 * Socket functions
 **********************************************************************/

P(close, void, int sock, const char *name)
{
  for (;;)
    {
      if (!close(sock))
        {
          PFD_V(_, "close %d: %s", sock, name);
          return;
        }
      if (errno != EINTR)
        PFD_OOPS(_, "close %d fail: %s: %s", sock, name);
      PFD_V(_, "close %d interrupted: %s", sock, name);
    }
}

P(connect, int, int sock, struct sockaddr_un *un, socklen_t max, const char *name)
{
  PFD_V(_, "connecting %d: %s", sock, name);
  if (PFD_R(_, connect(sock, (struct sockaddr *)un, max), "connect %d: %s", sock, name))
    return 1;
  _->sock	= sock;
  return 0;
}

P(bind, int, int sock, struct sockaddr_un *un, socklen_t max, const char *name)
{
  if (!bind(sock, (struct sockaddr *)un, max))
    {
      PFD_V(_, "bind %d: %s", sock, name);
      return 0;
    }
  if (errno != EADDRINUSE)
    PFD_OOPS(_, "cannot bind to socket address: %s", name);

  PFD_E(_, "bind %d: %s", sock, name);
  if (!un->sun_path[0] || !_->listen)
    return 1;
  if (!_->listen)
    return 1;

  PFD_unlink(_, name);
  return 1;
}

P(listen, void, int sock, const char *name)
{
  if (listen(sock, 1))
    PFD_OOPS(_, "listen() error: %s", name);
  PFD_V(_, "listen %d: %s", sock, name);
}

P(accept, void, int sock, const char *name)
{
  for (;;)
    {
      int	fd;

      PFD_V(_, "accept %d: %s", sock, name);
      fd	= accept(sock, NULL, NULL);
      if (fd>=0)
        {
          PFD_V(_, "accepted %d", fd);
          _->sock	= fd;
          return;
        }
      if (errno != EINTR)
        PFD_OOPS(_, "accept() error: %s", name);
    }
}

/* to init, just assign {0} */
struct PFD_retry
  {
    unsigned	inited, count, max, back, ms, incr, limit, total;
  };

P(retry_init, void, struct PFD_retry *r)
{
  /* not DRY, but how to improve this?	*/
  r->max	= _->waits && _->waits[0]>0 ? _->waits[1] : 1000;
  r->back	= _->waits && _->waits[0]>1 ? _->waits[2] : 10;
  r->ms		= _->waits && _->waits[0]>2 ? _->waits[3] : 0;
  r->incr	= _->waits && _->waits[0]>3 ? _->waits[4] : 20;
  r->limit	= _->waits && _->waits[0]>4 ? _->waits[5] : 2000;
  r->inited	= 1;
}

P(retry, int, struct PFD_retry *r)
{
  if (!r->inited)
    PFD_retry_init(_, r);

  if (r->count > _->retry && _->retry >= 0)
    return 1;	/* number retries exeeded	*/

  if (r->ms)
    {
      unsigned u;

      u	= r->ms;			/* r->ms<0 gives 128s	*/
      if (u > 128000)
        u	= 128000;		/* capped at 128s	*/
      PFD_V(_, "sleep %ums", u);

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

  r->count	+= 1;
  if (_->retry < 0)
    PFD_V(_, "retry %u (unlimited)", r->count);
  else
    PFD_V(_, "retry %u of %d", r->count, _->retry);
  return 0;
}

P(open, void, int create)
{
  const char		*n;
  struct sockaddr_un	sun;
  int			max, sock;
  struct PFD_retry	retry = {0};

  n	= _->sockname;
  if (n)
    {
      if (!strcmp(n, "-"))
        n	= "0";
      else if (*n=='$')
        n	= getenv(n+1);
    }
  if (!n || !*n)	PFD_INTERNAL(_, "missing socket name: %s", _->sockname);

  /* Numeric socket: use given FD	*/
  if (isdigit(n[0]))
    {
      sock	= PFD_int(_, n);

#ifdef SO_DOMAIN
      int	dom;
      socklen_t	len;

      len	= sizeof dom;
      if (getsockopt(sock, SOL_SOCKET, SO_DOMAIN, &dom, &len))
        PFD_OOPS(_, "socket option failure on fd=%d", sock);
      if (dom != AF_UNIX)
        PFD_OOPS(_, "socket not AF_UNIX (fd=%d)", sock);
#endif

      if (_->listen)
        PFD_listen(_, sock, n);
      PFD_accept(_, sock, n);
      return;
    }

  /* Open Regular or Abstract Unix Domain socket	*/
  max			= strlen(n);
  if (max > (int)sizeof(sun.sun_path))
    PFD_OOPS(_, "socket path too long: %s", n);

  sun.sun_family	= AF_UNIX;
  strncpy(sun.sun_path, n, sizeof(sun.sun_path));
  if (n[0]=='@')
    sun.sun_path[0]     = 0;    /* Abstract Linux Socket        */

  sock	= socket(sun.sun_family, SOCK_STREAM, 0);
  if (_->sock<0)
    PFD_OOPS(_, "socket() error");

  max		+= offsetof(struct sockaddr_un, sun_path);

  for (;;)
    {
      do
        {
          if (_->accept || _->listen || (!_->connect && create))
            {
              if (PFD_bind(_, sock, &sun, max, n))
                break;
              PFD_listen(_, sock, n);
              PFD_accept(_, sock, n);
              PFD_unlink(_, n);
              PFD_close(_, sock, n);
              return;
            }
          if (!PFD_connect(_, sock, &sun, max, n))
            return;
        } while (0);

      if (PFD_retry(_, &retry))
        break;
    }
  PFD_OOPS(_, "socket open error: %s", n);
}


/***********************************************************************
 * Commandline parsing
 **********************************************************************/

P(Swait, char * const *, char * const * argv)
{
  struct PFD_retry	r;

  PFD_NOTYET(_, "wait option");	/* see above	*/
  if (_->waits)
    PFD_OOPS(_, "multiple option w");
  argv	= PFD_getints(_, argv, &_->waits);
  if (*_->waits > 5)
    PFD_OOPS(_, "too many wait arguments");
  PFD_retry_init(_, &r);
  PFD_V(_, "wait set to %d %d %d %d %d", r.max, r.back, r.ms, r.incr, r.limit);
  return argv;
}

/* multiple retry args are just added
 *
 * Note: retry 5 5 could mean retry 5 times 5ms, which implies multiplication.  We do not do that.
 */
P(Sretry, char * const *, char * const * argv)
{
  int	*n = 0;

  argv	= PFD_getints(_, argv, &n);
  if (n[0])
    while (n[0])
      _->retry	+= n[n[0]--];	/* ignore overflow	*/
  else
    _->retry++;
  PFD_free(_, n);

  if (_->retry < 0)
    PFD_V(_, "retries set to unlimited");
  else
    PFD_V(_, "retries set to %d", _->retry);
  return argv;
}

P(Suse, char * const *, char * const * argv)
{
  /* XXX TODO XXX verbose?	*/
  return PFD_getints(_, argv, &_->uses);
}

P(usage, void)
{
  fprintf(stderr, "Usage: %s options.. mode socket fds.. -- cmd args..\n"
        "options (only first letter is important):\n"
        "	help	show this usage"
        "	accept	create accepting socket (socket must not exist)\n"
        "	listen	create listening socket (socket overwritten if exist)\n"
        "	connect	connect to socket\n"
        "	retry	retry connect if fails, default: -1\n"
        "	wait	retry wait: max backoff ms increment limit: default 1000 10 0 20 2000\n"
        "	success	exec cmd on success\n"
        "	error	exec cmd on error\n"
        "	fork	exec cmd after socket established\n"
        "	use	use the given FDs for passing (the other) FDs (p only)\n"
        "	keep	keep passed FDs open for forked command (i only)\n"
        "\n"
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
        PFD_OOPS(_, "missing mode.  One of: i o p  (Use h for help)");
      switch (**argv)
        {
        default:	PFD_OOPS(_, "invalid/unknown argument: %c", **argv);
        case 0:		PFD_OOPS(_, "empty argument encountered");
        case 'h':	PFD_usage(_);

        case 'l':	_->listen	= 1;			/*fallthru*/
        case 'a':	_->accept	= 1;			break;
        case 'c':	_->connect	= 1;			break;
        case 'r':	argv		= PFD_Sretry(_, argv);	continue;
        case 'w':	argv 		= PFD_Swait(_, argv);	continue;
        case 's':	_->onsuccess	= 1;			break;
        case 'e':	_->onerror	= 1;			break;
        case 'f':	_->dofork	= 1;			break;
        case 'u':	argv		= PFD_Suse(_, argv);	continue;
        case 'k':	_->keepfds	= 1;			break;
        case 'v':	_->verbose	= 1;			break;

        /* mode	*/
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
  _->sockname	= *argv ? PFD_dup(_, *argv++) : "0";
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

P(sendfd, void, int sock)
{
  struct iovec		io = { 0 };
  struct msghdr		msg = { 0 };
  struct cmsghdr	*cmsg;
  union PFD_pass	*u;
  uint32_t		mbuf;
  size_t		pl, tot;
  int			fd0, *fds, n;

  if (!_->fds)
    {
      fd0	= 0;
      fds	= &fd0;
      n	= 1;
    }
  else
    {
      fds	= _->fds+1;
      n	= _->fds[0];
    }

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
  fds[0]	= n;
  memcpy(fds+1, CMSG_DATA(cmsg), n);
  _->recfd	= fds;

  cmsg	= CMSG_NXTHDR(&msg, cmsg);
  if (cmsg)
    PFD_OOPS(_, "unexpected multiple control messages");
}

P(exec, void)
{
  if (_->done)
    return;
  _->done	= 1;
  if (!_->cmd)
    return;

  /* XXX TODO XXX pass FDs - NOTYET	*/

  execvp(_->cmd[0], _->cmd);
  PFD_OOPS(_, "exec failure: %s", _->cmd[0]);
}

P(fork, void)
{
  pid_t	pid;

  if (!_->dofork)
    {
      if (_->onsuccess)
        return;
      switch (_->mode)
        {
        default:	PFD_INTERNAL(_, "fork() %02x", _->mode);
        case 'o':	return;
        case 'i':	if (_->connect) return;
        case 'p':	break;
        }
    }
  pid	= fork();
  if (pid == (pid_t)-1)
    PFD_OOPS(_, "fork() failed");
  if (!pid)
    {
      PFD_exec(_);
      exit(1);
    }
  _->done	= 1;
}

P(main_i, void)
{
  PFD_V(_, "pass: in");
  PFD_open(_, 1);
  PFD_fork(_);
  PFD_sendfd(_, _->sock);
}

P(main_o, void)
{
  PFD_V(_, "pass: out");
  PFD_open(_, 0);
  PFD_fork(_);
  PFD_recvfd(_);
}

P(main_p, void)
{
  int	*fds, n, fd;

  PFD_V(_, "pass: proxy");
  PFD_open(_, 0);
  PFD_fork(_);
  PFD_recvfd(_);
  if (_->uses)
    {
      fds	= _->uses+1;
      n		= _->uses[0];
    }
  else
    {
      fd	= 0;
      fds	= &fd;
      n		= 1;
    }
  while (--n >= 0)
    PFD_sendfd(_, *fds++);
}

P(main, void)
{
  switch (_->mode)
    {
    default:	PFD_INTERNAL(_, "mode not i o p: %c (%02x)", _->mode, _->mode);
    case 'i':	PFD_main_i(_);	break;
    case 'o':	PFD_main_o(_);	break;
    case 'p':	PFD_main_p(_);	break;
    }
  PFD_exec(_);
}

#undef P

