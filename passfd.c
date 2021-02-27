/* PassFD: Pass FD to other programs using Unix Domain Socket
 *
 * This Works is placed under the terms of the Copyright Less License,
 * see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
 */

#include "passfd.h"

/* This also is an example how to use passfd.h
 */

int
main(int argc, char * const *argv)
{
  static struct PFD_passfd _;

  PFD_init(&_, argv[0]);
  PFD_args(&_, argv+1);
  PFD_main(&_);
  return PFD_exit(&_);
}

