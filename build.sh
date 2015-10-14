#!/bin/bash
gcc src/main.cpp -g -o bin/hmdd -lX11 -ldwarf -lelf
gcc src/hello.cpp -g -o test/hello
