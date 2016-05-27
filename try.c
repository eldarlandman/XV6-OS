#include "types.h"
#include "user.h"

int main(void)
{
  printf(2, "check\n");
  int i;
  for (i = 0; i < 10; i++)
    sbrk(5000);
  printf(2, "check\n");
  exit();
}