/*
 * \brief  Linux-side definitions of the pixel_audio driver
 * \author Ivan Loskutov
 * \date   2026-10-01
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__LX_EMUL_H_
#define _PIXEL_AUDIO__LX_EMUL_H_

/* Needed to trace and stop */
#include <lx_emul/debug.h>

/* fix for wait_for_completion_timeout where the __sched include is missing */
#include <linux/sched/debug.h>

/* fix for missing include in linux/dynamic_debug.h */
#include <linux/compiler_attributes.h>

/* fix for missing include in linux/irqdesc.h */
#include <linux/irq.h>

#include <linux/platform_device.h>

#include "pixel_audio.h"


/*
 * Device-tree nodes of the devices that are created by the staged bring-up
 * (lx_user.c) instead of the initial population of the platform bus
 */
#define PIXEL_AUDIO_DT_TRUSTY "/trusty"
#define PIXEL_AUDIO_DT_GSA    "/gsa-ns"
#define PIXEL_AUDIO_DT_MBOX0  "/mbox@176a0000"
#define PIXEL_AUDIO_DT_MBOX1  "/mbox@18320000"
#define PIXEL_AUDIO_DT_MBOX2  "/mbox@18340000"
#define PIXEL_AUDIO_DT_AOC    "/aoc@19000000"
#define PIXEL_AUDIO_DT_SPI7   "/spi@10960000"   /* controller of the amplifiers */
#define PIXEL_AUDIO_DT_SOUND  "/sound-aoc"      /* aoc_alsa machine driver */

/* register range of the GSA S2MPU (s2mpu.c), no driver is bound to the node */
#define PIXEL_AUDIO_DT_S2MPU  "/s2mpu_gsa@17c60000"


/* lx_emul.c */

/**
 * Create the platform device of a held-back device-tree node
 *
 * The matching driver is registered already, so the probe runs before the
 * function returns. Returns NULL if the node is missing or disabled.
 */
struct platform_device *pixel_audio_create_device(char const *path);

/**
 * Set a boolean module parameter of a built-in vendor module by its name
 * without the module prefix. Returns 0 if the parameter exists.
 */
int pixel_audio_set_bool_param(char const *name, bool value);


/* trusty_smc.c */

int           pixel_audio_trusty_poll_start(struct device *trusty_dev);
unsigned long pixel_audio_trusty_poll_count(void);
struct page  *pixel_audio_phys_to_page(phys_addr_t phys);


/* lx_user.c */

/**
 * Name of the bring-up step that is in progress, for log messages of code
 * that can block without a timeout
 */
char const *pixel_audio_current_step(void);
void        pixel_audio_set_step(char const *step);


/* aoc_log.c */

/**
 * Start forwarding the log of the AoC firmware (service "logging") as
 * "pixel_audio: AOC: <line>". To be called once the AoC is online.
 */
void pixel_audio_aoc_log_start(void);

/**
 * Sleep until the log thread has printed what the firmware logged so far
 *
 * The vendor code never sleeps while it waits for the reply of an AoC
 * command, so the log thread runs only where the caller lets it.
 *
 * \param max_lines  return after that many printed lines at the latest
 *                   (each takes about 14 ms)
 * \param settle     sleep one tick first: the firmware logs a command
 *                   shortly after it replied to it
 */
void pixel_audio_aoc_log_drain(unsigned max_lines, bool settle);

/**
 * Quiet phase: only firmware lines of the error level are printed, the
 * others are counted
 */
void pixel_audio_aoc_log_quiet(bool quiet);


/*
 * s2mpu.c: the S2MPU of GSA. Both functions access its registers, which are
 * powered only while the GSA wakelock is held: the caller acquires the
 * wakelock first.
 */

/**
 * Program the S2MPU as pKVM does on resume, with all memory read-write for
 * every VID, and enable it
 *
 * \return 0, or a negative error if the block could not be programmed
 */
int pixel_audio_s2mpu_open(void);

/**
 * Log and acknowledge the faults the S2MPU recorded
 */
void pixel_audio_s2mpu_faults(char const *when);


/* amp.c */

/**
 * Probe SPI7 and the two CS35L41, log the result line of stage "amp"
 *
 * \return number of amplifiers that completed their probe, negative if the
 *         SPI controller did not probe
 */
int pixel_audio_stage_amp(void);


/* card.c: stages of the sound card, each logs its result line */

int pixel_audio_stage_card(void);
int pixel_audio_stage_mixer(void);
int pixel_audio_stage_play(void);

/* '__register_chrdev' of sound/core/sound.c, keeps ALSA's file operations */
struct file_operations;
int pixel_audio_register_chrdev(unsigned int major, unsigned int baseminor,
                                unsigned int count, const char *name,
                                const struct file_operations *fops);

/* uaccess of a kernel that is its own user: plain copies (lx_emul.c) */
unsigned long pixel_audio_copy_to_user(void *to, const void *from,
                                       unsigned long n);
unsigned long pixel_audio_copy_from_user(void *to, const void *from,
                                         unsigned long n);

#endif /* _PIXEL_AUDIO__LX_EMUL_H_ */
