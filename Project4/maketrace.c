#include "global.h"

struct tracerecord tracerec;
extern FILE *fp[];
extern int NUM_PROCESSORS;
extern int  MAX_DELAY;

int numRecords = 0;

extern int *dataArray;

void dataInit(int *p, int size) {
  int i;
  for (i=0; i  < size; i++)
    *p++ = i;
}

 
int  * record(FILE *fp, int *q, int type, int data){
   tracerec.address = (long unsigned) q;
   tracerec.delay= (int) MAX_DELAY;
   tracerec.type = type;
   tracerec.data = data;
   numRecords++;
   fwrite(&tracerec, sizeof(struct tracerecord), 1,fp);
   return q;
}


// Processor i gets an inteleaved partition of the array.
void dointerleave(FILE *fp, int id){
  int i;
  int *p;
  int temp;


  for (i=0, p = dataArray + id; i < TOTALSIZE; i +=  NUM_PROCESSORS){
    temp =    *(record(fp, p,  1, *p));
    temp = temp *temp;
    (record(fp, p,  0, temp));
    p = p+NUM_PROCESSORS;
  }
}


// Processor i gets the ith chunk of adjacent array elements.
void dochunk(FILE *fp, int id){
  int i;
  int *p;
  int chunksize = TOTALSIZE/NUM_PROCESSORS;
  int temp;

  for (i=0,  p = dataArray + id*chunksize; i < chunksize; i++)    {
    temp =    *(record(fp, p,  1, *p));
    temp = temp * temp;
    record(fp, p,  0, temp);
    p++;
  }
}



int initTraceFiles(int mode) {
  int i;
  char filename[20];
  
  
  for (i=0; i < NUM_PROCESSORS; i++) {
    sprintf(filename, "memtrace%d\0",i);
    fp[i] = fopen(filename,"w+");
    numRecords = 0;
    
    if (mode == CHUNK)
      dochunk(fp[i],i);
    else 
      dointerleave(fp[i],i);
    
    fclose(fp[i]);
    fp[i] = fopen(filename,"r");
    printf("Created Trace File \"%s\"  with %d records\n", filename,numRecords);
  }
}

