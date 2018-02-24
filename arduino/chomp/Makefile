# This makefile uses https://github.com/sudar/Arduino-Makefile
# The paths below assume you have built arduino from source using these
# instructions https://github.com/arduino/Arduino/wiki/Building-Arduino
# Insted of installing the resulting .tar.gz, I just use the path to the built
# artifacts here

ARDUINO_DIR   = ${HOME}/src/Arduino/build/linux/work
ARDMK_DIR     = ${HOME}/src/Arduino-Makefile
BOARD_TAG     = mega
BOARD_SUB     = atmega2560
MONITOR_PORT  = /dev/ttyACM0

CXXFLAGS      = -Wall
CXXFLAGS_STD  = -std=c++11

include ${ARDMK_DIR}/Arduino.mk