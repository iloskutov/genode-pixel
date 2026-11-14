/*
 * \brief  Audio policy of the Pixel 7: call state and requests -> driver config
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Pure logic without Genode sessions and without libc, used by the
 * audio_control component (src/app/audio_control) and by the host tests in
 * src/lib/audio_policy/test.
 *
 * Input: the 'State' (call state, speaker/mute/volume request, codec band,
 * output and recording outside a call), the 'Settings' of the scenario
 * (amplifier mode) and the 'Paths' table
 * (path name -> ordered control writes, generated from the vendor's
 * mixer_paths.xml by tool/mixer_paths_to_hid.py).
 * Output: a 'Config', the ordered list of everything that has to be set in
 * that state. It is always complete, never a difference to an earlier one,
 * and the same input gives the same list.
 *
 * A second, independent output is the volume factor of media playback for
 * record_play_mixer ('media_factor'): AoC has no volume command for media.
 *
 * Routes (the order of the actions is the one of the LineageOS audio HAL):
 *
 * media    no call (unavailable, idle, incoming): DSP mode Ambient, the path
 *          of the output ('speaker' for both loudspeakers, 'handset' for the
 *          earpiece = the top one alone), 'low-latency-playbackP', playback
 *          PCM 1 enabled. With the record request also 'handset-mic',
 *          'audio-recordC', 'handset-mic-post' and the capture PCM 8.
 *          Speaker, mute, volume and band are ignored.
 * handset  call (outbound, alerting, active) without the speaker request:
 *          'voice-handset', 'hostless-ulC spk-vi' with PCM 15,
 *          'voice-handset-mic', DSP mode Telephony, 'sidetone-for handset',
 *          'voice-callC', 'voice-handset-mic-post', 'voice-callP', the
 *          in-call mute controls, the voice PCMs 4 and 11, the call volume;
 *          playback PCM disabled.
 * speaker  call with the speaker request: as handset with 'voice-speaker',
 *          'voice-speaker-mic', 'voice-speaker-mic-post' and no sidetone.
 *
 * In a call the output and the record request are ignored: earpiece or
 * loudspeaker is the speaker request, and the microphone belongs to the
 * call.
 *
 * Every config starts with the initial values of the table ('defaults'),
 * the values a control has while no route sets it. A path the table does
 * not hold is skipped and named in a MISSING action at the end.
 *
 * Amplifier mode ('Settings'): the loudspeaker paths 'speaker' and
 * 'voice-speaker' leave 'PCM Source' at its initial value DSP, they rely on
 * the amplifier firmware that Android's HAL loads once at its start.
 *
 * dsp  every config has, between the defaults and the route, the paths
 *      'cs35l41-load-protection-firmware-start' and '-end' merged into one
 *      list: each control once, with the value it has after both ('DSP
 *      Booted' = 0, 'DSP1 Preload Switch' = 1, both amplifiers). The list
 *      being the same in every config, the driver writes it with the first
 *      config and finds nothing to change later: the firmware is loaded
 *      once and stays loaded.
 * asp  no firmware paths. The routes media and speaker end their paths
 *      with 'AMP PCM Gain' = 'asp_gain' and 'PCM Source' = ASP for both
 *      amplifiers. The routes at the ear (handset call, earpiece output)
 *      take the gain of their path ('voice-handset', 'handset') and get
 *      'PCM Source' = ASP for both amplifiers as well: every config of
 *      this mode names both sources, so the driver never sets one back to
 *      DSP, for which the codec would ask for the firmware an image of
 *      this mode does not have.
 *
 * The earpiece output of media is the vendor's 'handset' path: it selects
 * ASP and its gain for the top amplifier itself (in mode dsp this write
 * follows the firmware paths and counts), and it does not switch the
 * bottom amplifier on, which stays off by its initial value 'R Main AMP
 * Enable Switch' = 0, whatever its source is.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__AUDIO_POLICY__POLICY_H_
#define _INCLUDE__AUDIO_POLICY__POLICY_H_

namespace Audio_policy {

	enum {
		MAX_PATHS   = 256,
		MAX_WRITES  = 2048,
		POOL_SIZE   = 64*1024,

		/* actions of a route that are not writes of the table */
		MAX_FIXED   = 32,
		MAX_ACTIONS = MAX_WRITES + MAX_FIXED,
	};

	/* ALSA PCM devices of the aoc_alsa sound card (fixed by its device tree) */
	enum Pcm : unsigned {
		PCM_PLAYBACK    = 1,    /* low-latency playback, fed by the Record session */
		PCM_VOICE_RX    = 4,    /* voice-call downlink, no data */
		PCM_CAPTURE     = 8,    /* audio record, feeds the Play session */
		PCM_VOICE_TX    = 11,   /* voice-call uplink, no data */
		PCM_HOSTLESS_TX = 15,   /* amplifier feedback (spk-vi), no data */
	};

	/* AoC tuning parameter of the call volume (ABLOCK_MODEM, ASP_ID_FM) */
	enum : unsigned {
		VOLUME_BLOCK = 19, VOLUME_COMPONENT = 3, VOLUME_KEY = 18,
		MAX_VOLUME = 6, DEFAULT_VOLUME = 4,
	};

	/*
	 * 'AMP PCM Gain' of the CS35L41 (google_amplifiers cs35l41/cs35l41.c:
	 * SOC_SINGLE_TLV, 0..0x14 in steps of 1 dB). The default is the value of
	 * the vendor's 'handset' path, which runs the amplifier without its DSP.
	 */
	enum : int { MAX_ASP_GAIN = 20, DEFAULT_ASP_GAIN = 6 };

	/* steps of the media volume, a factor in record_play_mixer */
	enum : int { MAX_MEDIA_VOLUME = 15, DEFAULT_MEDIA_VOLUME = 8 };

	enum class Call  { UNAVAILABLE, IDLE, INCOMING, OUTBOUND, ALERTING, ACTIVE };
	enum class Band  { NB, WB };
	enum class Route { MEDIA, HANDSET, SPEAKER };

	/*
	 * Where media plays outside a call
	 *
	 * SPEAKER   both loudspeakers (the vendor's path 'speaker')
	 * EARPIECE  the top loudspeaker alone, driven as in a call on the
	 *           earpiece (the vendor's path 'handset')
	 */
	enum class Output { SPEAKER, EARPIECE };

	/*
	 * How the loudspeaker routes drive the amplifiers
	 *
	 * DSP  through the amplifier's DSP with the speaker protection, the
	 *      vendor's way ('PCM Source' = DSP is the initial value of the
	 *      table). The firmware has to be loaded for it.
	 * ASP  directly from the serial port, without firmware and without
	 *      protection, at a capped amplifier gain.
	 */
	enum class Amp_mode { DSP, ASP };

	struct State;
	struct Settings;
	struct Write;
	struct Path;
	class  Paths;
	struct Action;
	struct Config;
	class  Names_seen;

	/**
	 * Call state by the name used in the 'call_state' report of modem_link
	 *
	 * An unknown name or a null pointer is UNAVAILABLE.
	 */
	Call call_from_string(char const *);

	/**
	 * Codec band by name ("nb", "wb"), anything else is WB
	 */
	Band band_from_string(char const *);

	/**
	 * Amplifier mode by name ("dsp", "asp"), anything else is DSP
	 */
	Amp_mode amp_mode_from_string(char const *);

	/**
	 * Media output by name ("speaker", "earpiece"), anything else is SPEAKER
	 */
	Output output_from_string(char const *);

	char const *band_name(Band);
	char const *output_name(Output);
	char const *route_name(Route);
	char const *amp_mode_name(Amp_mode);

	Route route(State const &);

	/**
	 * Return the state with everything removed that has no effect on the
	 * config: two states have the same config if their normalized forms
	 * are equal
	 */
	State normalized(State const &);

	/**
	 * Fill 'config' with the complete list of actions for 'state'
	 *
	 * The strings of the actions point into 'paths' and into constants of
	 * the library: the config is valid until 'paths' is modified.
	 */
	void generate(State const &state, Settings const &settings,
	              Paths const &paths, Config &config);

	/**
	 * Volume factor of media playback for a volume step
	 *
	 * \param step  0..MAX_MEDIA_VOLUME, clamped
	 * \param max   factor of the highest step, clamped to 0..1
	 *
	 * The factor is 0 at step 0 and 'max' at the highest step, with a
	 * quadratic curve in between: max * (step/MAX_MEDIA_VOLUME)^2.
	 */
	double media_volume_factor(int step, double max);

	/**
	 * Volume factor of media playback in 'state': 0 during a call (media
	 * is muted), the factor of the volume step otherwise
	 */
	double media_factor(State const &state, int step, double max);
}


