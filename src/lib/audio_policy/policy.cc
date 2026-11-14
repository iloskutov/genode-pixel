/*
 * \brief  Audio policy of the Pixel 7: call state and requests -> driver config
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Control names and values that are not taken from the path table:
 * 'Audio DSP Mode' (enum Ambient/Record/Telephony/..., aoc_alsa_ctl.c:2460,
 * 2583), 'Incall Mic Mute' and 'Incall Sink Mute' (bool, 1 = INCALL_MUTE,
 * aoc_alsa_ctl.c:2743-2745, aoc_alsa.h:117). The call volume is not an ALSA
 * control but the AoC tuning parameter block 19, component 3, key 18.
 * 'PCM Source' (enum ASP/DSP, a DAPM mux) and 'AMP PCM Gain' (0..20) of the
 * amplifiers, the bottom one with the prefix "R " (google_amplifiers
 * cs35l41/cs35l41.c:1141, 1762; prefix from the device tree).
 *
 * Paths of the vendor's table the policy asks for: 'speaker', 'handset',
 * 'low-latency-playbackP', 'handset-mic', 'audio-recordC',
 * 'handset-mic-post' (media); 'voice-handset', 'voice-speaker',
 * 'hostless-ulC spk-vi', 'voice-handset-mic', 'voice-speaker-mic',
 * 'sidetone-for handset', 'voice-callC', 'voice-handset-mic-post',
 * 'voice-speaker-mic-post', 'voice-callP' (calls); the two
 * 'cs35l41-load-protection-firmware-*' paths (mode dsp).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <audio_policy/policy.h>

using namespace Audio_policy;

namespace {

	unsigned length(char const *s)
	{
		unsigned n = 0;
		if (s)
			while (s[n]) n++;
		return n;
	}

	bool equal(char const *a, char const *b)
	{
		if (!a || !b)
			return false;

		for (; *a && *a == *b; a++, b++);

		return *a == *b;
	}

	/**
	 * Appends the actions of one config in order
	 */
	class Builder
	{
		private:

			enum { MAX_MISSING = 16 };

			Paths const &_paths;
			Config      &_config;
			char const  *_missing[MAX_MISSING] { };
			unsigned     _missing_count = 0;

			Builder(Builder const &);
			Builder &operator = (Builder const &);

			void _add(Action const &action)
			{
				if (_config.count >= MAX_ACTIONS) {
					_config.overflow = true;
					return;
				}
				_config.actions[_config.count++] = action;
			}

			static Action _control(char const *name, char const *value, int index)
			{
				Action action { };
				action.type  = Action::Type::CONTROL;
				action.name  = name;
				action.value = value;
				action.index = index;
				return action;
			}

			void _writes(Path const &path, Action::Type type)
			{
				_paths.for_each_write(path, [&] (Write const &write) {
					Action action = _control(write.control, write.value, write.index);
					action.type = type;
					_add(action);
				});
			}

			/**
			 * Return the path, or note it as missing
			 */
			Path const *_lookup(char const *name)
			{
				Path const * const path = _paths.lookup(name);

				if (!path && _missing_count < MAX_MISSING)
					_missing[_missing_count++] = name;

				return path;
			}

		public:

			Builder(Paths const &paths, Config &config)
			: _paths(paths), _config(config) { }

			void defaults() { _writes(_paths.defaults(), Action::Type::DEFAULT); }

			void path(char const *name)
			{
				Path const * const path = _lookup(name);
				if (path)
					_writes(*path, Action::Type::CONTROL);
			}

			/**
			 * Two paths applied in a row, each control written once
			 *
			 * A write of the first path that the second path overrides is
			 * left out. The list then is the same in every config, and a
			 * driver that skips the writes of unchanged values leaves the
			 * controls alone after the first config. Listed one after the
			 * other, a control the two paths set differently would change
			 * twice in every config. (Android's audio_route also writes a
			 * control only when its value changes.)
			 */
			void paths_merged(char const *first_name, char const *second_name)
			{
				Path const * const first  = _lookup(first_name);
				Path const * const second = _lookup(second_name);

				if (first && second)
					_paths.for_each_write(*first, [&] (Write const &write) {

						bool overridden = false;
						_paths.for_each_write(*second, [&] (Write const &later) {
							if (equal(later.control, write.control)
							 && (later.index < 0 || later.index == write.index))
								overridden = true; });

						if (!overridden)
							_add(_control(write.control, write.value, write.index));
					});

				if (first && !second)
					_writes(*first, Action::Type::CONTROL);

				if (second)
					_writes(*second, Action::Type::CONTROL);
			}

			void control(char const *name, char const *value)
			{
				_add(_control(name, value, -1));
			}

			void stream(unsigned pcm, bool capture)
			{
				Action action { };
				action.type    = Action::Type::STREAM;
				action.pcm     = pcm;
				action.capture = capture;
				_add(action);
			}

			void tuning(unsigned block, unsigned component, unsigned key, unsigned number)
			{
				Action action { };
				action.type      = Action::Type::TUNING;
				action.block     = block;
				action.component = component;
				action.key       = key;
				action.number    = number;
				_add(action);
			}

			void session_pcm(Action::Type type, unsigned pcm, bool enabled)
			{
				Action action { };
				action.type    = type;
				action.pcm     = pcm;
				action.enabled = enabled;
				_add(action);
			}

			void missing()
			{
				for (unsigned i = 0; i < _missing_count; i++) {
					Action action { };
					action.type = Action::Type::MISSING;
					action.name = _missing[i];
					_add(action);
				}
			}
	};

	unsigned clamped_volume(int volume)
	{
		if (volume < 0)
			return 0;

		return (unsigned(volume) > MAX_VOLUME) ? unsigned(MAX_VOLUME) : unsigned(volume);
	}

	int clamped_gain(int gain)
	{
		return (gain < 0) ? 0 : (gain > MAX_ASP_GAIN) ? int(MAX_ASP_GAIN) : gain;
	}

	char const *gain_text(int gain)
	{
		static char const * const text[MAX_ASP_GAIN + 1] {
			"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9", "10",
			"11", "12", "13", "14", "15", "16", "17", "18", "19", "20" };

		return text[clamped_gain(gain)];
	}

	/**
	 * Amplifier firmware, mode DSP: what Android's audio HAL does once at
	 * its start (seen in a log of LineageOS)
	 *
	 * The first path clears 'DSP Booted' and 'DSP1 Preload Switch', the
	 * second one sets the preload switch, which makes the kernel load the
	 * firmware and its tuning.
	 */
	void amplifier_firmware(Builder &builder)
	{
		builder.paths_merged("cs35l41-load-protection-firmware-start",
		                     "cs35l41-load-protection-firmware-end");
	}

	/**
	 * Both amplifiers without their DSP, mode ASP
	 *
	 * The gain comes first: the driver takes back the writes of a route in
	 * reverse order, so the source returns to DSP (mute without firmware)
	 * before the gain returns to its initial value.
	 */
	void amplifiers_direct(Builder &builder, Settings const &settings)
	{
		char const * const gain = gain_text(settings.asp_gain);

		builder.control("AMP PCM Gain",   gain);
		builder.control("R AMP PCM Gain", gain);
		builder.control("PCM Source",     "ASP");
		builder.control("R PCM Source",   "ASP");
	}

	/**
	 * The source of both amplifiers stays ASP in every route of mode ASP
	 *
	 * The driver reverts a control that the next config does not name to
	 * its initial value, and the initial source is DSP. A route that left
	 * the bottom amplifier's source out (earpiece, handset call) made the
	 * driver switch it back to DSP while that amplifier was still on: the
	 * codec driver then powers the amplifier's DSP and asks for its
	 * firmware, which an image for mode ASP does not have -- the audio
	 * driver stopped at that request (device, 2026-10-03, speaker ->
	 * earpiece). Named by every config, the sources are
	 * written once and never reverted.
	 */
	void amplifier_sources_direct(Builder &builder)
	{
		builder.control("PCM Source",   "ASP");
		builder.control("R PCM Source", "ASP");
	}

	/**
	 * Recording with the built-in microphones
	 *
	 * The order is the one of a call (device path, use-case path, '-post'
	 * path). 'handset-mic' is the device path of the HAL's back ends
	 * IN_HANDSET_*MIC (seen in a log of LineageOS): microphone list, DC blocker, spatial module. 'audio-recordC'
	 * connects the microphones to the capture PCM 8, the '-post' path sets
	 * the recording gain, whose initial value is the lowest one (-40 dB).
	 */
	void microphones(Builder &builder)
	{
		builder.path("handset-mic");
		builder.path("audio-recordC");
		builder.path("handset-mic-post");
	}

	void media(Builder &builder, State const &state, Settings const &settings)
	{
		bool const earpiece = (state.output == Output::EARPIECE);

		builder.control("Audio DSP Mode", "Ambient");

		/*
		 * 'handset' drives the top amplifier directly (source ASP, its own
		 * gain) and leaves the bottom one at its initial values, i.e.,
		 * switched off
		 */
		builder.path(earpiece ? "handset" : "speaker");
		builder.path("low-latency-playbackP");

		/* both amplifiers: 'handset' sets the top one's gain itself */
		if (settings.amp_mode == Amp_mode::ASP) {
			if (earpiece) amplifier_sources_direct(builder);
			else          amplifiers_direct(builder, settings);
		}

		if (state.record)
			microphones(builder);

		builder.session_pcm(Action::Type::PLAYBACK, PCM_PLAYBACK, true);
		builder.session_pcm(Action::Type::CAPTURE,  PCM_CAPTURE,  state.record);
	}

	void call(Builder &builder, State const &state, Settings const &settings,
	          bool speaker)
	{
		enum { PLAYBACK = false, CAPTURE = true };

		/* amplifier(s) */
		builder.path(speaker ? "voice-speaker" : "voice-handset");

		/* amplifier feedback to AoC */
		builder.path("hostless-ulC spk-vi");
		builder.stream(PCM_HOSTLESS_TX, CAPTURE);

		builder.path(speaker ? "voice-speaker-mic" : "voice-handset-mic");

		/* AoC loads its voice processing */
		builder.control("Audio DSP Mode", "Telephony");

		if (!speaker)
			builder.path("sidetone-for handset");

		/* microphone to the modem, modem downlink to the speaker sink */
		builder.path("voice-callC");
		builder.path(speaker ? "voice-speaker-mic-post" : "voice-handset-mic-post");
		builder.path("voice-callP");

		/* 'voice-handset' sets the gain of the top amplifier itself */
		if (settings.amp_mode == Amp_mode::ASP) {
			if (speaker) amplifiers_direct(builder, settings);
			else         amplifier_sources_direct(builder);
		}

		builder.control("Incall Mic Mute",  state.mute ? "1" : "0");
		builder.control("Incall Sink Mute", "0");

		/* opening the downlink PCM starts the modem voice session in AoC */
		builder.stream(PCM_VOICE_RX, PLAYBACK);
		builder.stream(PCM_VOICE_TX, CAPTURE);

		builder.tuning(VOLUME_BLOCK, VOLUME_COMPONENT, VOLUME_KEY,
		               clamped_volume(state.volume));

		/* no media while in a call */
		builder.session_pcm(Action::Type::PLAYBACK, PCM_PLAYBACK, false);
		builder.session_pcm(Action::Type::CAPTURE,  PCM_CAPTURE,  false);
	}
}


