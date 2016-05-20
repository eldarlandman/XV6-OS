#include "types.h"
#include "user.h"

int main(void)
{
  printf(2, "check\n");
  int * p = (int*)1000000000;
  (*p) = 5;
  printf(2, "check\n");
  exit();
}