#include "types.h"
#include "user.h"

int main(void)
{
  printf(2, "start\n");
  int i, n;
  char c = 'a';
  for (i = 0; i < 20; i++)
  {
    n = (int)sbrk(4096);
    *((char*)n) = c;
    printf(2, "allocation %d allcated address %d wrote %c\n", i, n, c);
    c++;
  }
  if (fork()==0)
  {
    for (i = 0; i < 20; i++)
    {
      printf(2, "found %c on %d\n", *((char*)n), n);
      n = n - 4096;
    }
  }
  printf(2, "finish\n");
  wait();
  exit();
}