Call Audio_policy::call_from_string(char const *name)
{
	if (equal(name, "idle"))     return Call::IDLE;
	if (equal(name, "incoming")) return Call::INCOMING;
	if (equal(name, "outbound")) return Call::OUTBOUND;
	if (equal(name, "alerting")) return Call::ALERTING;
	if (equal(name, "active"))   return Call::ACTIVE;

	return Call::UNAVAILABLE;
}


Band Audio_policy::band_from_string(char const *name)
{
	return equal(name, "nb") ? Band::NB : Band::WB;
}


Amp_mode Audio_policy::amp_mode_from_string(char const *name)
{
	return equal(name, "asp") ? Amp_mode::ASP : Amp_mode::DSP;
}


Output Audio_policy::output_from_string(char const *name)
{
	return equal(name, "earpiece") ? Output::EARPIECE : Output::SPEAKER;
}


char const *Audio_policy::output_name(Output output)
{
	return (output == Output::EARPIECE) ? "earpiece" : "speaker";
}


char const *Audio_policy::band_name(Band band)
{
	return (band == Band::NB) ? "nb" : "wb";
}


char const *Audio_policy::amp_mode_name(Amp_mode mode)
{
	return (mode == Amp_mode::ASP) ? "asp" : "dsp";
}


char const *Audio_policy::route_name(Route route)
{
	switch (route) {
	case Route::MEDIA:   return "media";
	case Route::HANDSET: return "handset";
	case Route::SPEAKER: return "speaker";
	}
	return "media";
}


