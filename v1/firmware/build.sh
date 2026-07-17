#!/bin/bash

SHELF="$(cd ../../.. && echo $PWD)"
mkdir -p build 
cd build

export PICO_EXAMPLES_PATH=/dev/null
export PICO_SDK_PATH=$SHELF/pico-sdk
export PICOTOOL_FETCH_FROM_GIT_PATH=$SHELF/picotool/

set -ex
cmake ..
make
ls -l *.uf2
