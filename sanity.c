#include "types.h"
#include "stat.h"
#include "user.h"

#define CPU 0
#define S_CPU 1
#define IO 2

#define STIME 0  //sleeping or IO time
#define RETIME 1 //ready or waiting time
#define  RUTIME 2 //running time
#define TOTAL 3 //total processes per type


int
main(int argc, char *argv[])
{
  char * type;
  int pid, n, i, j, k;
  
  int retime, stime, rutime;
  
int statis [3][4]; 

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
	  statis[CPU][STIME]+=stime;
	  statis[CPU][RETIME]+=retime;
	  statis[CPU][RUTIME]+=rutime;
	  statis[CPU][TOTAL]++;  
	  break;
	  
	case(S_CPU):
	  type = "S-CPU";
	  statis[S_CPU][STIME]+=stime;
	  statis[S_CPU][RETIME]+=retime;
	  statis[S_CPU][RUTIME]+=rutime;
	  statis[S_CPU][TOTAL]++;  
	  break;
	  
	case(IO):
	  type = "IO";	  
	  statis[IO][STIME]+=stime;
	  statis[IO][RETIME]+=retime;
	  statis[IO][RUTIME]+=rutime;
	  statis[IO][TOTAL]++;
	  break;
	  
	default:
	  type = "this should not happen";
	  break;
      }
      printf(1, "pid = %d || type = %s || wait time = %d || run time = %d || sleep time = %d\n", 
	      pid, type, retime, rutime, stime);
    }
  
  
  printf(1, "average time for CPU processes:    sleeping  time= %d || ready time=%d ||turnaround time= %d \n",
  statis[CPU][STIME] / statis[CPU][TOTAL]  ,  statis[CPU][RETIME] / statis[CPU][TOTAL]  ,  (statis[CPU][STIME] +statis[CPU][RETIME]+ statis[CPU][RUTIME] ) / statis[CPU][TOTAL]  );
    
printf(1, "average time for S_CPU processes:    sleeping  time= %d || ready time=%d ||turnaround time= %d \n",
  statis[S_CPU][STIME] / statis[S_CPU][TOTAL]  ,  statis[S_CPU][RETIME] / statis[S_CPU][TOTAL]  ,  (statis[S_CPU][STIME] +statis[S_CPU][RETIME]+ statis[S_CPU][RUTIME] ) / statis[S_CPU][TOTAL]  );
  
printf(1, "average time for IO processes:    sleeping  time= %d || ready time=%d ||turnaround time= %d \n",
  statis[IO][STIME] / statis[IO][TOTAL]  ,  statis[IO][RETIME] / statis[IO][TOTAL]  ,  (statis[IO][STIME] +statis[IO][RETIME]+ statis[IO][RUTIME] )/ statis[IO][TOTAL]  );
  
  
  }
  else
  {
    printf(1, "signature error: %d args inserted instead of 1", (argc - 1));
  }
  exit();
}
