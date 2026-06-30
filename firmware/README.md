As of June 2026, this is the official software for the Copico Centipede:

    https://github.com/strickyak/copico-centipede/ , firmware

=========================================================

Hints:

mkdir -p ~/tmp/firmware

cd  ~/tmp/firmware

cmake ~/coco-shelf/copico-centipede/firmware

PICO_EXAMPLES_PATH=/dev/null PICO_SDK_PATH=/home/strick/modoc/coco-shelf/pico-sdk PICOTOOL_FETCH_FROM_GIT_PATH=/home/strick/modoc/coco-shelf/picotool/ make  && cp -v centipede.uf2 /media/strick/RP2350
