/*
 * \brief  Genode side of the sound card: Record/Play sessions, the 'config'
 *         ROM with the mixer policy, the "mixer" report
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Modelled on repos/pc/src/driver/audio/pc/audio.cc. Differences: no
 * Audio_in/Audio_out mode; the sessions are opened when the card task asks
 * for them (the early bring-up stages run without a mixer); the Record
 * sessions' wake-up signal is used to start the playback PCM; controls are
 * addressed by name and reported without a limit on their number; the
 * configuration is the ordered list of a mixer policy.
 */

/*
 * Derived from the following file of the Genode OS framework:
 *
 *   repos/pc/src/driver/audio/pc/audio.cc (author Sebastian Sumpf)
 *
 * Copyright (C) 2022-2024 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <play_session/connection.h>
#include <record_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>

#include <lx_kit/env.h>

#include "audio.h"
#include "audio_main.h"

using namespace Genode;

namespace Pixel_audio {

	struct Stereo_output;
	struct Stereo_input;
	struct Plan;
}


/*************************************
 ** Record sessions (playback data) **
 *************************************/

struct Pixel_audio::Stereo_output : Noncopyable
{
	enum { FRAMES = PIXEL_AUDIO_PERIOD_FRAMES, CHANNELS = PIXEL_AUDIO_CHANNELS };

	Env &_env;

	Record::Connection _left  { _env, "left"  };
	Record::Connection _right { _env, "right" };

	/* 16 bit per sample, interleaved left and right */
	int16_t _data[FRAMES*CHANNELS] { };

	/*
	 * Sent by the mixer when samples become available after the sessions
	 * were depleted (and once when the handler is installed)
	 */
	void _handle_wakeup()
	{
		pixel_audio_card_notify(PIXEL_AUDIO_EVENT_WAKEUP);
		Lx_kit::env().scheduler.execute();
	}

	Signal_handler<Stereo_output> _wakeup_handler {
		_env.ep(), *this, &Stereo_output::_handle_wakeup };

	static int16_t _s16(float v)
	{
		v = (v > 1.0f) ? 1.0f : (v < -1.0f) ? -1.0f : v;
		return int16_t(v*32767);
	}

	int16_t const *record()
	{
		using Samples_ptr = Record::Connection::Samples_ptr;

		Record::Num_samples const n { FRAMES };

		bool depleted = false;

		_left.record(n,
			[&] (Record::Time_window const tw, Samples_ptr const &samples) {

				for (unsigned i = 0; i < FRAMES; i++)
					_data[i*CHANNELS] = _s16(samples.start[i]);

				_right.record_at(tw, n, [&] (Samples_ptr const &samples) {
					for (unsigned i = 0; i < FRAMES; i++)
						_data[i*CHANNELS + 1] = _s16(samples.start[i]); });
			},
			[&] { depleted = true; });

		return depleted ? nullptr : _data;
	}

	Stereo_output(Env &env) : _env(env)
	{
		_left.wakeup_sigh(_wakeup_handler);
	}
};


/********************************
 ** Play sessions (microphone) **
 ********************************/

struct Pixel_audio::Stereo_input : Noncopyable
{
	enum { FRAMES = PIXEL_AUDIO_PERIOD_FRAMES, CHANNELS = PIXEL_AUDIO_CHANNELS };

	Env &_env;

	Play::Connection _left  { _env, "mic_left"  };
	Play::Connection _right { _env, "mic_right" };

	Play::Time_window _time_window { };

	void play(int16_t const *frames)
	{
		float const scale = 1.0f/32768;

		/* 10 ms per period, a hint for the first one only */
		Play::Duration const duration_us { 1000u*1000u*FRAMES/PIXEL_AUDIO_RATE };

		_time_window = _left.schedule_and_enqueue(_time_window, duration_us,
			[&] (auto &submit) {
				for (unsigned i = 0; i < FRAMES; i++)
					submit(scale*float(frames[i*CHANNELS])); });

		_right.enqueue(_time_window,
			[&] (auto &submit) {
				for (unsigned i = 0; i < FRAMES; i++)
					submit(scale*float(frames[i*CHANNELS + 1])); });
	}

	void stop()
	{
		_left.stop();
		_right.stop();
		_time_window = { };
	}

	Stereo_input(Env &env) : _env(env) { }
};


/**************************
 ** Parsed configuration **
 **************************/

/*
 * One heap block: the plan, its two arrays and all strings. The C side
 * sees the embedded 'pixel_audio_plan' only.
 */
struct Pixel_audio::Plan
{
	struct pixel_audio_plan plan;   /* first member: the C handle */
	size_t                  size;
};


namespace {

	using namespace Pixel_audio;

	struct Audio
	{
		Env &env;

		Heap heap { env.ram(), env.rm() };

		Attached_rom_dataspace config { env, "config" };

		void handle_config()
		{
			pixel_audio_card_notify(PIXEL_AUDIO_EVENT_CONFIG);
			Lx_kit::env().scheduler.execute();
		}

