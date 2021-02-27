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
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>

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

    unsigned		listen:1, accept:1, connect:1, onsuccess:1, onerror:1, dofork:1, keepfds:1;
    unsigned char	mode;
    const char		*sockname;
    int			*fds, *waits, *uses, *recfd;
    char * const	*cmd;
  };


/***********************************************************************
 * Error processing
 **********************************************************************/

#define	PFD_INTERNAL(_,X,...)	PFD_OOPS(_, "internal error in %s:%d:%s: " X, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

P(exec, void);

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

P(OOPS, void, const char *s, ...)
{
  va_list	list;

  va_start(list, s);
  PFD_vOOPS(_, errno, s, list);
  va_end(list);	/* never reached	*/
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
  return _->code;
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

P(int, int, const char *s)
{
  char		*end;
  unsigned long	u;
  int		i;

  u	= strtoul(s, &end, 10);
  i	= u;
  if (i<0 || i != u || !end || *end)
    PFD_OOPS(_, "Not a positive number: %s", s);
  return i;
}

P(getuint, int, int **i, const char *s)
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
        if (*argv && PFD_getuint(_, i, *argv))
          PFD_OOPS(_, "Parsing env var %s failed", s);
      PFD_split_free(_, split);
      return 0;
    }

  if (!isdigit(*s))
    return 1;

  *i		= PFD_realloc(_, *i, (1 + ++(**i)) * sizeof **i);
  i[0][**i]	= PFD_int(_, s);
  return 0;
}

P(getuints, char * const *, char * const * argv, int **i)
{
  *i	= PFD_alloc(_, sizeof **i);
  **i	= 0;
  for (;; argv++)
    if (PFD_getuint(_, i, *argv))
      return argv;
}


/***********************************************************************
 * Socket functions
 **********************************************************************/

P(close, void, int sock, const char *name)
{
  close(sock);
  /* XXX TODO XXX error processing	*/
}

P(connect, int, int sock, struct sockaddr_un *un, socklen_t max, const char *name)
{
  if (connect(sock, (struct sockaddr *)un, max))
    return 1;
  _->sock	= sock;
  return 0;
}

P(bind, int, int sock, struct sockaddr_un *un, socklen_t max, const char *name)
{
  if (!bind(sock, (struct sockaddr *)un, max))
    return 0;
  if (errno != EADDRINUSE)
    PFD_OOPS(_, "cannot bind to socket address: %s", name);
  if (!un->sun_path[0] || !_->listen)
    return 1;
  if (!_->listen)
    return 1;
  PFD_INTERNAL(_, "not yet implemented");
#if 0
  000;
  unlink(_->sockname);
#endif
  return 1;
}

P(listen, void, int sock, const char *name)
{
  if (listen(sock, 1))
    PFD_OOPS(_, "listen() error: %s", name);
}

P(accept, void, int sock, const char *name)
{
  for (;;)
    {
      int	fd;

      fd	= accept(sock, NULL, NULL);
      if (fd>=0)
        {
          _->sock	= fd;
          return;
        }
      if (errno != EINTR)
        PFD_OOPS(_, "accept() error: %s", name);
    }
}

P(unlink, void, const char *name)
{
  unlink(name);
  /* XXX TODO XXX error processing	*/
}

P(open, void, int create)
{
  const char		*n;
  struct sockaddr_un	sun;
  int			max, sock;

  n	= _->sockname;
  if (n)
    {
      if (!strcmp(n, "-"))
        n	= "0";
      else if (*n=='$')
        n	= getenv(n+1);
    }
  if (!n || !*n)	PFD_INTERNAL(_, "missing socket name: %s", _->sockname);

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

  sun.sun_family	= AF_UNIX;
  max			= strlen(n);
  if (max > (int)sizeof(sun.sun_path))
    PFD_OOPS(_, "socket path too long: %s", n);

  strncpy(sun.sun_path, n, sizeof(sun.sun_path));
  if (n[0]=='@')
    sun.sun_path[0]     = 0;    /* Abstract Linux Socket        */

  sock	= socket(sun.sun_family, SOCK_STREAM, 0);
  if (_->sock<0)
    PFD_OOPS(_, "socket() error");

  max		+= offsetof(struct sockaddr_un, sun_path);

  for (;;)
    {
      for (;;)
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
          if (PFD_connect(_, sock, &sun, max, n))
            break;
          return;
        }
      /* XXX TODO XXX implement retry	*/
      PFD_OOPS(_, "socket open error: %s", n);
    }
}


/***********************************************************************
 * Commandline parsing
 **********************************************************************/

P(setwait, char * const *, char * const * argv)
{
  if (_->waits)
    PFD_OOPS(_, "multiple option w");
  return PFD_getuints(_, argv, &_->waits);
}

P(setuse, char * const *, char * const * argv)
{
  return PFD_getuints(_, argv, &_->uses);
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
        case 'w':	argv 		= PFD_setwait(_, argv);	continue;
        case 's':	_->onsuccess	= 1;			break;
        case 'e':	_->onerror	= 1;			break;
        case 'f':	_->dofork	= 1;			break;
        case 'u':	argv		= PFD_setuse(_, argv);	continue;
        case 'k':	_->keepfds	= 1;			break;

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
  return PFD_getuints(_, argv, &_->fds);
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
  PFD_open(_, 1);
  PFD_fork(_);
  PFD_sendfd(_, _->sock);
}

P(main_o, void)
{
  PFD_open(_, 0);
  PFD_fork(_);
  PFD_recvfd(_);
}

P(main_p, void)
{
  int	*fds, n, fd;

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

