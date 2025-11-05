#include "global.h"
#include <string.h>

extern struct cacheblock CACHE[][CACHESIZE];
extern int MEM[];
extern int dataArray;

extern int NUM_PROCESSORS;
extern int MAX_DELAY;
extern int MODE;
extern int TRACE;
extern int BUS_RELEASE;

void getParams(int argc, char *argv[]) {
  int i;


  for (i=1; i < argc ; i = i+2) {
     if (strcmp(argv[i], "--numProcs") == 0) {
       NUM_PROCESSORS  = atoi(argv[i+1]);
     }
     else  if (strcmp(argv[i], "--cpuDelay") == 0){
       MAX_DELAY  = atoi(argv[i+1]);
     }
     else  if (strcmp(argv[i], "--mode") == 0) {
       if (strcmp(argv[i+1], "CHUNK") == 0)
	 MODE = CHUNK;
       else 	if (strcmp(argv[i+1], "INTERLEAVE") == 0)
	 MODE  = INTERLEAVE;
       else {
	 printf("Unsupported Data Placement mode\n");
	 exit(1);
       }
    }
     else  if (strcmp(argv[i], "--trace") == 0){
       TRACE  = atoi(argv[i+1]);
     }
     else {
       printf("Unmatched Argument %s BYE!!!\n", argv[i]);
       exit(1);
     }
  }
  BUS_RELEASE = TRUE;  // No bus owner at start
  printf("**************************************************************************\n");
  printf("NUM_PROCESSORS: %d  CPU_DELAY: %d  MEM_DELAY: %d  MODE: %s  NUM_ELEMENTS: %d\n", NUM_PROCESSORS, MAX_DELAY, ((int) MEM_CYCLE_TIME), (MODE == CHUNK) ? "CHUNK" : "INTERLEAVE", TOTALSIZE);
  printf("***************************************************************************\n");
}



char *f(int state){
    switch (state) {
        case 1:  return("S");
        case 2:  return("M");
        case 3:return("I");
        case 4:return("IS");
        case 5:return("IM");
        case 6:return("SM");
    }
}

char * g(int reqtype) {
    switch (reqtype) {
        case 1: return("BUS_RD"); break;
        case 2: return("BUS_RDX"); break;
        case 3: return("INV"); break;
            
    }
}

char *h(int type){
  if (type == 0) return ("WRITE");
  else return("READ");
}

void displayCacheBlock(int processor, int blkNum) {
  int i;

  printf("CACHE Processor: %d  Time: %5.2f\n", processor, GetSimTime());
  for (i=0; i < INTS_PER_BLOCK; i++) 
    printf("CACHE[%d][%d]: Word[%d]: %d\n", processor, blkNum, i, CACHE[processor][blkNum].DATA[i]);
  printf("********************************\n");
}



int Qallocated[MAX_NUM_QUEUES];
int NumInQueue[MAX_NUM_QUEUES];



int Qallocated[MAX_NUM_QUEUES] = {0};
int NumInQueue[MAX_NUM_QUEUES] = {0};
struct queueNode * Qhead[MAX_NUM_QUEUES], * Qtail[MAX_NUM_QUEUES];



double getServiceTime(struct genericQueueEntry *req, double MINSERVICETIME, double MAXSERVICETIME) {
  double interval;
  double time;
  
  interval =  MAXSERVICETIME - MINSERVICETIME;
  time = (drand48() * interval)  + (double) MINSERVICETIME;
  return time;
}



int  getNextRef(FILE *fp, struct tracerecord *ref) {
 

  if (fread(ref, sizeof(struct tracerecord), 1,fp)  == 1) 
    return(TRUE);
    else 
      return(FALSE); // End of Tracefile fp
  }
  
  void makeQueue(int id) {
    if (id > MAX_NUM_QUEUES) {
      printf("Too many open queues. Upgrade to professional version. Charges may appy!\n");
      exit(1);
    }
    
    if (Qallocated[id] == TRUE) {
      printf("Trying to allocate a duplicate queue with same id: %d\n", id);
      exit(1);
    }
    Qallocated[id] = TRUE;
    Qhead[id] = Qtail[id] = (struct queueNode *) NULL;
    NumInQueue[id] = 0;
  }
  
  
  void insertQueue(int id, struct genericQueueEntry *data) {
    struct  queueNode * ptr;

    if (Qallocated[id] == FALSE) 
    exit(1);
    
    ptr = (struct queueNode *) malloc(sizeof(struct queueNode));
    ptr->next = NULL;
    ptr->data = (void *) data;
    NumInQueue[id]++;
    
    if (Qhead[id] == NULL) 
    Qhead[id] = ptr;
    else 
      Qtail[id]->next = ptr;
    
    Qtail[id]  = ptr;    
  }
  
  struct genericQueueEntry *  getFromQueue(int id) {
    struct genericQueueEntry *  ptr;
    struct  queueNode * temp;
    
    
    if (Qallocated[id] == FALSE) {
      printf("Accessing  non-existent queue %d\n", id);
      exit(1);
    }
    if (Qhead[id] == NULL) {
      printf("Accessing empty queue %d\n", id);
    exit(1);
    }
    
    temp = Qhead[id];
    Qhead[id] = Qhead[id]-> next;
    ptr = temp->data;
    free(temp);
    NumInQueue[id]--;
  return ptr;
  }
  
  
  struct genericQueueEntry *  pokeQueue(int id) { 
    struct genericQueueEntry *  ptr;
    
  if (Qallocated[id] == FALSE) {
    printf("Accessing  non-existent queue %d\n", id);
    exit(1);
  }
  if (Qhead[id] == NULL) {
    printf("Accessing empty queue %d\n", id);
    exit(1);
  }

  ptr  = Qhead[id]-> data;
  if (DEBUG)
    printf("Returning  request for address %x from queue %d\n", ptr->address,  id);
  return ptr;
}


int getSizeOfQueue(int id){
  if (Qallocated[id] == FALSE) {
    printf("Getting size of non-existent queue %d\n", id);
    exit(1);
  }
  if (NumInQueue[id] < 0) {
    printf("Why did my queue get negative?\n");
    exit(1);
  }
   return(NumInQueue[id]);
}



void displayQueue(int id) {
  struct queueNode * ptr;
  ptr = Qhead[id];
  printf("*************************************\n");
  while (ptr != NULL) {
    printf("ADDRESS: %x\n", (ptr->data) -> address);
    ptr = ptr->next;
  }
  printf("*************************************\n");
}


struct  queueNode *  getLastMatchingEntry(struct queueNode * ptr, int address){
  struct queueNode *temp;

 if (ptr == NULL)
   return NULL;
 else {
   temp = getLastMatchingEntry(ptr->next, address);
if (temp != NULL)
     return temp;
   else
     if ( (ptr->data)->address  == address)
       return ptr ;
     else 
       return NULL;
 }
}






struct queueNode * getLastEntry(int id, int address) {
  struct queueNode * ptr;
   ptr = Qhead[id];
  return( getLastMatchingEntry(ptr, address));
}




