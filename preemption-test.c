#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <atomic.h>
#include <sys/shm.h>
#include <sys/stat.h>

#define MAX_RECORD	0x800000
#define MAX_TASK	16
#define MAX_TASK_LINE	4
#define WAIT_10_SEC	0x10000000
#define USLEEP_10_SEC	10000000

int *testrecordarea;
int _testrecordptr;
int *testrecordptr = &_testrecordptr;

unsigned int volatile unused;
unsigned int _starttest;
unsigned int *starttest = &_starttest;
unsigned int total;
unsigned int locktask;
unsigned int delay = 100;
unsigned int basetid;
unsigned int sharememory;
unsigned int switch_by_delay;
unsigned int switch_by_yield;
unsigned int max_test_count = 0x1000;
unsigned int bigdelay = 0;

int my_atomic_exc_and_add(int *p, int value)
{
	int result = *p;
	if (*p == 1)
	  *p = value;
	else
	  *p += value;
	return result;
}
//__NR_sched_setpreempt
//#define __NR_sched_setpreempt (4000 + 315)

int busywait(int period)
{
	for(;period > 0; period --)
		unused++;
}

//int my_atomic_comp_and_exc_byte(char *p, 
int putvalue(char value, int taskid)
{
	int position = my_atomic_exc_and_add(testrecordptr, MAX_TASK_LINE);
	int oldvalue, newvalue, old2;
	char *pnewvalue;

	if (!*starttest)
	  printf("position %d\n", position);
	if (position >= max_test_count || taskid >= MAX_TASK)
		return -1;//test finish

	pnewvalue = (char*)&testrecordarea[position];
	pnewvalue[taskid] = value;
	return 0;
#if 0
	while (1) {
		oldvalue = testrecordarea[position];
		newvalue = oldvalue;
		pnewvalue = &newvalue;
		pnewvalue[taskid] = value;
		old2 = atomic_compare_and_exchange_val_acq(
			testrecordarea + position,
			newvalue, oldvalue);
		printf("old2 %x oldval %x newvalue %x\n", old2, oldvalue, newvalue);
		/*if (old2 == oldvalue)*/
			return 0;
	}
#endif
}

void printtestbuffer()
{
	int i;
	char c;

	printf("           0 1 2 3  4 5 6 7  8 9 a b  c d e f\n");
	for(i = 0; i < max_test_count; ) {
		printf("%08x: %08x %08x %08x %08x\n", i,
			testrecordarea[i],
			testrecordarea[i+1],
			testrecordarea[i+2],
			testrecordarea[i+3]);
		i += 4;
		if (i % 64 == 0) {
			c = getchar();
			if (c == 'q')
				return;
			printf("           0 1 2 3  4 5 6 7  8 9 a b  c d e f\n");
		}
	}
	printf("counter %d\n", unused);
}
int sched_setpreempt(int enable)
{
      long err = 0;
      if (locktask) {
	    register long __v0 __asm__("$2") ;
	    register long __a0 __asm__("$4") = (long) enable;
	    register long __a3 __asm__("$7");
	    __asm__ __volatile__ (
				  ".set\tnoreorder\n\t"
				  "li\t$2, %2\t\t\t# "
				  "sched_setpreempt"
				  "\n\t"
				  "syscall\n\t"
				  ".set reorder" : "=r" (__v0), "=r" (__a3)
				                 : "i" (((4000 + 351))), "r" (__a0) 
				                 : "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "$24", "$25", "hi", "lo", "memory"
				  );
	    err = __a3;
      }
      return err;
}