Route Audio_policy::route(State const &state)
{
	bool const in_call = state.call == Call::OUTBOUND
	                  || state.call == Call::ALERTING
	                  || state.call == Call::ACTIVE;

	if (!in_call)
		return Route::MEDIA;

	return state.speaker ? Route::SPEAKER : Route::HANDSET;
}


State Audio_policy::normalized(State const &state)
{
	Route const r = route(state);

	/*
	 * No call: the requests of a call and the band are without effect, the
	 * output and the recording count. In a call it is the other way round.
	 */
	if (r == Route::MEDIA) {
		State result { };
		result.output = state.output;
		result.record = state.record;
		return result;
	}

	State result { };
	result.call    = Call::ACTIVE;
	result.speaker = (r == Route::SPEAKER);
	result.mute    = state.mute;
	result.volume  = int(clamped_volume(state.volume));
	result.band    = state.band;
	return result;
}


void Audio_policy::generate(State const &requested, Settings const &settings,
                            Paths const &paths, Config &config)
{
	State const state = normalized(requested);
	Route const r     = route(state);

	config.route    = r;
	config.band     = state.band;
	config.output   = state.output;
	config.record   = state.record;
	config.amp_mode = settings.amp_mode;
	config.count    = 0;
	config.overflow = false;

	Builder builder { paths, config };

	builder.defaults();

	if (settings.amp_mode == Amp_mode::DSP)
		amplifier_firmware(builder);

	if (r == Route::MEDIA)
		media(builder, state, settings);
	else
		call(builder, state, settings, r == Route::SPEAKER);

	builder.missing();
}