		Signal_handler<Audio> config_handler {
			env.ep(), *this, &Audio::handle_config };

		Constructible<Stereo_output>      output { };
		Constructible<Stereo_input>       input  { };
		Constructible<Expanding_reporter> mixer  { };

		Audio(Env &env) : env(env) { config.sigh(config_handler); }
	};

	Audio *audio_ptr;

	/* call 'fn' with start and length of the attribute value, if present */
	bool with_attribute(Node const &node, char const *name, auto const &fn)
	{
		bool done = false;
		node.for_each_attribute([&] (Node::Attribute const &attr) {
			if (done || Generator::Tag_name { attr.tag } != name)
				return;
			done = true;
			fn(attr.value.start, attr.value.num_bytes);
		});
		return done;
	}

	size_t attr_len(Node const &node, char const *name)
	{
		size_t len = 0;
		with_attribute(node, name, [&] (char const *, size_t n) { len = n; });
		return len;
	}

	bool write_node(Node const &node)
	{
		return node.has_type("default") || node.has_type("control");
	}

	bool action_node(Node const &node)
	{
		return node.has_type("control") || node.has_type("stream")
		    || node.has_type("tuning");
	}
}


void Pixel_audio::audio_init(Genode::Env &env)
{
	static Audio audio { env };
	audio_ptr = &audio;
}


/*****************
 ** C interface **
 *****************/

extern "C" int pixel_audio_playback_sessions(void)
{
	if (!audio_ptr)
		return -1;

	if (audio_ptr->output.constructed())
		return 0;

	try {
		audio_ptr->output.construct(audio_ptr->env);
	} catch (...) {
		error("Record sessions \"left\"/\"right\" could not be opened");
		return -1;
	}
	return 0;
}


extern "C" short const *pixel_audio_record_period(void)
{
	if (!audio_ptr || !audio_ptr->output.constructed())
		return nullptr;

	return audio_ptr->output->record();
}


extern "C" int pixel_audio_capture_sessions(void)
{
	if (!audio_ptr)
		return -1;

	if (audio_ptr->input.constructed())
		return 0;

	try {
		audio_ptr->input.construct(audio_ptr->env);
	} catch (...) {
		error("Play sessions \"mic_left\"/\"mic_right\" could not be opened");
		return -1;
	}
	return 0;
}


extern "C" void pixel_audio_play_period(short const *frames)
{
	if (audio_ptr && audio_ptr->input.constructed() && frames)
		audio_ptr->input->play(frames);
}


extern "C" void pixel_audio_play_stop(void)
{
	if (audio_ptr && audio_ptr->input.constructed())
		audio_ptr->input->stop();
}


extern "C" struct pixel_audio_plan const *pixel_audio_plan_acquire(void)
{
	if (!audio_ptr)
		return nullptr;

	Audio &audio = *audio_ptr;

	audio.config.update();

	Node const config = audio.config.node();

	/*
	 * First pass: number of entries and bytes of all strings
	 */
	unsigned num_defaults = 0, num_actions = 0;
	size_t   strings = attr_len(config, "route") + 1;

	config.for_each_sub_node([&] (Node const &node) {

		if (node.has_type("default")) num_defaults++;
		if (action_node(node))        num_actions++;

		if (write_node(node))
			strings += attr_len(node, "name") + 1 + attr_len(node, "value") + 1;
	});

	size_t const size = sizeof(Plan)
	                  + num_defaults*sizeof(pixel_audio_write)
	                  + num_actions *sizeof(pixel_audio_action)
	                  + strings;

	void *block = nullptr;
	try { block = audio.heap.alloc(size); } catch (...) { }
	if (!block)
		return nullptr;

	bzero(block, size);

	Plan              &plan     = *(Plan *)block;
	pixel_audio_write *defaults = (pixel_audio_write *)(&plan + 1);
	pixel_audio_action *actions = (pixel_audio_action *)(defaults + num_defaults);
	char              *str      = (char *)(actions + num_actions);

	auto stored = [&] (Node const &node, char const *name) {
		char * const result = str;
		with_attribute(node, name, [&] (char const *start, size_t n) {
			memcpy(str, start, n);
			str += n; });
		*str++ = 0;
		return result;
	};

	plan.size              = size;
	plan.plan.route        = stored(config, "route");
	plan.plan.defaults     = defaults;
	plan.plan.actions      = actions;
	plan.plan.playback_pcm = -1;
	plan.plan.capture_pcm  = -1;

	auto fill_write = [&] (Node const &node, pixel_audio_write &w) {
		w.name  = stored(node, "name");
		w.index = int(node.attribute_value("index", -1L));
		w.value = stored(node, "value");
	};

	unsigned d = 0, a = 0;

	/*
	 * Second pass over the same ROM content
	 */
	config.for_each_sub_node([&] (Node const &node) {

		if (node.has_type("default")) {
			if (d < num_defaults)
				fill_write(node, defaults[d++]);
			return;
		}

		if (action_node(node) && a < num_actions) {

			pixel_audio_action &action = actions[a++];

			if (node.has_type("control")) {
				action.type = PIXEL_AUDIO_ACTION_CONTROL;
				fill_write(node, action.write);
			}
			else if (node.has_type("stream")) {
				using Direction = String<16>;
				action.type    = PIXEL_AUDIO_ACTION_STREAM;
				action.pcm     = node.attribute_value("pcm", 0u);
				action.capture = node.attribute_value("direction", Direction())
				              == "capture";
			}
			else {
				action.type      = PIXEL_AUDIO_ACTION_TUNING;
				action.block     = node.attribute_value("block",     0u);
				action.component = node.attribute_value("component", 0u);
				action.key       = node.attribute_value("key",       0u);
				action.value     = node.attribute_value("value",     0u);
			}
			return;
		}

		if (node.has_type("playback")) {
			if (node.attribute_value("enabled", false))
				plan.plan.playback_pcm = int(node.attribute_value("pcm", 0u));
			return;
		}

		if (node.has_type("capture")) {
			if (node.attribute_value("enabled", false))
				plan.plan.capture_pcm = int(node.attribute_value("pcm", 0u));
			return;
		}

		if (node.has_type("missing")) {
			using Name = String<96>;
			warning("config: path \"", node.attribute_value("name", Name()),
			        "\" is missing in the path table of the policy, its "
			        "control writes are absent");
			return;
		}

		warning("config: node of unknown type ignored: ", node);
	});

	plan.plan.num_defaults = d;
	plan.plan.num_actions  = a;

	return &plan.plan;
}