void test_entry_1_no_preempt_low_task(int myid)
{
	int rc = 0, i = 0;

	while (!*starttest) usleep(delay);

	//set to no preempt
	rc = sched_setpreempt(0);
	if (rc)
	  printf("disable %d\n", rc);
	rc = 0;
	while (rc == 0)
	{
		i++;
		rc = putvalue(i, myid);
		busywait(0x10000);
		if (i % 16 == 0)
		{
		  if (switch_by_delay)
		    usleep(delay) /*set to preemptable*/;
		  else if (switch_by_yield)
		    sched_yield();
		  else {
		    sched_setpreempt(1);
		    sched_setpreempt(0);
		  }
			if (bigdelay)
			{
			    printf("low bigdelay\n");
				sleep(bigdelay);
			}
		}
	}
	rc = sched_setpreempt(1);
	if (rc)
	  printf("enable %d\n", rc);

	total --;
	printf("task %d final %d\n", myid, i);
}

void test_entry_1_preempt_high_task(int myid)
{
	int rc = 0, i = 0;

	while (!*starttest) usleep(delay);

	while (rc == 0)
	{
		i++;
		rc = putvalue(i, myid);
		busywait(0x10000);
		if (i % 4 == 0)
		{
			usleep(delay);
			if (bigdelay)
			{
				printf("high bigdelay\n");
				sleep(bigdelay);
			}
		}
	}

	total --;
	printf("task %d final %d\n", myid, i);
}

void test_entry_big_delaier(int bdelay)
{
	while (!*starttest) usleep(delay);
	printf("before sleep %d\n", bdelay);
	sleep(bdelay);
	printf("set bigdelay\n");
	bigdelay = bdelay;
	printf("before sleep %d\n", bdelay);
	sleep(bdelay);
	printf("clear bigdelay\n");
	bigdelay = 0;
}

int testdelaystop;
volatile int testdelaycount;
void test_entry_test_delay(int testdelay)
{
  if (testdelay) {
    usleep(testdelay);
    testdelaystop = 1;
    printf("testdelay count is %d after usleep(%d)\n", testdelaycount, testdelay);
  } else {
    while(!testdelaystop)
      testdelaycount++;
  }
}

void create_task(int id, int priority, int preemptable, void (*entrypt)(int), pthread_t *tid)
{
  pthread_attr_t attr;
  struct sched_param schedp;

  total++;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8192);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  schedp.sched_priority = priority;
  pthread_attr_setschedparam(&attr, &schedp);

  printf("create task %d p %d\n", id, priority);
  pthread_create(tid, &attr, (void*(*)(void*))entrypt, (void*)id);
}