double Audio_policy::media_volume_factor(int step, double max)
{
	if (step < 0)                step = 0;
	if (step > MAX_MEDIA_VOLUME) step = MAX_MEDIA_VOLUME;

	if (!(max > 0.0)) max = 0.0;    /* also for a value that is not a number */
	if (max > 1.0)    max = 1.0;

	/* the quotient is exactly 1 at the highest step */
	return max * (double(step*step) / double(MAX_MEDIA_VOLUME*MAX_MEDIA_VOLUME));
}


double Audio_policy::media_factor(State const &state, int step, double max)
{
	return (route(state) == Route::MEDIA) ? media_volume_factor(step, max) : 0.0;
}


/***********
 ** Paths **
 ***********/

char const *Paths::_stored(char const *s, unsigned len)
{
	if (len >= POOL_SIZE - _pool_used)
		return nullptr;

	char * const dst = _pool + _pool_used;
	for (unsigned i = 0; i < len; i++)
		dst[i] = s[i];
	dst[len] = 0;

	_pool_used += len + 1;
	return dst;
}


void Paths::clear()
{
	_pool_used   = 0;
	_write_count = 0;
	_path_count  = 0;
	_defaults    = Path { };
	_current     = nullptr;
	_duplicates  = 0;
	_overflow    = false;
	_pool[0]     = 0;
}


bool Paths::begin_path(char const *name, unsigned name_len)
{
	_current = nullptr;

	if (_path_count >= MAX_PATHS) {
		_overflow = true;
		return false;
	}

	/* copy first: the lookup needs a terminated string */
	unsigned const pool_used = _pool_used;
	char const * const stored = _stored(name, name_len);
	if (!stored) {
		_overflow = true;
		return false;
	}

	if (lookup(stored)) {
		_pool_used = pool_used;
		_duplicates++;
		return false;
	}

	Path path { };
	path.name = stored;

	_paths[_path_count] = path;
	_current = &_paths[_path_count++];
	return true;
}


bool Paths::begin_path(char const *name)
{
	return begin_path(name, length(name));
}


void Paths::begin_defaults()
{
	_current = &_defaults;
}


bool Paths::add_write(char const *control, unsigned control_len, int index,
                      char const *value, unsigned value_len)
{
	if (!_current)
		return false;

	if (_write_count >= MAX_WRITES) {
		_overflow = true;
		return false;
	}

	unsigned const pool_used = _pool_used;

	Write write { };
	write.control = _stored(control, control_len);
	write.value   = _stored(value,   value_len);
	write.index   = (index < 0) ? -1 : index;

	if (!write.control || !write.value) {
		_pool_used = pool_used;
		_overflow  = true;
		return false;
	}

	int const slot = int(_write_count++);
	_writes[slot] = write;

	if (_current->last >= 0)
		_writes[_current->last].next = slot;
	else
		_current->first = slot;

	_current->last = slot;
	_current->count++;
	return true;
}


bool Paths::add_write(char const *control, int index, char const *value)
{
	return add_write(control, length(control), index, value, length(value));
}


Path const *Paths::lookup(char const *name) const
{
	for (unsigned i = 0; i < _path_count; i++)
		if (equal(_paths[i].name, name))
			return &_paths[i];

	return nullptr;
}


Write const &Paths::write(Path const &path, unsigned n) const
{
	static Write const none { };

	int i = path.first;
	for (; i >= 0 && n; n--)
		i = _writes[i].next;

	return (i >= 0) ? _writes[i] : none;
}


/****************
 ** Names_seen **
 ****************/

void Names_seen::clear()
{
	_count = 0;
}


bool Names_seen::first_time(char const *name)
{
	char     copy[NAME_LEN];
	unsigned len = length(name);

	if (len > NAME_LEN - 1)
		len = NAME_LEN - 1;

	for (unsigned i = 0; i < len; i++)
		copy[i] = name[i];
	copy[len] = 0;

	for (unsigned i = 0; i < _count; i++)
		if (equal(_names[i], copy))
			return false;

	if (_count < MAX) {
		for (unsigned i = 0; i <= len; i++)
			_names[_count][i] = copy[i];
		_count++;
	}
	return true;
}
