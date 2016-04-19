#include "types.h"
#include "stat.h"
#include "user.h"
#define MAX_DIGITS 11

struct workerList
{
    int pid;
    struct workerList * next;
    struct workerList * prev;
};

void workerHandler(int pid, int value)
{
  //TODO workers handler
}

void primsrvHandler(int pid, int value)
{
  //TODO
}

void doWork(void)
{
  //TODO workers code
}

void initWorkers(struct workerList * workersHead, int workerCount)
{
    struct workerList * newWorker;
    int i, pid;
    workersHead->prev = 0;
    pid = fork();
    if (pid == 0)
    {
      doWork();
    }
    else
    {
      workersHead->pid = pid;
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
	  newWorker = malloc(sizeof(workersHead));
	  newWorker->prev = workersHead;
	  newWorker->pid = pid;
	  newWorker->next = 0;
	  workersHead->next = newWorker;
	  workersHead = newWorker;
	}
      }

}

void sendWorkerSignal(struct workerList * workersHead, int number)
{
  //TODO
}

void waitForWorkers()
{
  //TODO
}

int
main(int argc, char *argv[])
{
  char s[MAX_DIGITS];
  //char * s = &((char)input);
  int workerCount = atoi(argv[1]);
  struct workerList * workersHead = malloc(sizeof(struct workerList));
  if (argc == 2 && workerCount > 0)
  {
    int number;
    initWorkers(workersHead, workerCount);
    sigset(primsrvHandler);
    while (1)
    {
      gets(s, MAX_DIGITS);
      number = atoi(s);
      if (number ==0)
      {
	break;
      }
      sendWorkerSignal(workersHead, number);
      
    }
  }
  else
  {
    printf(1, "signature error: %d args inserted instead of 1", (argc - 1));
  }
  waitForWorkers();
  exit();
  return 0;
}