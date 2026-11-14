#
# Audio policy of the Pixel 7 (platform independent, no libc): call state
# and requests -> config of the pixel_audio driver. Host tests in
# src/lib/audio_policy/test. Used by src/app/audio_control.
#
SRC_CC = policy.cc
vpath %.cc $(REP_DIR)/src/lib/audio_policy
