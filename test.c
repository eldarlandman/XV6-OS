#include "types.h"
#include "user.h"
#include "fcntl.h"

void myHandler(int pid, int value)
{
}

int
main(void)
{
  int i, j;
  for (i = 1; i<= 12; i++)
  {
    j = sigsend(2, i);
    printf(2, "sent sig %d to pid=2 returned %d\n", i, j);
  }
  for (i = 0; i<= 1000000; i++){}
  
  return 0;
}