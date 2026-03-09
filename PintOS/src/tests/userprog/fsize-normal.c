/* Ensures that filesize syscall works normally. */

#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  test_name = "fsize-normal";

  int handle = open("sample.txt");
  size_t size = filesize(handle);

  /* Size of sample.txt is 239 */
  if (size != 239)
    fail("filesize() returned %d instead of %zu", size, 239);
}