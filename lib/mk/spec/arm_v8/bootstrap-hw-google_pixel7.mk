REQUIRES = hw

REP_INC_DIR += src/bootstrap/board/google_pixel7

SRC_CC  += bootstrap/spec/arm/gicv3.cc
SRC_CC  += bootstrap/spec/arm_64/cortex_a53_mmu.cc
SRC_CC  += bootstrap/board/google_pixel7/platform.cc
SRC_S   += bootstrap/board/google_pixel7/handoff.s

vpath bootstrap/board/google_pixel7/% $(REP_DIR)/src

include $(call select_from_repositories,lib/mk/spec/arm_v8/bootstrap-hw.inc)
