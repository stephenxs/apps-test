#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
//#include <atomic.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sched.h>

#include <unistd.h>
#include <sys/syscall.h>

int mygettid()
{
#ifdef SYS_gettid
int tid = syscall(SYS_gettid);
#else
#error "SYS_gettid unavailable on this system"
#endif
return tid;
}
unsigned int locktask;
int sched_setpreempt(int enable)
{
#ifndef SYS_sched_enablepreempt
#define SYS_sched_enablepreempt 4351
#if ARCH!=MIPS
#error sched_enablepreempt not defined
#endif
#endif
if (locktask)
return syscall(SYS_sched_enablepreempt, enable);
else return 0;
}

/*set affinity to NR_CPUS - 1*/
int sched_setaffinity()
{
#ifndef SYS_sched_setaffinity
#error sched_setaffinity not defined
#else
  cpu_set_t cpumsk = {0};
  //  cpumask_set_cpu(NR_CPUS - 1, &cpumsk);
  __CPU_SET_S(1, sizeof(cpumsk), &cpumsk);
  printf("setting affinity for %d\n", getpid());
  return syscall(SYS_sched_setaffinity, getpid(), sizeof(cpumsk), &cpumsk);
#endif
}
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
unsigned int quiet = 0;
unsigned int allfinish = 0;
const char *faultinj_fn;
const char *faultinj_ar;

