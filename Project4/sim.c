#include "global.h"

extern int initTraceFiles(int);
extern void displayCacheBlock(int, int);
extern char * h(int);
extern void getParams(int, char **);
extern int  getNextRef(FILE *, struct tracerecord *);
extern void insertQueue(int,  struct genericQueueEntry *);
extern void * getFromQueue(int);
extern void makeQueue(int);

int NUM_PROCESSORS;
int MAX_DELAY;
int MODE;
int TRACE;
int *dataArray;

int MEM[2 * TOTALSIZE]; // Physical Memory
struct cacheblock CACHE[MAX_NUM_PROCESSORS][CACHESIZE];  // Cache



// Statistics
int cache_write_hits[MAX_NUM_PROCESSORS], cache_write_misses[MAX_NUM_PROCESSORS];
int cache_read_hits[MAX_NUM_PROCESSORS], cache_read_misses[MAX_NUM_PROCESSORS];
int cache_upgrades[MAX_NUM_PROCESSORS], cache_writebacks[MAX_NUM_PROCESSORS];
int numUtoX[MAX_NUM_PROCESSORS];
int numThreadsCompleted = 0;

// Input
//struct tracerecord  tracerec;   // Trace record read from trace file
char *traceFile[MAX_NUM_PROCESSORS];
FILE *fp[MAX_NUM_PROCESSORS];

// Synchronization Variables
SEMAPHORE *sem_memreq[MAX_NUM_PROCESSORS];
SEMAPHORE *sem_memdone[MAX_NUM_PROCESSORS];
SEMAPHORE *sem_bussnoop[MAX_NUM_PROCESSORS], *sem_bussnoopdone[MAX_NUM_PROCESSORS];

//   Bus Arbitration Signals
int BUS_REQUEST[MAX_NUM_PROCESSORS];  // Bus Request
int BUS_GRANT[MAX_NUM_PROCESSORS];  // Bus Grant
int BUS_RELEASE; // Bus Released
struct busrec BROADCAST_CMD;   // Broadcast request

PROCESS *proccntrl, *memcntrl, *buscntrl, *bussnooper, *cachecntrl, *memwritecntrl;


void cleanUp()  {
 int i;
     // Simulation complete. All records in tracefile have been processed
 int tcache_read_hits =0, tcache_read_misses=0, tcache_write_hits=0, tcache_upgrades=0, tcache_write_misses=0, tcache_writebacks=0,tnumUtoX = 0;

 for (i=0; i < NUM_PROCESSORS; i++) {
   printf("\nProcessor: %d\tSILENT READ HITS: %d\tREAD MISSES: %d\tSILENT WRITE HITS: %d\tUPGRADES: %d\tWRITE MISSES: %d\tWRITEBACKS: %d\tNum  UPGRADES converted to BUSRDX: %d\tTime: %5.2f\n", i, (cache_read_hits[i] - cache_read_misses[i]), cache_read_misses[i], cache_write_hits[i] - cache_write_misses[i], cache_upgrades[i], cache_write_misses[i], cache_writebacks[i],numUtoX[i],GetSimTime());
   printf("Simulation ended  at %5.2f\n",GetSimTime());
   tcache_read_hits += (cache_read_hits[i] - cache_read_misses[i]);
   tcache_read_misses += cache_read_misses[i];
   tcache_write_hits  += (cache_write_hits[i] - cache_write_misses[i]);
   tcache_upgrades  += cache_upgrades[i];
   tcache_write_misses += cache_write_misses[i];
   tcache_writebacks += cache_writebacks[i];
   tnumUtoX += numUtoX[i];
 }
 
 printf("\n\nTOTALS: SILENT READ HITS: %d\tREAD MISSES: %d\tSILENT WRITE HITS: %d\tUPGRADES: %d\tWRITE MISSES: %d\tWRITEBACKS: %d\tTime: %5.2f\tNumber UPGRADES  Converted to BUSRDX: %d\n", tcache_read_hits, tcache_read_misses, tcache_write_hits, tcache_upgrades, tcache_write_misses,tcache_writebacks,tnumUtoX,GetSimTime());
 
 
 for (i=0; i < TOTALSIZE; i++)
   if (TRACE)
     printf("dataArray[%p] : %d\n",    dataArray + i,  *(dataArray  + i) );
      exit(1);
}

void  initializeSemaphores() {
    int i;
    
    for (i=0; i < NUM_PROCESSORS; i++) {
      sem_memreq[i] = NewSemaphore("memreq",0);          
      sem_memdone[i] = NewSemaphore("memdone",0);
      sem_bussnoop[i] = NewSemaphore("bussnoop", 0);    // BackEndController(BusSnooper)  i  waits on sem_bussnoop[i]
      sem_bussnoopdone[i] = NewSemaphore("bussnoopdone", 0);    // Must wait for all Snoopers to be done
    }
  }


