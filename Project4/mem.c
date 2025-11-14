#include "global.h"

extern struct busrec BROADCAST_CMD;   // Bus Commands
extern int BUS_REQUEST[], BUS_GRANT[], BUS_RELEASE; // Bus Handshake Signals

extern SEMAPHORE *sem_bussnoop[]; // Signal each BusSnooper that a bus command is being broadcast
extern SEMAPHORE *sem_bussnoopdone[]; // Signal that command has been handled at this BusSnooper 

extern int NUM_PROCESSORS;

// Cache Statistics
extern int cache_write_hits[], cache_write_misses[];
extern int cache_read_hits[], cache_read_misses[];
extern int cache_upgrades[], cache_writebacks[];

extern struct cacheblock CACHE[][CACHESIZE];  // Cache
extern  int MakeBusRequest(int, struct genericQueueEntry *);  
extern int getSizeOfQueue(int);
extern int TRACE;

extern  struct genericQueueEntry  * getFromQueue(int);
extern void displayCacheBlock(int, int);
extern char* g();
extern char* h();
extern char* f();

// Synchronization Variables
extern SEMAPHORE *sem_memreq[], *sem_memdone[];

/* *********************************************************************************************
 Called by  HandleCacheMiss() and LookupCache() to create a bus transaction.
 It waits till granted bus access by the  Bus Arbiter, then broadcasts the command, and waits till 
 all snoopers acknowledge completion. 
 ********************************************************************************************* */

int  MakeBusRequest(int procId, struct genericQueueEntry * req){       
  int blkNum,  op;
  int i;

  BUS_REQUEST[procId] = TRUE;   // Assert BUS_REQUEST signal
  if (TRACE)
    printf("MakeBusRequest: Proc %d asserts  BUS REQUEST Time %5.2f\n", procId, GetSimTime());

  while (BUS_GRANT[procId] == FALSE)  // Wait for BUS_GRANT
    ProcessDelay(CLOCK_CYCLE);       // Checking for BUS_GRANT wvery cycle (mem.c)

  BUS_REQUEST[procId] = FALSE;   // De-assert request

  // Create Bus Command based on type of request and current state
  blkNum = (req->address >> BLKSIZE) % CACHESIZE;
  op = req->type;

  BROADCAST_CMD.address  =  req->address;
  if  (CACHE[procId][blkNum].STATE == SM)
    BROADCAST_CMD.reqtype  = INV;
  else  if  (op == LOAD)
    BROADCAST_CMD.reqtype  = BUS_RD;
  else 
    BROADCAST_CMD.reqtype  = BUS_RDX;
  
  if (TRACE)
    printf("MakeBusRequest: Proc %d BUS GRANT. Broadcast %s Addr: %d Time %5.2f\n", procId, g(BROADCAST_CMD.reqtype), BROADCAST_CMD.address, GetSimTime());
   
  for (i=0; i < NUM_PROCESSORS; i++) 
    SemaphoreSignal(sem_bussnoop[i]);  // Wake up each BusSnooper 
  
  for (i=0; i < NUM_PROCESSORS; i++)
    SemaphoreWait(sem_bussnoopdone[i]);  // Wait for all Bus Snoopers to complete
}

/* Release the bus for use by another processor */
void releaseBus(int procId) {  
  BUS_GRANT[procId] = FALSE;
  BUS_RELEASE = TRUE;
  ProcessDelay(epsilon); // Synchronization Delay after BUS_RELEASE (mem.c)
}

// HandleCacheMiss: Make appropriate Bus Request and wait for completion. Update statistics.
int HandleCacheMiss(int procId, struct genericQueueEntry *req) {
  unsigned blkNum, myTag, op;
  
  // Parse the request
  blkNum = (req->address >> BLKSIZE) % CACHESIZE;
  myTag = (req->address >> BLKSIZE) / CACHESIZE;
  op = req->type;   // LOAD or STORE

  if ((CACHE[procId][blkNum].STATE != I) && (CACHE[procId][blkNum].TAG == myTag)) { // Cache HIT: ERROR
    if (TRACE)
      printf("HandleCacheMiss: Proc %d Cache HIT Addr: %x Time: %5.2f\n", procId, req->address, GetSimTime());
    exit(1);
  }

  if (TRACE)
    printf("HandleCacheMiss: Proc %d Cache MISS Addr: %x Time: %5.2f\n", procId,req->address, GetSimTime());
  
  switch (op) {
  case  LOAD:
    cache_read_misses[procId]++;  // Read Miss  Counter
    MakeBusRequest(procId, req);  // Reads block into cache
    break;
    
  case STORE:
    cache_write_misses[procId]++;  // Write Miss Counter
    MakeBusRequest(procId, req);  // Reads block into cache
    break;
  }
  
  releaseBus(procId);  // Release bus
  
  if (TRACE)
    printf("HandleCacheMiss:Proc %d MISS handling completed Addr: %x Time: %5.2f\n", procId, req->address, GetSimTime());
}

