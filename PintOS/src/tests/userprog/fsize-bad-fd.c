/* Ensures that filesize syscall exits -1 for bad file descriptors. */

#include <limits.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  test_name = "fsize-bad-fd";

  filesize(0x20101234);
  filesize(5);
  filesize(1234);
  filesize(-1);
  filesize(-1024);
  filesize(INT_MIN);
  filesize(INT_MAX);
}