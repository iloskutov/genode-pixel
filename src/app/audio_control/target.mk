#
# audio_control: audio policy of the Pixel 7 (call state + UI requests ->
# config of the pixel_audio driver). See README.
#
TARGET  = audio_control
SRC_CC  = main.cc
LIBS   += base audio_policy
