/*
 * \brief  Audio policy of the Pixel 7: generates the config of pixel_audio
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * ROMs 'call_state' (modem_link), 'audio_request' (user interface: call
 * requests, media volume, media output, recording) and
 * 'paths' (tool/mixer_paths_to_hid.py) in, reports 'config' (for the
 * driver) and 'mixer_config' (for record_play_mixer) out. The policy is the
 * library 'audio_policy'; this component parses the ROMs and writes the
 * results as HID. See README for the formats.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <os/reporter.h>

/* local includes */
#include <audio_policy/policy.h>

namespace Audio_control {

	using namespace Genode;

	struct Main;
}


struct Audio_control::Main
{
	using Action = Audio_policy::Action;

	Env &_env;

	/* what does not change at run time: amplifier mode, mixer template */
	Attached_rom_dataspace _config_rom        { _env, "config" };

	Attached_rom_dataspace _call_state_rom    { _env, "call_state" };
	Attached_rom_dataspace _audio_request_rom { _env, "audio_request" };
	Attached_rom_dataspace _paths_rom         { _env, "paths" };

	/* the config with the vendor's 199 initial values takes about 16 KiB */
	Expanding_reporter _reporter { _env, "config", "config",
	                               Expanding_reporter::Initial_buffer_size { 32*1024 } };

	/* the config of record_play_mixer, only with a 'mixer' node in the config */
	Constructible<Expanding_reporter> _mixer_reporter { };

	Audio_policy::Settings const _settings = _settings_from_config();

	/* volume factor of the highest media-volume step */
	double const _max_media_volume =
		_config_rom.node().attribute_value("max_media_volume", 1.0);

	Audio_policy::Paths      _paths    { };
	Audio_policy::Config     _config   { };
	Audio_policy::State      _state    { };
	Audio_policy::Names_seen _warned   { };   /* missing paths */
	Audio_policy::Names_seen _warned_outputs { };
	bool                     _reported = false;

	/* volume factor of media playback in the mixer config reported last */
	double _media_factor   = 0;
	bool   _mixer_reported = false;

	Audio_policy::Settings _settings_from_config() const
	{
		using namespace Audio_policy;
		using Name = String<16>;

		Node const &config = _config_rom.node();
		Name const  mode   = config.attribute_value("amp_mode", Name("dsp"));

		if (mode != "dsp" && mode != "asp")
			warning("amp_mode '", mode, "' is neither 'dsp' nor 'asp', using 'dsp'");

		long const gain = config.attribute_value("asp_gain", long(DEFAULT_ASP_GAIN));

		Settings settings { };
		settings.amp_mode = amp_mode_from_string(mode.string());
		settings.asp_gain = (gain < 0)                  ? 0
		                  : (gain > long(MAX_ASP_GAIN)) ? int(MAX_ASP_GAIN) : int(gain);

		if (gain != long(settings.asp_gain))
			warning("asp_gain ", gain, " is outside 0..", int(MAX_ASP_GAIN),
			        ", using ", settings.asp_gain);

		return settings;
	}

	/**
	 * Call 'fn' with start and length of the attribute value, if present
	 */
	static void _with_attribute(Node const &node, char const *name, auto const &fn)
	{
		bool done = false;
		size_t const len = strlen(name);
		node.for_each_attribute([&] (Node::Attribute const &attr) {
			if (done || attr.tag.num_bytes != len || strcmp(attr.tag.start, name, len))
				return;
			done = true;
			fn(attr.value.start, unsigned(attr.value.num_bytes));
		});
	}

	void _import_writes(Node const &section)
	{
		section.for_each_sub_node("ctl", [&] (Node const &ctl) {
			int const index = int(ctl.attribute_value("index", -1L));
			_with_attribute(ctl, "name", [&] (char const *name, unsigned name_len) {
				_with_attribute(ctl, "value", [&] (char const *value, unsigned value_len) {
					_paths.add_write(name, name_len, index, value, value_len); }); });
		});
	}