void  initializeMem() {
  // Initialize Memory to match the initial values in the trace 
    int i;
    dataArray = (int *) (((unsigned long) MEM) & (-1L << BLKSIZE)); // Align address to block boundary
    printf("Base Physical Address: %p\n", dataArray);
    if (TRACE)
    for (i=0; i  < TOTALSIZE; i++) {
      *(dataArray + i) = i;
      if (TRACE)
	printf("Initializing dataArray%p] : %d\n",    dataArray+i, *(dataArray+i));
    }
}


void   initializeCache() {
// Initialize all cache blocks
    int i,j,k;

    // Set all cache blocks  to the Invalid State
      for (i=0; i < NUM_PROCESSORS; i++) {
	for (j=0; j < CACHESIZE; j++) {
	  CACHE[i][j].STATE = I;   
	  for (k=0; k < INTS_PER_BLOCK; k++) {
	    CACHE[i][j].DATA[k] = 0;
	  }
	  if (DEBUG) 
	    displayCacheBlock(i, j);
	}
      }
  
  
  // Initialize Cache Statistics
    for (i=0; i < NUM_PROCESSORS; i++) {
      
      cache_write_hits[i] = 0;
      cache_write_misses[i]= 0;
      cache_upgrades[i] = 0;
      cache_read_hits[i] = 0;
      cache_read_misses[i] = 0;
      cache_writebacks[i] = 0;
    }
  }




void UserMain(int argc, char *argv[])
{
  int i,j, k;
  void Processor(), FrontEndController(), BackEndController(), BusArbiter();

  
  getParams(argc, argv);
  initializeMem();
  initializeCache();
  initializeSemaphores();
  initTraceFiles(MODE);  // CHUNKED or INTERLEAVED


  // Create a Memory Request Queue for each processor
  for (i=0; i < NUM_PROCESSORS; i++) 
    makeQueue(LS_QUEUE+i);
  
  // Create a Front End Cache  Controller  for each processor */
  for (i=0; i < NUM_PROCESSORS; i++) {
    memcntrl = NewProcess("memcntrl",FrontEndController,0);
    ActivitySetArg(memcntrl,NULL,i);
    ActivitySchedTime(memcntrl,0.00000,INDEPENDENT);
  }
  printf("Done Creating FrontEnd Controllers \n");
  

// Create a Bus Snooper for each processor
  for (i=0; i < NUM_PROCESSORS; i++) {
    bussnooper = NewProcess("bussnooper",BackEndController,0);
    ActivitySetArg(bussnooper,NULL,i);
    ActivitySchedTime(bussnooper,0.000005,INDEPENDENT);
  }
  printf("Done Creating BackEnd Controllers\n");


  // Create a Bus Arbiter
  buscntrl = NewProcess("buscntrl",BusArbiter,0);
  ActivitySetArg(buscntrl,NULL,1);
  ActivitySchedTime(buscntrl,0.00000,INDEPENDENT);
  printf("Done Creating Bus Arbiter  Process\n");
  
  
// Create a process to model activities of  each processor 
  for (i=0; i < NUM_PROCESSORS; i++){
    proccntrl = NewProcess("proccntrl",Processor,0);
    ActivitySetArg(proccntrl,NULL,i);
    ActivitySchedTime(proccntrl,0.00000,INDEPENDENT);
  }
  printf("Done Creating Processors\n");
    
  // Initialization is done, now start the simulation
  DriverRun(MAX_SIMULATION_TIME); // Maximum time of the simulation (in cycles). 

  printf("Simulation ended without completing the trace  at %5.2f\n",GetSimTime());
}



// Processor model
void Processor()
{
  struct tracerecord  *traceref = malloc(sizeof(struct tracerecord));
  struct genericQueueEntry   *memreq;
  int proc_num;

  proc_num = ActivityArgSize(ME) ;
  if (TRACE)
    printf("Processor[%d]: Activated at time %5.2f\n", proc_num, GetSimTime());
  
  while(1) {	  
    if (getNextRef(fp[proc_num], traceref) == 0) 
      break;  // Get next trace record; quit if done
  
    // Create a memory request and  insert into LS_QUEUE for this processor
    memreq = malloc(sizeof(struct genericQueueEntry));  
    memreq->address = traceref->address; 
    memreq->type = traceref->type;
    memreq->delay = traceref->delay;
    memreq->data = traceref->data;
    insertQueue(LS_QUEUE + proc_num, memreq);
       
       if (TRACE) {
	 printf("\nProcessor %d makes %s request  ", proc_num, h(memreq->type));
	 if (memreq->type == STORE)
	   printf("Addr: %x Write Data: %d Time: %5.2f\n", memreq->address, memreq->data, GetSimTime());
	 else
	   printf("Addr: %x Time:%5.2f\n", memreq->address, GetSimTime());
       }

       SemaphoreSignal(sem_memreq[proc_num]);  // Notify memory controller of request
       SemaphoreWait(sem_memdone[proc_num]);   // Wait for request completion 
       ProcessDelay((double) traceref->delay);  //  Delay between requests 
  }

  numThreadsCompleted++;
  if (numThreadsCompleted == NUM_PROCESSORS)
    cleanUp();

}


