#include "global.h"

extern struct cacheblock CACHE[][CACHESIZE];  // Cache

// Cache Statistics
extern int cache_write_hits[], cache_write_misses[];
extern int cache_upgrades[],cache_read_hits[], cache_read_misses[];
extern int cache_writebacks[];
extern int TRACE;
extern int NUM_PROCESSORS;

extern struct busrec BROADCAST_CMD;   // Bus Commands
extern int BUS_REQUEST[], BUS_GRANT[], BUS_RELEASE; // Bus Handshake Signals

extern SEMAPHORE *sem_bussnoop[]; // Signal each snooper that a bus command is being broadcast
extern SEMAPHORE *sem_bussnoopdone[]; // Signal that command has been handled at this snooper 

extern int numUtoX[];
extern int f(int), g(int);
extern void displayCacheBlock(int, int);


void atomicUpdate(int procId, int blkNum, int state) {
  CACHE[procId][blkNum].STATE = state;
}

void writebackMemory(int address, int * blkDataPtr) {  // Copy cache block to memory
  int i;
 int  *memptr = (int *) (long int) address;
  
  for (i=0; i < INTS_PER_BLOCK; i++){
    *memptr++  =  blkDataPtr[i];  
    if (DEBUG)
       printf("After WriteBack: dataArray[%p]  Value:  %d\n", (int *) (long int) address + i , *(  ( (int *) ( (long int) address)) + i) );
    }
  
}

void readFromMemory(int address, int *blkDataPtr) {  // Copy memory block to cache 
  int i;
  int *memptr = (int *) (long) address;
  for (i=0; i < INTS_PER_BLOCK; i++){
     blkDataPtr[i] = *memptr;
     if (DEBUG)
       printf("Reading dataArray[%p]  copied to blkData[%d]: %d\n", memptr, i, blkDataPtr[i]);
     memptr++;
  }
}


/* *********************************************************************************************
BusSnooper process for each processor/cache. Woken  with broadcast command (in struct BROADCAST_CMD).
********************************************************************************************* */

