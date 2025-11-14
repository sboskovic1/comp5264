#!/bin/bash
clear
gcc *.c ./yacsim.o -lm -o main
./main --mode INTERLEAVE --cpuDelay 360 --numProcs 32 --trace 0