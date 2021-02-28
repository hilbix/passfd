> This is mostly untested for now
>
> `PASSFDSOCK="$(mktemp)" passfd l i \$PASSFDSOCK 4 -- ssh -o ProxyUseFDPass=yes -o 'ProxyCommand=passfd p $PASSFDSOCK' user@example.com`

[![passfd Build Status](https://api.cirrus-ci.com/github/hilbix/passfd.svg)](https://cirrus-ci.com/github/hilbix/passfd/master)


# PassFD

Shell tool to pass FDs via sockets.

## Usage

	git clone https://github.com/hilbix/passfd.git
	cd passfd
	make
	sudo make install

Then to see the Usage:

	passfd

## About

`passfd` also solves the problem where you cannot pass an FD via `exec()` for some reason.
For example if "parent" wants to pass something to "grandchild", but "child" closes all FDs in between.

`passfd` has 3 modes: in/pipe/out

- Passing "in" means, send one or more FDs of the current process to a unix socket
- Passing means, receive one or more FDs from one socket and pass them to another socket
- Passing "out" means, receive one or more FDs and exec a program which then can use them

	passfd modifiers mode socket fds.. -- command args..

`modifiers`:

- `a` like `accept`: create listening socket, which must not exist
- `l` like `listen`: create listening socket, which is overwritten if it already exists
- `c` like `connect`: connect to socket
- `t` timeout in ms (for `a` and `c`).  Default: 10000ms
- `r` like `retry` optionally followed by a number of retries: retry if something fails.  Defaults to the number of `r`s seen.  -1 means forever
- `w` like `wait` optionally followed by numbers `max` `backoff` `ms` `increment` `limit`.  Waiting when socket setup fails.  Defaults to 1000 10 0 20 2000
- `s` like `success`: execute command when `passfd` terminates successfully
- `e` like `error`: execute command on error (for `o` command then is always executed)
- `f` like `fork`: fork command after socket established (before incoming connect or after successful connection)
- `u` like `use` followed by a list of FDs: use those FDs (compare: `read -u`) to pass the other FDs, default: 0 (this is for `p`)
- `k` keep passed FDs open for forked command, too (this is for `i`)
- `v` enable verbose mode (dumps status to stderr)
- missing: use the defaults for the given mode

`mode`:

- `i` like `into` socket: create new socket, wait for connection to socket, remove socket, pass FDs, terminate
- `o` like `out` of socket: connect to socket, receive FDs, exec command with given args and given FDs
- `p` like `pass`: connect to socket, receive FDs, sort FDs by number, pass FDs to FDs given by `u`se

`socket`:

- `$ENV` to access the given environment variable
- A number, which refers to an open FD.  Note with `a` this does `accept()`, with `l` this does `listen()`+`accept()`
- `-` is the same as `0`
- `@abstract` for abstrat Unix sockets (Linux only)
- Path.  Use `./` for relative files which start with a digit or `@`.

`fds`:

- Must be numeric
- You must give the same number of FDs on both sides!
- If no FD is given, it defaults to 0
- `$ENV` works here, too, the environment variable can be a space separated list

`--`:

- Is always ignored
- But must always be present if `command` follows
- Even if command would not be ambiguous here

`command args..`:

- `i` with `a` or `l` defaults to `f` (this makes sure the socket exists)
- `i` with `c` defaults to `s`
- `o` defaults to `s`
- `p` defaults to `f`.  Note that command gets passed a socketpair as FD3 which receives the FDs

Fun Facts:

- `retry` (without number) increments the number of retries 2 time (as 'retry' includes 2 `r`)
- A `retry` is, when `w` hits the maximum or the limit (max total time).
- Backoff is added to `ms` each loop, then increment is added to backoff.
- Retry numbers can go below 0.
- Waiting is capped at 128000ms if `ms` goes higher.

Example to allow `ssh` to connect to FD 4 of the current shell:

`PASSFDSOCK="$(mktemp)" passfd l i \$PASSFDSOCK 4 -- ssh -o ProxyUseFDPass=yes -o 'ProxyCommand=passfd p $PASSFDSOCK' user@example.com'


# FAQ

WTF why?

- Because I need it

Contact? BUG?

- Open issue on GH, eventually I listen.

Contrib? Patch?

- Open PR on GH, eventually I listen.
- Stick to the license!

License?

- This Works is placed under the terms of the Copyright Less License,  
  see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
- Read: Free as in free beer, free speech and free baby.

