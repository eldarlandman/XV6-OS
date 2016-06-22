#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if (argc < 3)
  {
    printf(1, "invalid args, expected: mount path partNum\n");
    exit();
  }
  int newPart = atoi(argv[2]);
  int res = mount(argv[1], (uint)newPart);
  printf(1, "mount resulted %d\n", res);
  exit();
}