	void _import_paths(Node const &paths)
	{
		_paths.clear();
		_warned.clear();

		unsigned unnamed = 0;

		paths.for_each_sub_node([&] (Node const &node) {

			if (node.has_type("defaults")) {
				_paths.begin_defaults();
				_import_writes(node);
			}

			if (node.has_type("path")) {
				bool named = false;
				_with_attribute(node, "name", [&] (char const *name, unsigned len) {
					named = true;
					if (_paths.begin_path(name, len))
						_import_writes(node);
				});
				if (!named)
					unnamed++;
			}
		});

		if (unnamed)
			warning(unnamed, " paths without name ignored");

		if (_paths.duplicates())
			warning(_paths.duplicates(), " paths defined more than once, "
			        "first definition used");

		if (_paths.overflow())
			warning("path table exceeds ", unsigned(Audio_policy::MAX_PATHS), " paths, ",
			        unsigned(Audio_policy::MAX_WRITES), " writes or ",
			        unsigned(Audio_policy::POOL_SIZE), " bytes of text, rest dropped");

		log("path table: ", _paths.path_count(), " paths, ",
		    _paths.defaults().count, " initial values");
	}

	Audio_policy::State _requested_state()
	{
		using namespace Audio_policy;
		using Name = String<16>;

		Node const &call    = _call_state_rom.node();
		Node const &request = _audio_request_rom.node();

		long const volume = request.attribute_value("volume", long(DEFAULT_VOLUME));

		State state { };
		state.call    = call_from_string(call.attribute_value("state", Name()).string());
		state.band    = band_from_string(call.attribute_value("band",  Name()).string());
		state.speaker = request.attribute_value("speaker", false);
		state.mute    = request.attribute_value("mute",    false);
		state.record  = request.attribute_value("record",  false);

		Name const output = request.attribute_value("output", Name("speaker"));

		if (output != "speaker" && output != "earpiece"
		 && _warned_outputs.first_time(output.string()))
			warning("output '", output, "' is neither 'speaker' nor 'earpiece', "
			        "using 'speaker'");

		state.output = output_from_string(output.string());
		state.volume  = (volume < 0)                ? 0
		              : (volume > long(MAX_VOLUME)) ? int(MAX_VOLUME) : int(volume);

		/* requests that do not count in this state do not matter */
		return normalized(state);
	}

	int _requested_media_volume() const
	{
		using namespace Audio_policy;

		long const step = _audio_request_rom.node().attribute_value(
			"media_volume", long(DEFAULT_MEDIA_VOLUME));

		return (step < 0)                      ? 0
		     : (step > long(MAX_MEDIA_VOLUME)) ? int(MAX_MEDIA_VOLUME) : int(step);
	}

	static void _gen_write(Generator &g, Action const &action)
	{
		g.attribute("name", action.name);
		if (action.index >= 0)
			g.attribute("index", action.index);
		g.attribute("value", action.value);
	}

	static void _gen_action(Generator &g, Action const &action)
	{
		auto yes_no = [] (bool v) { return v ? "yes" : "no"; };

		switch (action.type) {

		case Action::Type::DEFAULT:
			g.node("default", [&] { _gen_write(g, action); });
			break;

		case Action::Type::CONTROL:
			g.node("control", [&] { _gen_write(g, action); });
			break;

		case Action::Type::STREAM:
			g.node("stream", [&] {
				g.attribute("pcm",       action.pcm);
				g.attribute("direction", action.capture ? "capture" : "playback"); });
			break;

		case Action::Type::TUNING:
			g.node("tuning", [&] {
				g.attribute("block",     action.block);
				g.attribute("component", action.component);
				g.attribute("key",       action.key);
				g.attribute("value",     action.number); });
			break;

		case Action::Type::PLAYBACK:
		case Action::Type::CAPTURE:
			g.node(action.type == Action::Type::PLAYBACK ? "playback" : "capture", [&] {
				g.attribute("pcm",     action.pcm);
				g.attribute("enabled", yes_no(action.enabled)); });
			break;

		case Action::Type::MISSING:
			g.node("missing", [&] { g.attribute("name", action.name); });
			break;
		}
	}