int my_atomic_exc_and_add(int *p, int value, int *lastpos)
{
	int result = *p;
	if (result == 1) {
	  result = value;
	  *p = value;
	}
	else
	  *p += value;
	if (*lastpos > result) {
	  result = *lastpos + value;
	  *p = result;
	  *p += value;
	  printf("reentrance atomic add, jump from %d to %d\n", *lastpos, result);
	}
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
int putvalue(char value, int taskid, int *lastpos)
{
	int position = my_atomic_exc_and_add(testrecordptr, MAX_TASK_LINE, lastpos);
	int oldvalue, newvalue, old2;
	char *pnewvalue;

	if (!*starttest)
	  printf("position %d\n", position);
	if (position >= max_test_count || taskid >= MAX_TASK)
		return -1;//test finish

	value &= 0x7f;
	if (value == 0)
		value = 0xa5;
	if (position <= *lastpos) {
		printf("task %d got position %d error, last %d value %d, jump\n ", taskid, position, *lastpos, value); 
	}
	pnewvalue = (char*)&testrecordarea[position];
	pnewvalue[taskid] = value;
	*lastpos = position;
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
void printline(int pos)
{
	printf("%08x: %08x %08x %08x %08x\n", pos,
		ntohl(testrecordarea[pos]),
		ntohl(testrecordarea[pos+1]),
		ntohl(testrecordarea[pos+2]),
		ntohl(testrecordarea[pos+3]));
}
void printtestbuffer(int quiet)
{
  int i, j, k;
  char c, *ptest;
  unsigned char cgroup_lastvalue[MAX_TASK_LINE] = {0};
  unsigned char cgroup_lasttask[MAX_TASK / MAX_TASK_LINE] = {0};
  unsigned char cgroup_lastcanswitch[MAX_TASK / MAX_TASK_LINE] = {1,1,1,1};
  int cgroup_lastposi[MAX_TASK_LINE];
  unsigned char cgroup, value, task, canswitch;

	if (!quiet) printf("           0 1 2 3  4 5 6 7  8 9 a b  c d e f\n");
	for(i = 0; i < max_test_count; ) {
		if (!quiet)
			printline(i);
		ptest = (char*)&testrecordarea[i];
		cgroup = 0;
		canswitch = 0;
		value = 0;
		for(j = 0; j < MAX_TASK; j++) {
		  if (ptest[j] != 0) {
		    if (value != 0) {
		      if (quiet)
			printline(i);
		      printf("%%warnning: duplicate non-zero value\n");
		    }
		    cgroup = j / MAX_TASK_LINE;
		    value = ptest[j];
		    if (value == 0xa5) value = 0x80;
		    task = j % MAX_TASK_LINE;
		    canswitch = value % 16 == 0 || task == 3 && value % 4 == 0;

		    if (i != 0) {
			if (cgroup_lasttask[cgroup] == task 
				&& ((cgroup_lastvalue[cgroup] + 1)&0x7f) == (value&0x7f)
					|| //task switch permitted
				cgroup_lasttask[cgroup] != task && cgroup_lastcanswitch[cgroup])
				;
			else {// illegal switch
				int k;
				printf("===================================================\n");
				printline(cgroup_lastposi[cgroup]);

				printf("          ");
				for (k = 0; k < cgroup; k++)
					printf("         ");
				for (k = 0; k < cgroup_lasttask[cgroup]; k++)
					printf("  ");
				printf("^^switched!\n");
				printf("task %d cg %d value %d \n", task, cgroup, value);
				printf("cg lasttask %d cg lastvalue %d cg lastcanswitch %d\n",
					cgroup_lasttask[cgroup], cgroup_lastvalue[cgroup], cgroup_lastcanswitch[cgroup]);

				printline(i);
				printf("===================================================\n");
			}

			cgroup_lastvalue[cgroup] = value;
			cgroup_lasttask[cgroup] = task;
			cgroup_lastcanswitch[cgroup] = canswitch;
			cgroup_lastposi[cgroup] = i;
		    }
		  }
		}

		i += 4;
		if (i % 64 == 0 && !quiet) {
			c = getchar();
			if (c == 'q')
				return;
			printf("           0 1 2 3  4 5 6 7  8 9 a b  c d e f\n");
		}
	}

	printf("counter %d\n", unused);
}

void test_entry_1_no_preempt_low_task(int myid)
{
	int rc = 0, i = 0, pos = 0;

	printf("myid is %d, tid is %d\n", myid, mygettid());
	while (!*starttest) usleep(delay*1000);

	//set to no preempt
	rc = sched_setpreempt(0);
	if (rc)
	  printf("disable %d\n", rc);
	rc = 0;
	while (rc == 0)
	{
		i++;
		rc = putvalue(i, myid, &pos);
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
	int rc = 0, i = 0, pos = 0;

        printf("myid is %d, tid is %d\n", myid, mygettid());
	while (!*starttest) usleep(delay);

	while (rc == 0)
	{
		i++;
		rc = putvalue(i, myid, &pos);
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
        printf("big delaie tid is %d\n", mygettid());
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

void test_entry_preempt_page_fault(int unused)
{
    const char *filename = faultinj_fn, *argu = faultinj_ar;
    int arglen = strlen(argu), file, rc;

    printf("tid %d going to write [%s] to %s\n", mygettid(), argu, filename);
    file = open(filename, O_RDWR, 0);
    printf("%s is %d, argu [%s] leng %d\n", filename, file, argu, arglen);

    while (!allfinish){
        usleep(100);
	write(file, argu, arglen);
    }
    printf("wrote [%s] to %s\n", argu, filename);
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

  printf("create task %d p %d entrypoint %x\n", id, priority, (unsigned int)entrypt);
  pthread_create(tid, &attr, (void*(*)(void*))entrypt, (void*)id);
}

void main(int argc, char *argv[])
{
  pthread_t tid[5];
  int rc, i;
  int base_pri = 70, bdelay = 0;
  int segment_id;
	struct sched_param param;

	sched_setaffinity();

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
	      printtestbuffer(quiet);
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
	  case 'q':
	    quiet = 1;
	    break;
	  case 'P':
	    faultinj_fn = argv[i]+1;
	    i++;
	    faultinj_ar = argv[i];
	    break;
	  case '?':
	    printf("usage: %s [l] [d] [y] [T<test count>] [D<delay>] [b<base priority>] [t<delay>] [i<base tid>] [B<delay>]\n", argv[0]);
	    printf("          [C] [s|F|S<sys v shmem region>] [P<filename> <arg>][q]\n");
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
		   "  B -- big delay\n"
		   "  P -- preempt page fault exception\n"
		   "  q -- quiet\n");
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

	if (faultinj_fn) {
	  create_task(0, 0, 0, test_entry_preempt_page_fault, &tid[0]);
	  usleep(1000);
	}
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

	allfinish = 1;
	if (!sharememory) {
	  if (quiet)
	    printtestbuffer(1);
	  else {
	    getchar();
	    printtestbuffer(0);
	  }
	}
}