void main(int argc, char *argv[])
{
  pthread_t tid[5];
  int rc, i;
  int base_pri = 70, bdelay = 0;
  int segment_id;
	struct sched_param param;

	param.sched_priority = 70;
	rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if 0
	printtestbuffer();
printf("before wait \n");
//busywait(0x1000000);
//usleep(10000000);
printf("after wait\n");
	putvalue('A', 0);
	putvalue('C', 2);
	putvalue('B', 1);
	putvalue('D', 3);
	printtestbuffer();
#endif
	for(i = 1; i < argc; i++){
	  switch (argv[i][0]) {
	  case 'l':
	    locktask = 1;
	    break;
	  case 'd':
	    switch_by_delay = 1;
	    switch_by_yield = 0;
	    break;
	  case 'y':
	    switch_by_yield = 1;
	    switch_by_delay = 0;
	    break;
	  case 't':
	    rc = 1000000;
	    sscanf(argv[i]+1, "%d", &rc);
	    create_task(rc, 99, 0, test_entry_test_delay, &tid[1]);
	    create_task(0, 98, 0, test_entry_test_delay, &tid[0]);
	    pthread_join(tid[0], NULL);
	    pthread_join(tid[1], NULL);
	    return;
	  case 'T':
	    rc = MAX_RECORD;
	    sscanf(argv[i]+1, "%d", &rc);
	    max_test_count = rc;
	    
	    break;
	  case 'D':
	    rc = delay;
	    sscanf(argv[i]+1, "%d", &rc);
	    delay = rc;
	    break;
	  case 'b':
	    rc = base_pri;
	    sscanf(argv[i]+1, "%d", &rc);
	    base_pri = rc;
	    break;
	  case 'M':
	    sscanf(argv[i]+1, "%d", &rc);
	    printf("we got pointer %x with size %d\n",
		   malloc(rc), rc);
	    return;
	  case 'i':
	    rc = basetid;
	    sscanf(argv[i]+1, "%d", &rc);
	    if (rc % 4) {
	      printf("bast tid should be integral multiple of 4\n");
	      return;
	    }
	    basetid = rc;
	    break;
	  case 'C':
	    segment_id = shmget(IPC_PRIVATE, sizeof(int) * MAX_RECORD,
				IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	    printf("we got segment id %d\n", segment_id);
	    return;
	  case 's':
	  case 'F':
	  case 'S':
	    sscanf(argv[i]+1, "%d", &rc);
	    testrecordarea = shmat(rc, 0, 0);
	    if (NULL == testrecordarea) {
	      printf("wrong segment id %s\n", argv[i]);
	      return;
	    }
	    if (argv[i][0] == 'F') {
	      printf("start %d\n", *testrecordarea);
	      *testrecordarea = 0;
	      testrecordarea++;
	      printtestbuffer();
	      return;
	    }
	    if (argv[i][0] == 'S') {
	      memset(testrecordarea, 0, sizeof(int) * MAX_RECORD);
	      *testrecordarea = 1;
	      return;
	    }
	    break;
	  case 'B':
	    sscanf(argv[i]+1, "%d", &bdelay);
	    break;
	  case '?':
	    printf("usage: %s [l] [d] [y] [T<test count>] [D<delay>] [b<base priority>] [t<delay>] [i<base tid>] [B<delay>]\n", argv[0]);
	    printf("          [C] [s|F|S<sys v shmem region>]");
	    printf("  l -- locktask\n"
		   "  d -- switch lower priority thread by usleep(default by preemption)\n"
		   "  y -- switch lower priority thread by yield\n"
		   "  T -- max test count\n"
		   "  D -- delay per usleep\n"
		   "  b -- base priority\n"
		   "  M -- malloc a buffer\n"
		   "  i -- base tid\n"
		   "  C -- create shmem region\n"
		   "  s -- use shmem region\n"
		   "  S -- start a shmem test\n"
		   "  F -- finish a shmem region test and printout result\n"
		   "  t -- test busywait vs. usleep\n"
		   "  B -- big delay\n");
	    return;
	  default:
	    printf("invalid argument %s\n", argv[i]);
	    return;
	  }
	}

	printf("%s with %s %s \n\tmax test %d\n\tdelay %d\n", argv[0],
	       locktask ? "lock task" : "not lock task",
 	       switch_by_delay ? "switch by delay" : "switch by preemption",
	       max_test_count,
	       delay);

	create_task(basetid + 0, base_pri, 0, test_entry_1_no_preempt_low_task, &tid[0]);
	base_pri += 4;
	create_task(basetid + 1, base_pri, 0, test_entry_1_no_preempt_low_task, &tid[1]);
	base_pri += 4;
	create_task(basetid + 2, base_pri, 0, test_entry_1_no_preempt_low_task, &tid[2]);
	base_pri += 4;
	create_task(basetid + 3, base_pri, 0, test_entry_1_preempt_high_task, &tid[3]);
	if (bdelay)
	  create_task(bdelay, base_pri, 0, test_entry_big_delaier, &tid[4]);

	if (NULL == testrecordarea) {
	  testrecordarea = malloc(sizeof(int) * MAX_RECORD);
	  *starttest = 1;
	} else {
	  sharememory = 1;
	  starttest = testrecordarea;
	  testrecordptr = testrecordarea;
	  testrecordarea++;
	}

	pthread_join(tid[0], NULL);
	pthread_join(tid[1], NULL);
	pthread_join(tid[2], NULL);
	pthread_join(tid[3], NULL);

	if (!sharememory) {
	  getchar();
	  printtestbuffer();
	}
}
