TARGET   = gs201_platform
REQUIRES = arm_v8

include $(call select_from_repositories,src/driver/platform/target.inc)

# cpufreq/*.h
INC_DIR += $(REP_DIR)/include
