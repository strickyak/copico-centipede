SHELF="$(cd ../.. && echo $PWD)"
mkdir -p build 

export PICO_EXAMPLES_PATH=/dev/null
export PICO_SDK_PATH=$SHELF/pico-sdk
export PICOTOOL_FETCH_FROM_GIT_PATH=$SHELF/picotool/

set -ex
cd build
cmake ..
make
pwd
ls -tl