struct Audio_policy::State
{
	Call call    = Call::UNAVAILABLE;
	bool speaker = false;
	bool mute    = false;
	int  volume  = DEFAULT_VOLUME;   /* call volume step, clamped to 0..MAX_VOLUME */
	Band band    = Band::WB;

	/* requests that count outside a call only */
	Output output = Output::SPEAKER;   /* where media plays */
	bool   record = false;             /* record with the built-in microphones */

	bool operator == (State const &other) const
	{
		return call == other.call && speaker == other.speaker && mute == other.mute
		    && volume == other.volume && band == other.band
		    && output == other.output && record == other.record;
	}

	bool operator != (State const &other) const { return !(*this == other); }
};


/**
 * Inputs that are fixed for a scenario (config of audio_control)
 */
struct Audio_policy::Settings
{
	Amp_mode amp_mode = Amp_mode::DSP;
	int      asp_gain = DEFAULT_ASP_GAIN;   /* clamped to 0..MAX_ASP_GAIN */
};


/**
 * One control write of a path
 */
struct Audio_policy::Write
{
	char const *control = "";
	char const *value   = "";    /* integer(s) or enum item, as text */
	int         index   = -1;    /* element of the control, < 0: the whole control */
	int         next    = -1;    /* next write of the same path */
};


