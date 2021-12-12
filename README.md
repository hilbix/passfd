> This is mostly untested for now

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

`passfd` has 4 modes: direct/in/pipe/out

- Mode "direct" means: Create a socket and pass this to some already open other unix socket
- Mode "in" means:     Send one or more FDs of the current process to a (new) unix socket
- Mode "pipe" means:   Receive one or more FDs from (new) socket and pass them to (open) socket
- Mode "out" means:    Receive one or more FDs and exec a program which then can use them

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

- `d` like `direct` socket: create new socket, wait for connection to socket, remove socket, pass FDs, terminate
- `i` like `into` socket: create new socket, wait for connection to socket, remove socket, pass FDs, terminate
- `o` like `out` of socket: connect to socket, receive FDs, exec command with given args and given FDs
- `p` like `pipe`: connect to socket, receive FDs, sort FDs by number, pass FDs to FDs given by `u`se

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
- `p` defaults to `f`.  ~~In future, cmd gets passed a socketpair which receives the FDs~~

Fun Facts:

- `retry` (without number) increments the number of retries 2 times (as 'retry' includes 2 `r`)
- A `retry` is, when `w` hits the maximum or the limit (max total time).
- Backoff is added to `ms` each loop, then increment is added to backoff.
- Retry numbers can go below 0.
- Waiting is capped at 128000ms if `ms` goes higher.

Example to allow `ssh` to connect to FD 4 of the current shell:

	PASSFDSOCK="$(mktemp)" passfd l i \$PASSFDSOCK 4 -- ssh -o ProxyUseFDPass=yes -o 'ProxyCommand=passfd p $PASSFDSOCK' user@example.com'


# BUGs

- socketpair option not yet implemented
  - I am not completely sure how to do it properly and in which situation it would be helpful
  - If you have any idea please open Issue

- Inverse passing not yet implemented
  - Means: pass from open socket to new socket
  - I am not sure if this is needed at all
  - I have no good idea how to call it
  - Can be emulated with: `passfd out - 0 -- passfd in socket 0`

- `direct` mode is limited
  - It would be very demanding to implement everything which might be needed
  - If this is a problem use a wrapper command like `socat` (or my tool `socklinger`) to do the actual wrapping
  - However the tool should not `fork()`.  It should just `connect()` and then `exec()` passfd
  - Also it is a bit redundant as `bash` supports something like `<>/dev/tcp/IP/port`

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

