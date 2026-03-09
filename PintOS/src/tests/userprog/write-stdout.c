/* Test writing to the console. This will have two processes write
    100+ byte messages with no interleaving if successful. write-stdout.c */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  exec("child-long");
  exec("child-long");
}