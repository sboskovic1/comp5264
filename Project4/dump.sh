#!/bin/bash
clear
rm core.*
./run.sh
gdb main core*
