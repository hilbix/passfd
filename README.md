> This is terribly incomplete for now and some things noted below not yet work.
> 
> However the examples work as advertized.

[![passfd Build Status](https://api.cirrus-ci.com/github/hilbix/passfd.svg)](https://cirrus-ci.com/github/hilbix/passfd/master)


# PassFD

Shell tool to pass FDs via Unix domain sockets.

Of course you can archive a similar result completely different.

The important thing here is, that `passfd` can terminate after the FDs are passed,
such that the FD then can be handled by the program in question directly
without some intermediate data-pumping process.

However this works for programs which can receive or send FDs like:

- `ssh`:  Can receive FDs from `ProxyCommand`.
- `netcat`:  Can send FDs (iE. when used as `ProxyCommand`)

In contrast to special purpose programs like above, `passfd` can act in all directions:

- receive FDs from itself or other programs like `netcat`
- send FDs to itself or other programs like `ssh`
- Fork programs with some passed FDs (like pipes).
- Create some socket and pass it somewhere else (however `netcat` has much more options for this)


## Usage

	git clone https://github.com/hilbix/passfd.git
	cd passfd
	make
	sudo make install

Then to see a short Usage:

	passfd

To see the long Usage:

	passfd h

## About

`passfd` also solves the problem where you cannot pass an FD via `exec()` for some reason.
For example if "parent" wants to pass something to "grandchild", but "child" closes all FDs in between.

`passfd` has following modes:

- Mode "direct" means: Create a socket and pass this to some already open other unix socket
- Mode "in" means:     Send one or more FDs of the current process to a (new) unix socket
- Mode "pipe" means:   Receive one or more FDs from (new) socket and pass them to (open) socket
- Mode "out" means:    Receive one or more FDs and exec a program which then can use them
- Mode "x" means:      Start of bidirectional pipes
- Mode "y" means:      Middle of bidirectional pipes
- Mode "x" means:      End of bidirectional pipes

	passfd modifiers mode socket fds.. -- command args..

`modifiers` (unused: `bgjm`)

- `a` like `accept`: create new listening socket, which must not exist
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
- `n` like `nonce`: (security) use environment variable `$PASSFD_NONCE` for socket communication
- `q` like `quiet`: do not set/modify `PASSFD_` environment variables on forked program
- missing: use the defaults for the given mode

`mode`:

- `d` like `direct` socket: create new socket, exec cmd with FD, if cmd ok pass socket to `use`
- `i` like `into` socket: create new socket, wait for connection to socket, remove socket, pass FDs, terminate
- `o` like `out` of socket: connect to socket, receive FDs, exec command with args and received FDs as given
- `p` like `pipe`: connect to socket, receive FDs, sort FDs by number, pass FDs to FDs given by `u`se

`mode` (future, perhaps):

- `x`/`y`/`z` start/mid/end some bidirectional pipe
  - NOT YET IMPLEMENTED
  - This creates some temporary abstract unix domain sockets for communication and sends their name and some NONCE over the pipe
  - `x` expects some other `passfd y` or `passfd z` on STDOUT
  - `y` expects some `passfd x` or `passfd y` on STDIN and some `passfd y` or `passfd z` on STDOUT
  - `z` expects some `passfd x` or `passfd y` on STDIN

`socket`:

- `$ENV` to access the given environment variable
- A number, which refers to an open FD.  Note with `a` this does `accept()`, with `l` this does `listen()`+`accept()`
- `-` is the same as `0`
- `@abstract` for abstrat Unix sockets (Linux only)
- Path.  Use `./` for relative files which start with a digit or `@`.
- `[host]:port[@bind]` (only valid for mode `d`)
  - `@bind` is half ignored currently

`fds`:

- Must be numeric
- You must give the same number (=count) of FDs on both sides!
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


# Examples

> Look into `Test.sh` for another example for I/O


## D: Open connection to a port and send a parole first

	ssh -o ProxyUseFDPass=yes -o 'ProxyCommand=passfd d %h:1234 1 -- echo hello world' somehost

On `somehost` you can run (and diagnose) `sshd` like this:

	socat -d -d tcp-listen:1234,reuseaddr,fork system:'set -x; read HW && [ "hello world" = "$HW" ] && exec sshd -d -D -i'

Only those who know the parole (`hello world`) can attach to this `ssh` ..


## I/P: Pass some open FD (here: 4) to SSH as the proxied connection:

	PASSFDSOCK="$(mktemp)" passfd l i \$PASSFDSOCK 4 -- ssh -o ProxyUseFDPass=yes -o 'ProxyCommand=passfd p $PASSFDSOCK' user@example.com

- FD 4 must be some open socket, connected to some `sshd`, of course.
- As `ssh` closes all FDs from the outside, a temporary socket is used to pass the FD
- The name of the socket can be passed using the environment
- Here a socket from the filesystem is used, but you can use abstract Unix Domain Sockets (starting with `@`) if you like

You can test this locally with `bash` as follows:

	4<>/dev/tcp/127.0.0.1/22 PASSFDSOCK="$(mktemp)" passfd l i \$PASSFDSOCK 4 -- ssh -o ProxyUseFDPass=yes -o 'ProxyCommand=passfd p $PASSFDSOCK' $LOGNAME 


# BUGs

- It is far too unintuitive to use
  - Can more and better examples help?

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

- This tool is barely tested
  - However the examples work

- `x`, `y`, `z` are missing

- `@bind` (from `[host]:port@bind` not implemented (ignored)


# TODO

This is mostly untested for now ~~and proabably not very useful to others today~~.


## Ideas

Receive FD from some program like `nc -F` and execute some program with it

- something like `passfd x 1 0 nc -F 127.0.0.1 22 | passfd z -- script`
- something like `passfd o '|nc -F 127.0.0.1 22' 3 -- script`

Pass FD (like STDIN) when forked from some program like `busybox nc -e 'passfd ..'` to some other open socket (like `ssh`)

- something like `TMPS="$(mktemp)"; passfd a p "$TMPS" -- busybox nc -e 'passfd i \$TMPS'`
- or (better): `PASSFD_NONCE= passfd n a p @ -- busybox nc -e 'passfd n i @`
- This needs:
  - Passing environment variables `PASSFD_xxx`
  - `@` creates temporary Abstract Unix Domain Sockets
  - `@` connects to Abstract Unix Domain Socket from environment variable `$PASSFD_SOCK`
  - NONCEs (created if `PASSFD_NONCE` is unset or empty)

Make a bi-directional pipe like `producer | passfd x 1 1 -- first program | passfd z -- second program | consumer`

- Idea here is that `passfd` passes the socket into the 2nd passfd via the pipe
- As pipes do not allow to pass FDs, we must transfer a name, not an FD!
- `first` still can read on STDIN from producer and bi-directionally communicates with `second` on STDOUT
- `second` still can write on STDOUT to consumer and bi-directionally communicates with `first` on STDIN
- both `passfd` do not show up in the pipe (as they were only needed to establish the communication)
- hence this looks like `producer | ( first<>second ) | consumer`
- The latter should scale like for: `producer | ( first<>second<>third ) | consumer`
  - Even with some additional connection of `first<>third` as follows:
  - `p | passfd x 2 1 3 -- 1st | passfd y 2 0 -1 1 -- 2nd | passfd z 0 3 0 -- 3rd | c`
  - `passfd x 2 1 3 -- 1st`:    read in nothing,         pass out 2 sockets (S1 S2), execute `1st 1<>S1 3<>S2`
  - `passfd y 2 0 -1 1 -- 2nd`: read in sockets (S1 S2), pass out 2 sockets (S2 S3), execute `2nd 0<>S1 1<>S3`
  - `passfd z 0 3 0 -- 3rd`:    read in sockets (S2 S3), pass out nothing,           execute `3rd 3<>S2 0<>S3`
  - Note that newly created sockets (S1 S2 on `passfd x`, S3 on `passfd y`) must be "consumed" locally

Fill in some default environment variables for the forked program:

- Everything which might be interesing, like nonces etc. on `x`
- Suppressed by `q`


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

