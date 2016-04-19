#include "types.h"
#include "user.h"
#include "fcntl.h"

void myHandler(int pid, int value)
{
 printf(2, "%d sent %d\n", pid, value); 
}

int
main(void)
{
  int i, j;
  sigset(myHandler);
  int pid = fork();
  if (pid != 0)
  {
    for (i = 0; i < 100000; ) {i++;}
    fork();
    fork();
    fork();
    fork();
    fork();
    sigsend(pid, 20);
  }
  else
  {
    for (j = 0; j < 1000; j++)
      for (i = 0; i < 10000; ) {i++;}
  }
  if (pid != 0)
  {
    for (i = 0; i < 33; i++)
      wait();
  }
  
  exit();
}