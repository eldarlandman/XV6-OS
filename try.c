#include "types.h"
#include "user.h"

int main(void)
{
  printf(2, "start\n");
  int i, n;
  for (i = 0; i < 20; i++)
  {
    n = (int)sbrk(4100);
    printf(2, "allocation %d allcated address %d\n", i, n);
  }
  printf(2, "finish\n");
  exit();
}