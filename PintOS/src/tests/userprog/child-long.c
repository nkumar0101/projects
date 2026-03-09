/* Test writing to the console a very long message. write-stdout.c */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include "tests/lib.h"

void test_main(void) {

  char buffer[] =
      "This is going to be a rather long message. We are trying to test if the write function to "
      "stdout will have everything written in one go.\nThis may seem familiar; if this is the "
      "second time you're reading this, and in order, great!\n";
  write(1, buffer, sizeof(buffer) - 1);
}