/* ******************************************************************************************
Checks cache for requested word. Returns FALSE on a cache miss.
On cache HIT: a silent transaction is applied to the cache and incurs 1 CLOCK_CYCLE delay, while
a hit requiring a bus transaction is handled synchronously before returning.
/* ***************************************************************************************/

int LookupCache(int procId, struct genericQueueEntry *req) {
  unsigned blkNum, myTag;
  unsigned op;
  int wordOffset;
  char *printString;

  //  Parse the memory request address  
  blkNum = (req->address >> BLKSIZE) % CACHESIZE;
  myTag = (req->address >> BLKSIZE) / CACHESIZE;
  wordOffset = (req->address % (0x1 << (BLKSIZE))) / sizeof(int);
  op = req->type;   // LOAD or STORE

  if ((CACHE[procId][blkNum].TAG == myTag) &&  (CACHE[procId][blkNum].STATE != I)) {   // Cache Hit
    if (TRACE)
      printf("LookupCache: Proc %d Cache Hit Addr: %x Time: %5.2f\n", procId,req->address,GetSimTime());

    
  switch (CACHE[procId][blkNum].STATE) {
    case M: { // Silent Transaction
        switch(op) {
        case LOAD: 
        cache_read_hits[procId]++;  // Read Hits Counter
        req->data =  CACHE[procId][blkNum].DATA[wordOffset];// Read requested word from cache
        break;
        
        case STORE: 
        cache_write_hits[procId]++;  // Write Hits Counter      
        CACHE[procId][blkNum].DATA[wordOffset] =  req->data;  // Write word to cache 
        if (DEBUG)
        displayCacheBlock(procId, blkNum);
        break;
        }
        ProcessDelay(CLOCK_CYCLE);  // Fixed delay to process Cache Hit in M state (mem.c)
        break;
    }
        
    case S: { 
        switch(op) {
        case LOAD:                                    
        cache_read_hits[procId]++; // Read Hits Counter
        req->data =  CACHE[procId][blkNum].DATA[wordOffset];  // Read reequested word from cache
        ProcessDelay(CLOCK_CYCLE);  // Delay to process Cache Hit in S state (mem.c)
        break;
        
        case STORE:
        if (TRACE)
        printf("LookupCache: Proc %d  UPGRADE  addr %x at %5.2f\n", procId,req->address,GetSimTime());
        cache_upgrades[procId]++;   // UPGRADE counter	
        CACHE[procId][blkNum].STATE = SM; // Transient state
        
        MakeBusRequest(procId, req); // Wait for handling of INV request
        if (CACHE[procId][blkNum].STATE != M) { 
        printf("LookupCache: ERROR State after serving INV is not M. Time: %5.2f\n", GetSimTime());
        exit(1);
        };
        
        CACHE[procId][blkNum].DATA[wordOffset] = req->data; // Write the word to cache
        if (TRACE) {
        printf("LookupCache: Updated cache Addr: %x after completing INV. STATE[%d][%d]: %s Time: %5.2f\n", req->address, procId, blkNum, f(CACHE[procId][blkNum].STATE), GetSimTime());
        }
        if (DEBUG)
        displayCacheBlock(procId, blkNum);
        
        releaseBus(procId);  // Safe to allow the next bus transaction
        break;
        }
        break;
    }
  }
  
  return(TRUE);  // Cache Hit serviced 
  }

  else  // Cache Miss
    return(FALSE);
  
}


/* *******************************************************************************************
Woken up by my processor on positive clock edge with a request in my  LS_QUEUE.
Get request from the queue and call LookupCache() to process the request.
LookupCache() returns TRUE on a cache hit and FALSE on a miss.
A write hit requiring an Upgrade is handled completely within LookupCache().
On a cache miss HandleCacheMiss() is invoked to service the miss.
Notify the processor when  the memory request is complete.
*********************************************************************************************/
void FrontEndController() {
  int procId;
  struct genericQueueEntry *req;

  procId = ActivityArgSize(ME) ;
  if (TRACE)
    printf("FrontEndCacheController[%d]: Activated at time %5.2f\n",   procId, GetSimTime());

  while(1) {
    SemaphoreWait(sem_memreq[procId]);  // Wait for memory request 
 
    if (getSizeOfQueue(LS_QUEUE + procId) > 0) // Get request
      req = (struct genericQueueEntry *) getFromQueue(LS_QUEUE + procId);
    else {
      printf("ERROR: No Memory Request To Service\n");
      exit(1);
    }       

    while (LookupCache(procId,req) == FALSE) {
      HandleCacheMiss(procId, req);  // Returns after reading  missed block into cache 
    }
    SemaphoreSignal(sem_memdone[procId]);  // Notify processor of request completion
  }
}




