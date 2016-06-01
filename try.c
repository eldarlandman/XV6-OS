#include "types.h"
#include "user.h"

#define PAGE_SIZE  4096
#define ALLOCATION_COUNT  20

/*int main(void)
{
  printf(2, "start\n");
  int i, n;
  char *p;
  for (i = 0; i < 10; i++);
  sbrk(4096);
  p = sbrk(4096);
  n =(int)*p;
  n++;
  for (i = 0; i < 10; i++);
  printf(2, "finish %d\n",getpid());
  wait();
  exit();
}*/

int main(void)
{
  printf(2, "start\n");
  int i, n;
  char c = 'a';
  for (i = 0; i < ALLOCATION_COUNT; i++)
  {
    n = (int)sbrk(PAGE_SIZE);
    *((char*)n) = c;
    printf(2, "allocation %d allcated address %d wrote %c\n", i, n, c);
    c++;
  }
  if (fork()==0)
  {
    sbrk(-PAGE_SIZE);
    sbrk(-PAGE_SIZE);
    n = n - PAGE_SIZE;
    n = n - PAGE_SIZE;
    for (i = 0; i < 18; i++)
    {
      printf(2, "found %c on %d\n", *((char*)n), n);
      n = n - PAGE_SIZE;
    }
  }
  printf(2, "finish %d\n",getpid());
  wait();
  exit();
}