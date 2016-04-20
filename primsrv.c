#include "types.h"
#include "stat.h"
#include "user.h"
#define MAX_DIGITS 11

struct workerList
{
    int pid;
    int request;
    int result;
    struct workerList * next;
    struct workerList * prev;
}workerList;

int isPrime(int n) // assuming n > 1
{
    int i;
    if (n == 1) return 1;
     if (n % 2 == 0) return 1;
     for(i = 3; i < n / 2; i+= 2)
     {
         if (n % i == 0)
             return 0;
     }
     return 1;
}

void workerHandler(int pid, int value)
{
  if (value == 0)
  {
    int pid = getpid();
    printf(1, "worker %d exit\n", pid);
    exit();
  }
  else if (value > 0)
  {
  value++;
    while(1)
    {
        if(isPrime(value))
            break;
        value++;
    }
    sigsend(pid, value);
  }
  else
  {
    value = value * -1;
    value--;
    while(1)
    {
        if(isPrime(value))
            break;
        value--;
    }
    sigsend(pid, value);
  }
}

void primsrvHandler(int pid, int value)
{
  struct workerList * workerPtr = &workerList;
  while (workerPtr->pid != pid)
  {
    workerPtr = workerPtr->next;
  }
  workerPtr->result = value;
}

void doWork(void)
{
  while(1)
    sleep(1);
}

void initWorkers(struct workerList * workersHead, int workerCount)
{
    struct workerList * newWorker;
    int i, pid;
    workersHead->prev = 0;
    pid = fork();
    if (pid == 0)
    {
      sigset(workerHandler);
      doWork();
    }
    else
    {
      workersHead->pid = pid;
      printf(1, "workers pids:\n%d\n", pid);
      workersHead->next = 0;
      for (i = 1; i < workerCount; i++)
      {
	pid = fork();
	if (pid == 0)
	{
	  sigset(workerHandler);
	  doWork();
	}
	else
	{
	  printf(1, "%d\n", pid);
	  newWorker = malloc(sizeof(workerList));
	  newWorker->prev = workersHead;
	  newWorker->pid = pid;
	  newWorker->next = 0;
	  workersHead->next = newWorker;
	  workersHead = newWorker;
	}
      }
    }
}

void sendWorkerSignal(struct workerList * workersHead, int number)
{
  while (workersHead->request != 0)
  {
    workersHead = workersHead->next;
    if (workersHead == 0)
    {
      printf(1, "no idle workers\n");
      return;
    }
  }
  workersHead->request = number;
  sigsend(workersHead->pid, number);
}

void endWork(struct workerList * workersHead)
{
  struct workerList * w = workersHead;
  while (w != 0)
  {
    sigsend(w->pid, 0);
    w = w->next;
    sleep(10);
  }
  w = workersHead;
  while (w != 0)
  {
    wait();
    w = w->next;
  }
  printf(1, "primsrv exit\n");
}

void collectResults(struct workerList * workersHead)
{
  struct workerList * workerPtr = workersHead;
  while (workerPtr != 0)
  {
    if (workerPtr->result !=0)
    {
      printf(1, "worker %d returned %d as a result for %d\n", workerPtr->pid, workerPtr->result, workerPtr->request);
      workerPtr->request = 0;
      workerPtr->result = 0;
    }
    workerPtr = workerPtr->next;
  }
}

int
main(int argc, char *argv[])
{
  int i;
  char s[MAX_DIGITS];
  //char * s = &((char)input);
  int workerCount = atoi(argv[1]);
  //struct workerList * workersHead = malloc(sizeof(struct workerList));
  if (argc == 2 && workerCount > 0)
  {
    int number;
    initWorkers(&workerList, workerCount);
    sigset(primsrvHandler);
    while (1)
    {
      printf(1, "please enter a number: ");
      read(0, s, 1);
      if (*s == '\n')
      {
	collectResults(&workerList);
	continue;
      }
      i = 0;
      while (s[i] != '\n' && i <MAX_DIGITS)
      {
	i++;
	read(0, s + i, 1);
      }
      s[i] = 0;
      number = atoi(s);
      if (number == 0)
      {
	break;
      }
      else if (number != 0)
	sendWorkerSignal(&workerList, number);
      for (i = 0; i < MAX_DIGITS; i++)
	s[i]=0;
    }
  }
  else
  {
    printf(1, "signature error: %d args inserted instead of 1", (argc - 1));
  }
  endWork(&workerList);
  exit();
  return 0;
}