	void _report()
	{
		using namespace Audio_policy;

		generate(_state, _settings, _paths, _config);

		for (unsigned i = 0; i < _config.count; i++) {
			Action const &action = _config.actions[i];
			if (action.type == Action::Type::MISSING && _warned.first_time(action.name))
				warning("path '", action.name, "' is not in the path table, skipped");
		}

		if (_config.overflow)
			warning("config truncated to ", unsigned(MAX_ACTIONS), " actions");

		_reporter.generate([&] (Generator &g) {
			g.attribute("route", route_name(_config.route));
			if (_config.route == Route::MEDIA) {
				g.attribute("output", output_name(_config.output));
				g.attribute("record", _config.record ? "yes" : "no");
			} else {
				g.attribute("voice_band", band_name(_config.band));
			}
			g.attribute("amp_mode", amp_mode_name(_config.amp_mode));

			for (unsigned i = 0; i < _config.count; i++)
				_gen_action(g, _config.actions[i]);
		});

		if (_config.route == Route::MEDIA)
			log("route media, output ", output_name(_config.output),
			    ", record ", _config.record ? "yes" : "no");
		else
			log("route ", route_name(_config.route),
			    ", mute ", _state.mute ? "yes" : "no",
			    ", volume ", _state.volume,
			    ", band ", band_name(_config.band));

		_reported = true;
	}

	/**
	 * Copy a node of the mixer template: the attribute value 'volume: media'
	 * becomes the volume factor of media playback
	 */
	static void _gen_mixer_node(Generator &g, Node const &node, double factor)
	{
		node.for_each_attribute([&] (Node::Attribute const &attr) {

			using Tag = String<64>;
			Tag const tag(Cstring(attr.tag.start, attr.tag.num_bytes));

			bool const media = tag == "volume"
			                && attr.value.num_bytes == 5
			                && strcmp(attr.value.start, "media", 5) == 0;
			if (media)
				g.attribute("volume", factor);
			else
				g.attribute(tag.string(), attr.value.start,
				            attr.value.num_bytes);
		});

		node.for_each_sub_node([&] (Node const &sub_node) {
			g.node(sub_node.type().string(), [&] {
				_gen_mixer_node(g, sub_node, factor); }); });
	}

	/**
	 * Write the mixer config if the volume of media playback changed
	 */
	void _report_mixer()
	{
		if (!_mixer_reporter.constructed())
			return;

		int    const step   = _requested_media_volume();
		double const factor = Audio_policy::media_factor(_state, step, _max_media_volume);

		if (_mixer_reported && factor == _media_factor)
			return;

		_config_rom.node().with_optional_sub_node("mixer", [&] (Node const &mixer) {
			_mixer_reporter->generate([&] (Generator &g) {
				_gen_mixer_node(g, mixer, factor); }); });

		if (Audio_policy::route(_state) != Audio_policy::Route::MEDIA)
			log("mixer: media muted during the call");
		else
			log("mixer: media volume ", step, ", factor ", factor);

		_media_factor   = factor;
		_mixer_reported = true;
	}

	void _handle_request()
	{
		_call_state_rom.update();
		_audio_request_rom.update();

		Audio_policy::State const state = _requested_state();

		/* nothing that affects the config: leave the driver alone */
		if (!_reported || state != _state) {
			_state = state;
			_report();
		}

		_report_mixer();
	}

	void _handle_paths()
	{
		_paths_rom.update();
		_import_paths(_paths_rom.node());
		_report();
	}

	Signal_handler<Main> _request_handler { _env.ep(), *this, &Main::_handle_request };
	Signal_handler<Main> _paths_handler   { _env.ep(), *this, &Main::_handle_paths };

	Main(Env &env) : _env(env)
	{
		_call_state_rom.sigh(_request_handler);
		_audio_request_rom.sigh(_request_handler);
		_paths_rom.sigh(_paths_handler);

		using namespace Audio_policy;

		if (_settings.amp_mode == Amp_mode::ASP)
			log("amplifier mode asp (no speaker protection), gain ",
			    _settings.asp_gain);
		else
			log("amplifier mode dsp");

		if (_max_media_volume < 0.0 || _max_media_volume > 1.0)
			warning("max_media_volume ", _max_media_volume, " is outside 0..1, clamped");

		if (_config_rom.node().has_sub_node("mixer"))
			_mixer_reporter.construct(_env, "config", "mixer_config");

		_state = _requested_state();
		_handle_paths();
		_report_mixer();
	}
};


void Component::construct(Genode::Env &env)
{
	static Audio_control::Main main(env);
}