extern "C" void pixel_audio_plan_release(struct pixel_audio_plan const *plan)
{
	if (!audio_ptr || !plan)
		return;

	Plan * const block = (Plan *)plan;

	audio_ptr->heap.free(block, block->size);
}


namespace {

	/* text buffer for an attribute value that lists the elements */
	struct Text
	{
		char   buf[4096];
		size_t len = 0;

		void append(char const *s)
		{
			for (; *s && len + 1 < sizeof(buf); s++)
				buf[len++] = *s;
			buf[len] = 0;
		}

		void number(long long v, bool hex)
		{
			if (len)
				append(" ");

			if (hex) {
				char const digits[] = "0123456789abcdef";
				char const pair[3] = { digits[(v >> 4) & 0xf], digits[v & 0xf], 0 };
				append(pair);
			} else {
				append(String<24>(v).string());
			}
		}
	};
}


extern "C" void pixel_audio_report_mixer(char const *card_name,
                                         struct pixel_audio_ctl_report const *ctls,
                                         unsigned count)
{
	if (!audio_ptr)
		return;

	Audio &audio = *audio_ptr;

	if (!audio.mixer.constructed()) {
		try {
			audio.mixer.construct(audio.env, "mixer", "mixer",
			                      Expanding_reporter::Initial_buffer_size { 128*1024 });
		} catch (...) { }

		if (!audio.mixer.constructed()) {
			error("report \"mixer\" could not be created (no Report service?)");
			return;
		}
	}

	static Text text;

	audio.mixer->generate([&] (Generator &g) {

		g.attribute("card",     card_name);
		g.attribute("controls", count);

		for (unsigned i = 0; i < count; i++) {

			pixel_audio_ctl_report const &c = ctls[i];

			bool const is_enum  = !strcmp(c.type, "enum");
			bool const is_bytes = !strcmp(c.type, "bytes");
			bool const is_int   = !strcmp(c.type, "int") || !strcmp(c.type, "int64");

			g.node("control", [&] {

				g.attribute("name",   c.name);
				g.attribute("numid",  c.numid);
				g.attribute("iface",  c.iface);
				g.attribute("type",   c.type);
				g.attribute("count",  c.count);
				g.attribute("access", c.readable && c.writable ? "rw"
				                    : c.readable               ? "r"
				                    : c.writable               ? "w" : "-");

				if (is_int) {
					g.attribute("min", c.min);
					g.attribute("max", c.max);
				}

				if (c.read_error)
					g.attribute("read_error", c.read_error);

				if (c.num_values) {

					/* the item text for enums, as a 'control' node names it */
					if (is_enum && c.items && c.values[0] >= 0
					 && c.values[0] < (long long)c.num_items && c.items[c.values[0]])
						g.attribute("value", c.items[c.values[0]]);

					text.len = 0;
					for (unsigned k = 0; k < c.num_values; k++)
						text.number(c.values[k], is_bytes);

					g.attribute(is_enum ? "selected" : "value", text.buf, text.len);
				}

				if (is_enum)
					for (unsigned k = 0; k < c.num_items; k++)
						g.node("item", [&] {
							g.attribute("name",  c.items[k] ? c.items[k] : "");
							g.attribute("index", k); });
			});
		}
	});
}
