#!/bin/bash

mkdir -p build
cd build || exit 1
rm -rf *
cmake ../
make -j4