struct Audio_policy::Path
{
	char const *name  = "";
	unsigned    count = 0;       /* number of writes */
	int         first = -1;
	int         last  = -1;
};


/**
 * Path table: path name -> ordered control writes, plus the initial values
 *
 * Filled section by section: 'begin_path' or 'begin_defaults', followed by
 * the 'add_write' calls of that section.
 */
class Audio_policy::Paths
{
	private:

		char     _pool[POOL_SIZE];
		unsigned _pool_used   = 0;

		Write    _writes[MAX_WRITES];
		unsigned _write_count = 0;

		Path     _paths[MAX_PATHS];
		unsigned _path_count  = 0;

		Path     _defaults   { };
		Path    *_current    = nullptr;
		unsigned _duplicates = 0;
		bool     _overflow   = false;

		char const *_stored(char const *, unsigned len);

		/*
		 * Noncopyable (a copy's 'Write' strings would point into the origin)
		 */
		Paths(Paths const &);
		Paths &operator = (Paths const &);

	public:

		Paths() { clear(); }

		void clear();

		/**
		 * Start a path
		 *
		 * \return false if the name exists already (the first definition
		 *         counts, as in Android's audio_route) or the table is
		 *         full. The following writes are dropped in both cases.
		 */
		bool begin_path(char const *name, unsigned name_len);
		bool begin_path(char const *name);

		/**
		 * Continue with the initial values
		 */
		void begin_defaults();

		/**
		 * Append a write to the section begun last
		 *
		 * \param index  element of the control, negative for the whole control
		 * \return false if there is no section or the table is full
		 */
		bool add_write(char const *control, unsigned control_len, int index,
		               char const *value, unsigned value_len);
		bool add_write(char const *control, int index, char const *value);

		/**
		 * Return the path of that name, or nullptr
		 */
		Path const *lookup(char const *name) const;

		Path const &defaults() const { return _defaults; }

		/**
		 * Call 'fn' with each 'Write' of 'path' in order
		 */
		template <typename FN>
		void for_each_write(Path const &path, FN const &fn) const
		{
			for (int i = path.first; i >= 0; i = _writes[i].next)
				fn(_writes[i]);
		}

		/**
		 * Return the n-th write of 'path', n < path.count
		 */
		Write const &write(Path const &path, unsigned n) const;

		unsigned path_count() const { return _path_count; }

		/**
		 * Number of 'begin_path' calls refused because of an existing name
		 */
		unsigned duplicates() const { return _duplicates; }

		/**
		 * True if a path, write or string did not fit and was dropped
		 */
		bool overflow() const { return _overflow; }
};


struct Audio_policy::Action
{
	enum class Type {
		DEFAULT,    /* initial value of a control: name, index, value */
		CONTROL,    /* control write: name, index, value */
		STREAM,     /* PCM held open without data: pcm, capture */
		TUNING,     /* AoC tuning parameter: block, component, key, number */
		PLAYBACK,   /* session-backed playback PCM: pcm, enabled */
		CAPTURE,    /* session-backed capture PCM: pcm, enabled */
		MISSING     /* path the table does not hold: name */
	};

	Type        type      = Type::CONTROL;
	char const *name      = "";
	char const *value     = "";
	int         index     = -1;      /* < 0: the whole control */
	unsigned    pcm       = 0;
	bool        capture   = false;
	bool        enabled   = false;
	unsigned    block     = 0;
	unsigned    component = 0;
	unsigned    key       = 0;
	unsigned    number    = 0;
};


struct Audio_policy::Config
{
	Route    route    = Route::MEDIA;
	Band     band     = Band::WB;     /* meaningful in a call only */
	Output   output   = Output::SPEAKER;   /* meaningful in the media route only */
	bool     record   = false;             /* meaningful in the media route only */
	Amp_mode amp_mode = Amp_mode::DSP;
	unsigned count    = 0;
	bool     overflow = false;        /* cannot happen with MAX_ACTIONS */
	Action   actions[MAX_ACTIONS];
};


/**
 * Set of names, to report each missing path once
 */
class Audio_policy::Names_seen
{
	public:

		enum { MAX = 32, NAME_LEN = 64 };

	private:

		char     _names[MAX][NAME_LEN];
		unsigned _count = 0;

	public:

		Names_seen() { clear(); }

		/**
		 * Remember 'name'
		 *
		 * \return true if the name was not known before. With MAX names
		 *         stored, a further name is new at every call.
		 */
		bool first_time(char const *name);

		void clear();
};

#endif /* _INCLUDE__AUDIO_POLICY__POLICY_H_ */
