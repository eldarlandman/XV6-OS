#include "types.h"
#include "stat.h"
#include "user.h"

#define CPU 0
#define S_CPU 1
#define IO 2


int
main(int argc, char *argv[])
{
  char * type;
  int pid, n, i, j, k;
  int stime = 0;
  int retime = 0;
  int rutime = 0;
  /*
int stime_acc=0;
int retime_acc=0;
int rutime_acc=0;
*/
  if (argc == 2)
  {
    n = atoi(argv[1]);
    for (i = 0; i < n * 3; i++)
    {
      pid = fork();
      if (pid == 0)
      {
	pid = getpid();
	switch (pid % 3)
	{
	  case(CPU):
	    for(j = 0; j < 100; j++)
	      for(k = 0; k < 1000000; k++){}
	  break;
	  case(S_CPU):
	    for(j = 0; j < 100; j++)
	    {
	      for(k = 0; k < 1000000; k++){}
	      yield();
	    }
	  break;
	  case(IO):
	    for(j = 0; j < 100; j++)
	    {
	      sleep(1);
	    }
	  break;
	  default:
	  break;
	}
	exit();
      }
    }
    for (i = 0; i < n * 3; i++)
    {
      pid= wait2(&retime, &rutime, &stime);
      switch(pid % 3)
      {
	case(CPU):
	  type = "CPU";
	  
	  break;
	case(S_CPU):
	  type = "S-CPU";
	  break;
	case(IO):
	  type = "IO";
	  break;
	default:
	  type = "this should not happen";
	  break;
      }
      printf(1, "pid = %d || type = %s || wait time = %d || run time = %d || sleep time = %d\n", 
	      pid, type, retime, rutime, stime);
    }
  
  /*
  printf(1, "average CPU time= %d  || average S-CPU time= %d || average IO time= %d\n,
  )*/
    
    
  }
  else
  {
    printf(1, "signature error: %d args inserted instead of 1", (argc - 1));
  }
  exit();
}
