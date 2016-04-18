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
  int i;
  sigset(myHandler);
  int pid = fork();
  if (pid != 0)
    sigsend(pid, 20);
  
  for (i = 0; i < 100000; ) {i++;}
  if (pid != 0)
    wait();
  
  exit();
}