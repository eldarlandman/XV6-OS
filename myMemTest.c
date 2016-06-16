#include "types.h"
#include "user.h"

#define PAGE_SIZE  4096
#define ALLOCATION_COUNT  20

int main(void)
 {//NFU & SCFIFO vs FIFO
  printf(2, "start\n");
  int i, k, j, l;
  char c = 'a';
  for (i = 0; i < 12; i++)
  {
    sbrk(PAGE_SIZE);
  }
  i = 3;
  for (j = 3; j < 13; j++)
  {
    for (k = 0; k < 1000000; k++)
    {
      for (l = 3; l < 13; l++)
      {
	*((char*)(l * PAGE_SIZE)) = *((char*)(l * PAGE_SIZE)) + 1;
	c = *((char*)(l * PAGE_SIZE));
      }
    }
    sbrk(PAGE_SIZE);
    *((char*)(i * PAGE_SIZE)) = *((char*)(i * PAGE_SIZE)) + 1;
    c = *((char*)(i * PAGE_SIZE));
    i = i + 1;
  }
  c++;
 printf(2, "finish %d\n",getpid());
 wait();
 exit();
 }

/*
int main(void)
{//for FIFO vs SCFIFO
  printf(2, "start\n");
  int i, n, k, j;
  char c = 'a';
  k = (int)sbrk(PAGE_SIZE);
  printf(2, "%d\n", k / PAGE_SIZE);
  k = k + (1 * PAGE_SIZE);
  for (i = 0; i < 10; i++)
  {
    n = (int)sbrk(PAGE_SIZE);
  }
  sbrk(PAGE_SIZE);
  for (i = 0; i < 5; i++)
  {
    *((char*)(k + (i * PAGE_SIZE))) = *((char*)(k + (i * PAGE_SIZE))) + 1;
    c = *((char*)(k + (i * PAGE_SIZE)));
  }
  for (i = 0; i < 5; i++)
  {
    n = (int)sbrk(PAGE_SIZE);
    for (j = 0; j < 5; j++)
    {
      *((char*)(k + (j * PAGE_SIZE))) = *((char*)(k + (j * PAGE_SIZE))) + 1;
      c = *((char*)(k + (j * PAGE_SIZE)));
    }
  }
  k = k + (PAGE_SIZE * 10);  
  for (i = 0; i < 5; i++)
  {
    *((char*)(k + (i * PAGE_SIZE))) = *((char*)(k + (i * PAGE_SIZE))) + 1;
    c = *((char*)(k + (i * PAGE_SIZE)));
  }
  for (i = 0; i < 5; i++)
  {
    n = (int)sbrk(PAGE_SIZE);
    for (j = 0; j < 5; j++)
    {
      *((char*)(k + (j * PAGE_SIZE))) = *((char*)(k + (j * PAGE_SIZE))) + 1;
      c = *((char*)(k + (j * PAGE_SIZE)));
    }
  }
  n = 1;c++;
  n++;
  printf(2, "finish %d\n",getpid());
  exit();
}*/