#include "types.h"
#include "stat.h"
#include "user.h"

#define PRIO_1 0
#define PRIO_2 1
#define PRIO_3 2

#define STIME 0  //sleeping or IO time
#define RETIME 1 //ready or waiting time
#define  RUTIME 2 //running time
#define TOTAL 3 //total processes per type


int
main(int argc, char *argv[])
{
#ifdef SML
  int pid, i, j, k, priority;
  
  int retime, stime, rutime;
  
int statis [3][4]; 

    for (i = 0; i < 20; i++)
    {
      pid = fork();
      if (pid == 0)
      {
	 pid = getpid();
	 priority = (pid % 3) + 1; //priority is set to the values 1 - 3 based on pid
	 if (set_prio(priority) < 0)
	 {
	   printf(1, "set priority failed for process %d\n", pid);
	   exit();
	 }
	 sleep(10);
	    for(j = 0; j < 100; j++)
	      for(k = 0; k < 1000000; k++){}
	  exit();
      }
    }
    for (i = 0; i < 20; i++)
    {
      
      pid= wait2(&retime, &rutime, &stime);
      switch(pid % 3)
      {
	case(PRIO_1):
	  statis[PRIO_1][STIME]+=stime;
	  statis[PRIO_1][RETIME]+=retime;
	  statis[PRIO_1][RUTIME]+=rutime;
	  statis[PRIO_1][TOTAL]++;  
	  break;
	  
	case(PRIO_2):
	  statis[PRIO_2][STIME]+=stime;
	  statis[PRIO_2][RETIME]+=retime;
	  statis[PRIO_2][RUTIME]+=rutime;
	  statis[PRIO_2][TOTAL]++;  
	  break;
	  
	case(PRIO_3):
	  statis[PRIO_3][STIME]+=stime;
	  statis[PRIO_3][RETIME]+=retime;
	  statis[PRIO_3][RUTIME]+=rutime;
	  statis[PRIO_3][TOTAL]++;
	  break;
	  
	default:
	  break;
      }
      printf(1, "pid = %d || priority = %d || wait time = %d || run time = %d || sleep time = %d\n", 
	      pid, (pid % 3) + 1, retime, rutime, stime);
    }
  
  
  printf(1, "average time for priority 1 processes:   \n \
  sleeping  time= %d || ready time=%d ||turnaround time= %d \n",
  statis[PRIO_1][STIME] / statis[PRIO_1][TOTAL]  ,  
  statis[PRIO_1][RETIME] / statis[PRIO_1][TOTAL]  ,  
  (statis[PRIO_1][STIME] +statis[PRIO_1][RETIME]+ statis[PRIO_1][RUTIME] ) / statis[PRIO_1][TOTAL]  );
    
printf(1, "average time for priority 2 processes:    \n\
sleeping  time= %d || ready time=%d ||turnaround time= %d \n",
  statis[PRIO_2][STIME] / statis[PRIO_2][TOTAL]  ,  
  statis[PRIO_2][RETIME] / statis[PRIO_2][TOTAL]  ,  
  (statis[PRIO_2][STIME] +statis[PRIO_2][RETIME]+ statis[PRIO_2][RUTIME] ) / statis[PRIO_2][TOTAL]  );
  
printf(1, "average time for priority 3 processes:    \n\
sleeping  time= %d || ready time=%d ||turnaround time= %d \n",
  statis[PRIO_3][STIME] / statis[PRIO_3][TOTAL]  ,  
  statis[PRIO_3][RETIME] / statis[PRIO_3][TOTAL]  ,  
  (statis[PRIO_3][STIME] +statis[PRIO_3][RETIME]+ statis[PRIO_3][RUTIME] )/ statis[PRIO_3][TOTAL]  );

printf(1, "%d\n", statis[PRIO_1][TOTAL] + statis[PRIO_2][TOTAL] + statis[PRIO_3][TOTAL]);
  
#endif
  exit();
}