void BackEndController() {
  int blkNum, broadcast_tag;
  unsigned  busreq_type;
  unsigned address;
  int BLKMASK = (-1 << BLKSIZE);
  int procId;
  struct genericQueueEntry writeback;

  procId = ActivityArgSize(ME); // Id of this BackEndController (Snooper)
  if (TRACE)
    printf("BackEndController[%d]: Activated at time %5.2f\n", procId, GetSimTime());
 

   printf("Scottie, Beef Me up! Im a skeleton Snooper and will simply terminate when the simulation time is reached.\n\n");

  while (1) {

    SemaphoreWait(sem_bussnoop[procId]); // Wait for a bus command
      if (BUSTRACE)
      printf("BusSnooper[%d] --  Woken with Bus COMMAND  at time %5.2f\n", procId, GetSimTime());

    /* Parse  Bus Command and extract relevant fields
       Future actions depend on whether this command originated from my own Front End or from another processor. */
   
    /*  ************************************************************************************ */

      /* Handle Bus Commands from my Front End. 

	 1.  Before handling the cache miss, I may need to write back the cache block being evicted.
	 If so, use the provided function "writebackMemory()" to write the block back to memory.
	 Simulate the time for the writeback by calling "ProcessDelay(MEM_CYCLE_TIME)". 
	 Increment my statistics counter "cache_writebacks[ ]".

	 2. Simulate the memory access time for a read by calling "ProcessDelay(MEM_CYCLE_TIME)".  
	 To read the values of the  block from memory use the function "readFromMemory()"; 

	 3. After the missed block is read into cache the tag and state needs to be updated.

	 4. In the  case that the cache block that we are reading is actually being flushed by some other cache (that has the 
	 block in the M state) we should make sure that before the "readFromMemory()" of step 2, the memory has been updated 
	 by the other cache. Delaying the "readFromMemory()" by epsilon using the ProcessDelay() function shown, will ensure the
	 race id handled corecttly.
	 Note we  delay for only one  MEM_CYCLE_TIME; the delays by the writer process and the reader process overlap.

	 5. After handling  the command the snooper should delay by one cycle using "ProcessDelay(CLOCK_CYCLE)" before 
	 it signals  "sem_busnoopdone[ ]" and returning. This simulates the time required for the Bus Snoopers  execution.
  
	 ************************************************************************************ */

    busreq_type = BROADCAST_CMD.reqtype;
    address = BROADCAST_CMD.address;
    broadcast_tag = (address >> (BLKSIZE + CACHEBITS)) / CACHESIZE;
    blkNum = (address >> BLKSIZE) % CACHESIZE;


    if (BUS_GRANT[procId] == TRUE)  { // My own Front End  initiated this request


      BUS_GRANT[procId] = FALSE;

      switch (busreq_type) {  
   
        case (INV): {
            if (CACHE[procId][blkNum].STATE == SM) {
                // Upgrade privilege
                CACHE[procId][blkNum].STATE = M;
            }
            break;
        }

        case (BUS_RD): {

            if (CACHE[procId][blkNum].STATE == M) {
                // cache miss and writeback
                int addr = (int)( (CACHE[procId][blkNum].TAG << (BLKSIZE + CACHEBITS)) | (blkNum << BLKSIZE) );
                writebackMemory(addr & BLKMASK, CACHE[procId][blkNum].DATA);
                ProcessDelay(MEM_CYCLE_TIME);
                cache_writebacks[procId]++;
            }
            ProcessDelay(epsilon);
            readFromMemory(address & BLKMASK, CACHE[procId][blkNum].DATA);
            ProcessDelay(MEM_CYCLE_TIME);
            CACHE[procId][blkNum].STATE = S;
            CACHE[procId][blkNum].TAG = broadcast_tag;
            break;
        }

        case (BUS_RDX): {

            if (CACHE[procId][blkNum].STATE == M) {
                // cache miss and writeback
                int addr = (int)( (CACHE[procId][blkNum].TAG << (BLKSIZE + CACHEBITS)) | (blkNum << BLKSIZE) );
                writebackMemory(addr & BLKMASK, CACHE[procId][blkNum].DATA);
                ProcessDelay(MEM_CYCLE_TIME);
                cache_writebacks[procId]++;
            }
            ProcessDelay(epsilon);
            readFromMemory(address & BLKMASK, CACHE[procId][blkNum].DATA);
            ProcessDelay(MEM_CYCLE_TIME);
            CACHE[procId][blkNum].STATE = M;
            CACHE[procId][blkNum].TAG = broadcast_tag;
            break;
        }
      }
      
      ProcessDelay(CLOCK_CYCLE);

      SemaphoreSignal(sem_bussnoopdone[procId]);  //  Uncomment Signal for actual code
      continue;
    }
    
  
    /*  ************************************************************************************ */
    
    
    /*   Handle  Bus Command  from another processor
	 
	 1.  Ignore command if the block is INVALID in my cache.
	 
	 2.  To copyback a requested cache block use the provided function "writebackMemory()".
	 After copying to memory simulate the writeback time by calling "ProcessDelay(MEM_CYCLE_TIME)".       
	 
	 3. After handling the command, delay by one cycle using "ProcessDelay(CLOCK_CYCLE)" before performing the signal 
	 on "sem_bussnoopdone[ ]" and returning. This simulates the time required for this execution.

	 3. If you receive an INV or BUS_RDX signal from some other processor when in the transient state SM,
	 increment my "numUtoX[]"counter by 1. Invalidate your state so the correct bus command is broadcast.
	 
	 ************************************************************************************ */
    
    // Check for valid block in the cache; else ignore command and continue.

    // printf("backend\n");
    if (CACHE[procId][blkNum].TAG != broadcast_tag || CACHE[procId][blkNum].STATE == I) {
        // printf("not matching tag\n");
        SemaphoreSignal(sem_bussnoopdone[procId]);   // Uncomment Signal for actual code
        continue;
    }

    switch (busreq_type) { // Handling command from other processor for a valid block in my cache

    
        case (INV): { // should never happen if block is in modified state
            // set block state to invalid locally 
            if (CACHE[procId][blkNum].STATE == SM) {
                numUtoX[procId]++;
            }
            CACHE[procId][blkNum].STATE = I;
            break;
        }
        case (BUS_RD): {
            // another processor wants to read this block
            if (CACHE[procId][blkNum].STATE == M) {
                // writeback to memory
                CACHE[procId][blkNum].STATE = S;
                int addr = (int)( (CACHE[procId][blkNum].TAG << (BLKSIZE + CACHEBITS)) | (blkNum << BLKSIZE) );
                writebackMemory(addr & BLKMASK, CACHE[procId][blkNum].DATA);
                ProcessDelay(MEM_CYCLE_TIME);
            }
            break;
        }  
        case (BUS_RDX): {
            // another processor wants to write this block
            if (CACHE[procId][blkNum].STATE == M) {
                // writeback to memory and invalidate
                CACHE[procId][blkNum].STATE = I;
                int addr = (int)( (CACHE[procId][blkNum].TAG << (BLKSIZE + CACHEBITS)) | (blkNum << BLKSIZE) );
                writebackMemory(addr & BLKMASK, CACHE[procId][blkNum].DATA);
                ProcessDelay(MEM_CYCLE_TIME);
            } else if (CACHE[procId][blkNum].STATE == SM) {
                numUtoX[procId]++;
                CACHE[procId][blkNum].STATE = I;
            } else if (CACHE[procId][blkNum].STATE == S) {
                CACHE[procId][blkNum].STATE = I;
            }
            break;
        }
    }

    ProcessDelay(CLOCK_CYCLE);   
    SemaphoreSignal(sem_bussnoopdone[procId]);   // Uncomment Signal for actual code
  }
}

/* *********************************************************************************************
Bus Arbiter checks for an asserted  BUS_REQUEST signal every clock cycle.
*********************************************************************************************/
void BusArbiter()
{
  int job_num, i;
static int procId = 0;

job_num = ActivityArgSize(ME) - 1;

 if (TRACE)
   printf("Bus Arbiter Activated at time %3.0f\n", GetSimTime());
 
 while(1){

       ProcessDelay(CLOCK_CYCLE);
       if (BUS_RELEASE == FALSE)   // Current bus-owner will release the bus when done
	 continue;


     for (i=0; i < NUM_PROCESSORS; i++)
       if (BUS_REQUEST[(i + procId)%NUM_PROCESSORS]) 
	 break;

     if (i == NUM_PROCESSORS) 
       continue;
     
     BUS_RELEASE = FALSE;     
     BUS_GRANT[(i+procId)%NUM_PROCESSORS] = TRUE;
     
       if (TRACE)
	 printf("BUS GRANT: Time %5.2f.  BUS_GRANT[%d]: %s\n", GetSimTime(), procId, BUS_GRANT[procId] ? "TRUE" : "FALSE");

     procId = (procId+1) % NUM_PROCESSORS;
    
 }
}

