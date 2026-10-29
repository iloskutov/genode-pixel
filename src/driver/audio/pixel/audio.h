/*
 * \brief  Interface between the sound-card task (card.c) and the Genode side
 *         (audio.cc): sessions, configuration, mixer report
 * \author Ivan Loskutov
 * \date   2026-10-01
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__AUDIO_H_
#define _PIXEL_AUDIO__AUDIO_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sample format between the sessions and the session-backed PCMs: 48 kHz,
 * two channels, signed 16 bit, interleaved, 10 ms per period.
 */
enum {
	PIXEL_AUDIO_RATE          = 48000,
	PIXEL_AUDIO_CHANNELS      = 2,
	PIXEL_AUDIO_PERIOD_FRAMES = 480,
};


/**************
 ** Sessions **
 **************/

/**
 * Open the Record sessions "left" and "right" (playback data)
 *
 * \return 0 on success. May be called repeatedly.
 */
int pixel_audio_playback_sessions(void);

/**
 * Record one period from the Record sessions
 *
 * \return PIXEL_AUDIO_PERIOD_FRAMES interleaved stereo frames, or NULL if
 *         the sessions are depleted (nobody plays). After a depletion the
 *         card task gets PIXEL_AUDIO_EVENT_WAKEUP once data is available
 *         again.
 */
short const *pixel_audio_record_period(void);

/**
 * Open the Play sessions "mic_left" and "mic_right" (captured data)
 */
int pixel_audio_capture_sessions(void);

/**
 * Hand one captured period (PIXEL_AUDIO_PERIOD_FRAMES interleaved stereo
 * frames) to the Play sessions
 */
void pixel_audio_play_period(short const *frames);

/**
 * Forget the time window of the Play sessions (capture was stopped)
 */
void pixel_audio_play_stop(void);


/*******************
 ** Configuration **
 *******************/

/*
 * The 'config' ROM is the mixer policy (README, "Configuration"). It is
 * handed to the card task as one ordered list.
 */

/* a 'default' or 'control' node */
struct pixel_audio_write
{
	char const *name;    /* mixer control */
	int         index;   /* element of the control, < 0: the whole control */
	char const *value;   /* integer(s) or enum item, as text */
};

enum pixel_audio_action_type {
	PIXEL_AUDIO_ACTION_CONTROL,  /* 'control': write            */
	PIXEL_AUDIO_ACTION_STREAM,   /* 'stream': pcm, capture      */
	PIXEL_AUDIO_ACTION_TUNING,   /* 'tuning': block .. value    */
};

struct pixel_audio_action
{
	enum pixel_audio_action_type type;

	struct pixel_audio_write write;

	unsigned pcm;
	int      capture;

	unsigned block, component, key, value;
};

struct pixel_audio_plan
{
	char const *route;                    /* attribute 'route', "" if absent */

	unsigned                         num_defaults;
	struct pixel_audio_write const  *defaults;

	unsigned                         num_actions;
	struct pixel_audio_action const *actions;

	int playback_pcm;                     /* < 0: no enabled 'playback' node */
	int capture_pcm;                      /* < 0: no enabled 'capture' node  */
};

/**
 * Parse the current 'config' ROM
 *
 * Nodes of an unknown type and 'missing' nodes are logged, not returned.
 * The plan stays valid until it is released. Returns NULL if memory is
 * exhausted.
 */
struct pixel_audio_plan const *pixel_audio_plan_acquire(void);
void pixel_audio_plan_release(struct pixel_audio_plan const *);


/******************
 ** Mixer report **
 ******************/

struct pixel_audio_ctl_report
{
	unsigned     numid;
	char const  *name;
	char const  *iface;       /* "mixer", "card", "pcm", ... */
	char const  *type;        /* "bool", "int", "int64", "enum", "bytes", "iec958", "none" */
	unsigned     count;       /* number of elements (bytes: size) */
	int          readable, writable;

	/* values read once at enumeration, valid if 'num_values' > 0 */
	unsigned         num_values;
	long long const *values;
	int              read_error;  /* errno of the read, 0 if read or not attempted */

	long long min, max;       /* integer types */

	unsigned            num_items;   /* enum */
	char const * const *items;
};

/**
 * Generate the "mixer" report from the list of all controls
 */
void pixel_audio_report_mixer(char const *card_name,
                              struct pixel_audio_ctl_report const *ctls,
                              unsigned count);


/************************************
 ** Events for the card task (C++) **
 ************************************/

enum pixel_audio_event {
	PIXEL_AUDIO_EVENT_CONFIG = 0,   /* the 'config' ROM changed            */
	PIXEL_AUDIO_EVENT_WAKEUP = 1,   /* the Record sessions have data again */
	PIXEL_AUDIO_EVENT_PLAYBACK = 2, /* period elapsed, playback PCM        */
	PIXEL_AUDIO_EVENT_CAPTURE  = 3, /* period elapsed, capture PCM         */
};

/**
 * Raise an event and unblock the card task (card.c). Callable from the
 * Genode side; the caller runs the Linux scheduler afterwards.
 */
void pixel_audio_card_notify(enum pixel_audio_event event);

#ifdef __cplusplus
}
#endif

#endif /* _PIXEL_AUDIO__AUDIO_H_ */
