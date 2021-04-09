#!/bin/sh

HOME_PATH=/home/ubuntu/weatherstation

python3 $HOME_PATH/futaba.py -u /dev/ttyUSB0 > $HOME_PATH/futaba.log 2>$HOME_PATH/futaba.err
