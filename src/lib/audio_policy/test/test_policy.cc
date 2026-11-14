/* Host test: Audio_policy (state + path table -> driver config) */
#include <audio_policy/policy.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using namespace Audio_policy;
using Type = Action::Type;


/*
 * A path table with the path names the policy asks for and a representative
 * subset of control writes (control names as in the kernel drivers, not the
 * vendor's table).
 */
struct Ctl { char const *name; int index; char const *value; };

static void add(Paths &paths, char const *name, std::vector<Ctl> const &ctls)
{
	if (std::strcmp(name, "defaults") == 0)
		paths.begin_defaults();
	else
		CHECK(paths.begin_path(name));

	for (Ctl const &c : ctls)
		CHECK(paths.add_write(c.name, c.index, c.value));
}

static void fill(Paths &paths, char const *without = "")
{
	struct Entry { char const *name; std::vector<Ctl> ctls; };

	std::vector<Entry> const table {
		{ "defaults", {
			{ "TDM_0_RX Chan",               -1, "Four" },
			{ "Main AMP Enable Switch",      -1, "0" },
			{ "R Main AMP Enable Switch",    -1, "0" },
			{ "BUILDIN MIC ID CAPTURE LIST",  0, "-1" },
			{ "BUILDIN MIC ID CAPTURE LIST",  1, "-1" },
			{ "Incall Mic Mute",             -1, "0" } } },
		{ "sidetone-for handset", {
			{ "Sidetone Biquad0",  0, "1065353216" },
			{ "Sidetone Biquad0",  1, "1065353216" },
			{ "Sidetone Volume",  -1, "-90" },
			{ "Sidetone Enable",  -1, "1" } } },
		{ "low-latency-playbackP",  { { "TDM_0_RX Mixer EP2",           -1, "1" } } },
		{ "voice-callP",            { { "TDM_0_RX Mixer EP5",           -1, "1" } } },
		{ "voice-callC",            { { "EP4 TX Mixer INTERNAL_MIC_TX", -1, "1" } } },
		{ "hostless-ulC spk-vi",    { { "NoHost1 TX Mixer TDM_0_TX",    -1, "1" } } },
		{ "handset", {
			{ "PCM Source",             -1, "ASP" },
			{ "AMP PCM Gain",           -1, "6" },
			{ "Main AMP Enable Switch", -1, "1" } } },
		{ "audio-recordC",    { { "EP1 TX Mixer INTERNAL_MIC_TX", -1, "1" } } },
		{ "handset-mic", {
			{ "Mic Spatial Module Enable",   -1, "1" },
			{ "BUILDIN MIC ID CAPTURE LIST",  0, "0" },
			{ "BUILDIN MIC ID CAPTURE LIST",  1, "3" },
			{ "MIC DC Blocker",              -1, "1" } } },
		{ "handset-mic-post", { { "MIC Record Soft Gain (dB)", -1, "22" } } },
		{ "voice-handset", {
			{ "Boost Peak Current Limit", -1, "3.50A" },
			{ "PCM Source",               -1, "ASP" },
			{ "AMP PCM Gain",             -1, "7" },
			{ "Main AMP Enable Switch",   -1, "1" } } },
		{ "speaker", {
			{ "Boost Peak Current Limit",   -1, "2.50A" },
			{ "R Boost Peak Current Limit", -1, "2.50A" },
			{ "Main AMP Enable Switch",     -1, "1" },
			{ "R Main AMP Enable Switch",   -1, "1" } } },
		{ "voice-speaker", {
			{ "ASPTX1 Slot Position",     -1, "1" },
			{ "R Main AMP Enable Switch", -1, "1" },
			{ "Main AMP Enable Switch",   -1, "1" } } },
		{ "voice-handset-mic", {
			{ "BUILDIN MIC ID CAPTURE LIST",          0, "0" },
			{ "BUILDIN MIC ID CAPTURE LIST",          1, "3" },
			{ "MIC HW Gain At High Power Mode (cB)", -1, "130" } } },
		{ "voice-handset-mic-post", { { "MIC Record Soft Gain (dB)", -1, "0" } } },
		{ "voice-speaker-mic", {
			{ "BUILDIN MIC ID CAPTURE LIST",          0, "0" },
			{ "BUILDIN MIC ID CAPTURE LIST",          1, "2" },
			{ "MIC HW Gain At High Power Mode (cB)", -1, "120" } } },
		{ "voice-speaker-mic-post", { { "MIC Record Soft Gain (dB)", -1, "1" } } },
		{ "cs35l41-load-protection-firmware-start", {
			{ "DSP Booted",            -1, "0" },
			{ "R DSP Booted",          -1, "0" },
			{ "DSP1 Preload Switch",   -1, "0" },
			{ "R DSP1 Preload Switch", -1, "0" } } },
		{ "cs35l41-load-protection-firmware-end", {
			{ "DSP1 Preload Switch",   -1, "1" },
			{ "R DSP1 Preload Switch", -1, "1" } } },
	};

	for (Entry const &e : table)
		if (std::strcmp(e.name, without) != 0)
			add(paths, e.name, e.ctls);
}


/* the config as text, one line per action */
static std::string text(Config const &c)
{
	std::string s = std::string("route ") + route_name(c.route);
	if (c.route != Route::MEDIA)
		s += std::string(" band ") + band_name(c.band);

	/* what a plain media config does not have */
	if (c.route == Route::MEDIA && c.output != Output::SPEAKER)
		s += std::string(" output ") + output_name(c.output);
	if (c.route == Route::MEDIA && c.record)
		s += " record";
	s += "\n";

	for (unsigned i = 0; i < c.count; i++) {
		Action const &a = c.actions[i];
		switch (a.type) {
		case Type::DEFAULT:
		case Type::CONTROL:
			s += (a.type == Type::DEFAULT) ? "default " : "control ";
			s += a.name;
			if (a.index >= 0) s += " [" + std::to_string(a.index) + "]";
			s += std::string(" = ") + a.value;
			break;
		case Type::STREAM:
			s += "stream " + std::to_string(a.pcm) + (a.capture ? " capture" : " playback");
			break;
		case Type::TUNING:
			s += "tuning " + std::to_string(a.block) + " " + std::to_string(a.component)
			   + " " + std::to_string(a.key) + " = " + std::to_string(a.number);
			break;
		case Type::PLAYBACK:
		case Type::CAPTURE:
			s += (a.type == Type::PLAYBACK) ? "playback " : "capture ";
			s += std::to_string(a.pcm) + (a.enabled ? " yes" : " no");
			break;
		case Type::MISSING:
			s += std::string("missing ") + a.name;
			break;
		}
		s += "\n";
	}
	if (c.overflow) s += "overflow\n";
	return s;
}

/* large objects: static, one set per call site through the tag */
struct Setup
{
	Paths  paths  { };
	Config config { };

	Setup(char const *without = "") { fill(paths, without); }

	std::string run(State const &state, Settings const &settings = Settings { })
	{
		generate(state, settings, paths, config);
		return text(config);
	}
};

static Settings asp(int gain = DEFAULT_ASP_GAIN)
{
	Settings s;
	s.amp_mode = Amp_mode::ASP;
	s.asp_gain = gain;
	return s;
}

static State state(Call call, bool speaker = false, bool mute = false, int volume = 4,
                   Band band = Band::WB)
{
	State s;
	s.call = call; s.speaker = speaker; s.mute = mute; s.volume = volume; s.band = band;
	return s;
}

/* no call, with the requests that count outside a call */
static State media(Output output, bool record = false)
{
	State s = state(Call::IDLE);
	s.output = output; s.record = record;
	return s;
}

static bool has(std::string const &t, std::string const &line)
{
	return ("\n" + t).find("\n" + line + "\n") != std::string::npos;
}

static long pos(std::string const &t, std::string const &line)
{
	size_t const p = ("\n" + t).find("\n" + line + "\n");
	return p == std::string::npos ? -1 : long(p);
}

/* every line present, in the given order */
static bool ordered(std::string const &t, std::vector<std::string> const &lines)
{
	long last = -1;
	for (std::string const &l : lines) {
		long const p = pos(t, l);
		if (p < 0 || p <= last) {
			std::printf("     order broken at '%s'\n", l.c_str());
			return false;
		}
		last = p;
	}
	return true;
}

static unsigned count(std::string const &t, char const *prefix)
{
	unsigned n = 0;
	std::string const hay = "\n" + t, needle = std::string("\n") + prefix;
	for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1))
		n++;
	return n;
}

static std::string without_line(std::string t, std::string const &line)
{
	size_t const p = ("\n" + t).find("\n" + line + "\n");
	if (p != std::string::npos)
		t.erase(p, line.size() + 1);
	return t;
}


static void test_idle_is_the_media_route()
{
	static Setup s;
	std::string const t = s.run(state(Call::IDLE));

	CHECK(s.config.route == Route::MEDIA);
	CHECK(has(t, "route media"));

	/* both amplifiers */
	CHECK(has(t, "control Main AMP Enable Switch = 1"));
	CHECK(has(t, "control R Main AMP Enable Switch = 1"));
	CHECK(has(t, "control Boost Peak Current Limit = 2.50A"));
	CHECK(has(t, "control TDM_0_RX Mixer EP2 = 1"));
	CHECK(has(t, "control Audio DSP Mode = Ambient"));

	CHECK(has(t, "playback 1 yes"));
	CHECK(has(t, "capture 8 no"));

	/* nothing of a call */
	CHECK(count(t, "stream ") == 0);
	CHECK(count(t, "tuning ") == 0);
	CHECK(count(t, "missing ") == 0);
	CHECK(count(t, "control Incall ") == 0);
	CHECK(count(t, "control Sidetone ") == 0);
	CHECK(!has(t, "control TDM_0_RX Mixer EP5 = 1"));
	CHECK(!s.config.overflow);
}

static void test_unavailable_and_incoming_equal_idle()
{
	static Setup s;
	std::string const idle = s.run(state(Call::IDLE));
	CHECK(s.run(state(Call::UNAVAILABLE)) == idle);
	CHECK(s.run(state(Call::INCOMING))    == idle);
	CHECK(s.run(State { })                == idle);   /* the initial state */
}

static void test_call_is_the_handset_route()
{
	static Setup s;
	std::string const active = s.run(state(Call::ACTIVE));

	CHECK(s.config.route == Route::HANDSET);
	CHECK(has(active, "route handset band wb"));
	CHECK(s.run(state(Call::OUTBOUND)) == active);
	CHECK(s.run(state(Call::ALERTING)) == active);

	std::string const &t = active;

	/* streams 15, 4, 11 and no other */
	CHECK(count(t, "stream ") == 3);
	CHECK(has(t, "stream 15 capture"));
	CHECK(has(t, "stream 4 playback"));
	CHECK(has(t, "stream 11 capture"));

	CHECK(has(t, "control Audio DSP Mode = Telephony"));
	CHECK(count(t, "control Audio DSP Mode") == 1);

	/* top amplifier only */
	CHECK(has(t, "control Main AMP Enable Switch = 1"));
	CHECK(has(t, "control PCM Source = ASP"));
	CHECK(count(t, "control R Main AMP Enable Switch") == 0);
	CHECK(count(t, "control R Boost Peak Current Limit") == 0);

	/* routing switches, microphone, sidetone */
	CHECK(has(t, "control NoHost1 TX Mixer TDM_0_TX = 1"));
	CHECK(has(t, "control EP4 TX Mixer INTERNAL_MIC_TX = 1"));
	CHECK(has(t, "control TDM_0_RX Mixer EP5 = 1"));
	CHECK(has(t, "control BUILDIN MIC ID CAPTURE LIST [1] = 3"));
	CHECK(has(t, "control MIC HW Gain At High Power Mode (cB) = 130"));
	CHECK(has(t, "control MIC Record Soft Gain (dB) = 0"));
	CHECK(has(t, "control Sidetone Enable = 1"));
	CHECK(has(t, "control Sidetone Biquad0 [1] = 1065353216"));

	/* media is off */
	CHECK(has(t, "playback 1 no"));
	CHECK(has(t, "capture 8 no"));
	CHECK(!has(t, "control TDM_0_RX Mixer EP2 = 1"));

	CHECK(has(t, "tuning 19 3 18 = 4"));
	CHECK(count(t, "missing ") == 0);
}

static void test_speaker_in_a_call()
{
	static Setup s;
	std::string const t = s.run(state(Call::ACTIVE, true));

	CHECK(s.config.route == Route::SPEAKER);
	CHECK(has(t, "route speaker band wb"));
	CHECK(s.run(state(Call::OUTBOUND, true)) == t);
	CHECK(s.run(state(Call::ALERTING, true)) == t);

	/* voice-speaker and voice-speaker-mic instead of the handset paths */
	CHECK(has(t, "control ASPTX1 Slot Position = 1"));
	CHECK(has(t, "control Main AMP Enable Switch = 1"));
	CHECK(has(t, "control R Main AMP Enable Switch = 1"));
	CHECK(has(t, "control BUILDIN MIC ID CAPTURE LIST [1] = 2"));
	CHECK(has(t, "control MIC HW Gain At High Power Mode (cB) = 120"));
	CHECK(has(t, "control MIC Record Soft Gain (dB) = 1"));
	CHECK(!has(t, "control PCM Source = ASP"));
	CHECK(!has(t, "control BUILDIN MIC ID CAPTURE LIST [1] = 3"));
	CHECK(!has(t, "control MIC HW Gain At High Power Mode (cB) = 130"));

	/* no sidetone */
	CHECK(count(t, "control Sidetone ") == 0);

	/* the rest as on the handset */
	CHECK(count(t, "stream ") == 3);
	CHECK(has(t, "stream 15 capture"));
	CHECK(has(t, "stream 4 playback"));
	CHECK(has(t, "stream 11 capture"));
	CHECK(has(t, "control Audio DSP Mode = Telephony"));
	CHECK(has(t, "control NoHost1 TX Mixer TDM_0_TX = 1"));
	CHECK(has(t, "control EP4 TX Mixer INTERNAL_MIC_TX = 1"));
	CHECK(has(t, "control TDM_0_RX Mixer EP5 = 1"));
	CHECK(has(t, "control Incall Mic Mute = 0"));
	CHECK(has(t, "playback 1 no"));
	CHECK(has(t, "tuning 19 3 18 = 4"));
	CHECK(count(t, "missing ") == 0);
}

static void test_requests_outside_a_call_change_nothing()
{
	static Setup s;
	for (Call call : { Call::IDLE, Call::UNAVAILABLE, Call::INCOMING }) {
		std::string const plain = s.run(state(call));
		CHECK(s.run(state(call, true))                         == plain);   /* speaker */
		CHECK(s.run(state(call, false, true))                  == plain);   /* mute */
		CHECK(s.run(state(call, false, false, 0))              == plain);   /* volume */
		CHECK(s.run(state(call, false, false, 4, Band::NB))    == plain);   /* band */
		CHECK(s.run(state(call, true, true, 6, Band::NB))      == plain);
		CHECK(s.config.route == Route::MEDIA);
	}
}

static void test_mute()
{
	static Setup s;
	std::string const unmuted = s.run(state(Call::ACTIVE, false, false));
	std::string const muted   = s.run(state(Call::ACTIVE, false, true));

	CHECK(has(unmuted, "control Incall Mic Mute = 0"));
	CHECK(has(muted,   "control Incall Mic Mute = 1"));
	CHECK(count(muted,   "control Incall Mic Mute") == 1);
	CHECK(count(unmuted, "control Incall Mic Mute") == 1);

	/* the downlink stays audible */
	CHECK(has(muted,   "control Incall Sink Mute = 0"));
	CHECK(has(unmuted, "control Incall Sink Mute = 0"));

	/* nothing else differs */
	CHECK(without_line(muted, "control Incall Mic Mute = 1")
	   == without_line(unmuted, "control Incall Mic Mute = 0"));

	std::string const speaker_muted = s.run(state(Call::ACTIVE, true, true));
	CHECK(has(speaker_muted, "control Incall Mic Mute = 1"));
}

static void test_volume()
{
	static Setup s;
	CHECK(State { }.volume == 4);
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 0)),   "tuning 19 3 18 = 0"));
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 3)),   "tuning 19 3 18 = 3"));
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 6)),   "tuning 19 3 18 = 6"));
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 7)),   "tuning 19 3 18 = 6"));
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 100)), "tuning 19 3 18 = 6"));
	CHECK(has(s.run(state(Call::ACTIVE, false, false, -1)),  "tuning 19 3 18 = 0"));
	CHECK(has(s.run(state(Call::ACTIVE, true,  false, 9)),   "tuning 19 3 18 = 6"));
	CHECK(count(s.run(state(Call::ACTIVE, false, false, 2)), "tuning ") == 1);
}

static void test_band()
{
	static Setup s;
	CHECK(State { }.band == Band::WB);
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 4, Band::NB)), "route handset band nb"));
	CHECK(has(s.run(state(Call::ACTIVE, false, false, 4, Band::WB)), "route handset band wb"));
	CHECK(has(s.run(state(Call::ACTIVE, true,  false, 4, Band::NB)), "route speaker band nb"));
	CHECK(s.config.band == Band::NB);

	/* the band changes nothing but the recorded attribute */
	std::string const nb = s.run(state(Call::ACTIVE, false, false, 4, Band::NB));
	std::string const wb = s.run(state(Call::ACTIVE, false, false, 4, Band::WB));
	CHECK(without_line(nb, "route handset band nb") == without_line(wb, "route handset band wb"));

	CHECK(band_from_string("nb")    == Band::NB);
	CHECK(band_from_string("wb")    == Band::WB);
	CHECK(band_from_string("")      == Band::WB);   /* attribute absent */
	CHECK(band_from_string(nullptr) == Band::WB);
	CHECK(band_from_string("swb")   == Band::WB);
	CHECK(std::strcmp(band_name(Band::NB), "nb") == 0);
	CHECK(std::strcmp(band_name(Band::WB), "wb") == 0);
}

static void test_call_names()
{
	CHECK(call_from_string("unavailable") == Call::UNAVAILABLE);
	CHECK(call_from_string("idle")        == Call::IDLE);
	CHECK(call_from_string("incoming")    == Call::INCOMING);
	CHECK(call_from_string("outbound")    == Call::OUTBOUND);
	CHECK(call_from_string("alerting")    == Call::ALERTING);
	CHECK(call_from_string("active")      == Call::ACTIVE);
	CHECK(call_from_string("")            == Call::UNAVAILABLE);
	CHECK(call_from_string(nullptr)       == Call::UNAVAILABLE);
	CHECK(call_from_string("activex")     == Call::UNAVAILABLE);
	CHECK(call_from_string("act")         == Call::UNAVAILABLE);

	CHECK(route(state(Call::IDLE, true))     == Route::MEDIA);
	CHECK(route(state(Call::INCOMING, true)) == Route::MEDIA);
	CHECK(route(state(Call::ACTIVE))         == Route::HANDSET);
	CHECK(route(state(Call::ALERTING, true)) == Route::SPEAKER);
}

static void test_missing_path()
{
	static Setup full, partial("voice-handset-mic");

	std::string const want = full.run(state(Call::ACTIVE));
	std::string const got  = partial.run(state(Call::ACTIVE));

	/* reported */
	CHECK(count(got, "missing ") == 1);
	CHECK(has(got, "missing voice-handset-mic"));

	/* skipped: the controls of the path are gone, everything else is as before */
	std::string expect = want;
	expect = without_line(expect, "control BUILDIN MIC ID CAPTURE LIST [0] = 0");
	expect = without_line(expect, "control BUILDIN MIC ID CAPTURE LIST [1] = 3");
	expect = without_line(expect, "control MIC HW Gain At High Power Mode (cB) = 130");
	CHECK(without_line(got, "missing voice-handset-mic") == expect);

	/* a path of another route is not missed */
	static Setup no_speaker("voice-speaker");
	CHECK(count(no_speaker.run(state(Call::ACTIVE)), "missing ") == 0);
	CHECK(count(no_speaker.run(state(Call::IDLE)),   "missing ") == 0);
	std::string const sp = no_speaker.run(state(Call::ACTIVE, true));
	CHECK(count(sp, "missing ") == 1);
	CHECK(has(sp, "missing voice-speaker"));
	CHECK(has(sp, "control TDM_0_RX Mixer EP5 = 1"));

	/* media */
	static Setup no_media("low-latency-playbackP");
	std::string const m = no_media.run(state(Call::IDLE));
	CHECK(has(m, "missing low-latency-playbackP"));
	CHECK(has(m, "control Main AMP Enable Switch = 1"));
	CHECK(has(m, "playback 1 yes"));
}

static void test_empty_table()
{
	static Paths  paths;
	static Config config;

	generate(state(Call::ACTIVE, false, true, 2), Settings { }, paths, config);
	std::string const t = text(config);

	/* every path of the route is reported, once, in the order of use */
	CHECK(count(t, "missing ") == 9);
	CHECK(ordered(t, { "missing cs35l41-load-protection-firmware-start",
	                   "missing cs35l41-load-protection-firmware-end",
	                   "missing voice-handset", "missing hostless-ulC spk-vi",
	                   "missing voice-handset-mic", "missing sidetone-for handset",
	                   "missing voice-callC", "missing voice-handset-mic-post",
	                   "missing voice-callP" }));

	/* what does not come from the table is still there */
	CHECK(count(t, "default ") == 0);
	CHECK(has(t, "control Audio DSP Mode = Telephony"));
	CHECK(has(t, "control Incall Mic Mute = 1"));
	CHECK(has(t, "control Incall Sink Mute = 0"));
	CHECK(count(t, "stream ") == 3);
	CHECK(has(t, "tuning 19 3 18 = 2"));
	CHECK(has(t, "playback 1 no"));

	generate(state(Call::IDLE), Settings { }, paths, config);
	std::string const m = text(config);
	CHECK(count(m, "missing ") == 4);
	CHECK(has(m, "missing cs35l41-load-protection-firmware-start"));
	CHECK(has(m, "missing cs35l41-load-protection-firmware-end"));
	CHECK(has(m, "missing speaker"));
	CHECK(has(m, "missing low-latency-playbackP"));
	CHECK(has(m, "playback 1 yes"));

	generate(state(Call::ACTIVE, true), Settings { }, paths, config);
	std::string const sp = text(config);
	CHECK(count(sp, "missing ") == 8);
	CHECK(has(sp, "missing voice-speaker"));
	CHECK(has(sp, "missing voice-speaker-mic"));
	CHECK(has(sp, "missing voice-speaker-mic-post"));
	CHECK(!has(sp, "missing sidetone-for handset"));

	/* without the amplifier DSP, the firmware paths are not asked for */
	generate(state(Call::ACTIVE, true), asp(), paths, config);
	std::string const sp_asp = text(config);
	CHECK(count(sp_asp, "missing ") == 6);
	CHECK(count(sp_asp, "missing cs35l41") == 0);
	CHECK(has(sp_asp, "control AMP PCM Gain = 6"));
	CHECK(has(sp_asp, "control R PCM Source = ASP"));

	generate(state(Call::IDLE), asp(), paths, config);
	CHECK(count(text(config), "missing ") == 2);
}

static void test_deterministic()
{
	static Setup a, b;

	for (Settings const &settings : { Settings { }, asp(), asp(11) })
		for (Call call : { Call::IDLE, Call::ACTIVE })
			for (bool speaker : { false, true }) {
				State const st = state(call, speaker, true, 5, Band::NB);

				std::string const first = a.run(st, settings);
				CHECK(a.run(st, settings) == first);   /* same objects, twice */
				CHECK(b.run(st, settings) == first);   /* a table built separately */

				/* a config that held another state and mode before */
				a.run(state(Call::ACTIVE, !speaker, false, 1));
				a.run(state(Call::IDLE), asp(3));
				CHECK(a.run(st, settings) == first);
			}
}

static void test_order_of_a_handset_call()
{
	static Setup s;
	std::string const t = s.run(state(Call::ACTIVE));

	/* the order of the LineageOS HAL */
	CHECK(ordered(t, {
		"control Boost Peak Current Limit = 3.50A",          /* voice-handset */
		"control Main AMP Enable Switch = 1",
		"control NoHost1 TX Mixer TDM_0_TX = 1",             /* hostless-ulC spk-vi */
		"stream 15 capture",
		"control BUILDIN MIC ID CAPTURE LIST [0] = 0",       /* voice-handset-mic */
		"control MIC HW Gain At High Power Mode (cB) = 130",
		"control Audio DSP Mode = Telephony",
		"control Sidetone Biquad0 [0] = 1065353216",         /* sidetone-for handset */
		"control Sidetone Volume = -90",
		"control Sidetone Enable = 1",
		"control EP4 TX Mixer INTERNAL_MIC_TX = 1",          /* voice-callC */
		"control MIC Record Soft Gain (dB) = 0",             /* voice-handset-mic-post */
		"control TDM_0_RX Mixer EP5 = 1",                    /* voice-callP */
		"control Incall Mic Mute = 0",
		"control Incall Sink Mute = 0",
		"stream 4 playback",
		"stream 11 capture",
		"tuning 19 3 18 = 4",
		"playback 1 no",
		"capture 8 no" }));
}

static void test_order_of_a_speaker_call()
{
	static Setup s;
	std::string const t = s.run(state(Call::ACTIVE, true));

	CHECK(ordered(t, {
		"control ASPTX1 Slot Position = 1",                  /* voice-speaker */
		"control NoHost1 TX Mixer TDM_0_TX = 1",
		"stream 15 capture",
		"control MIC HW Gain At High Power Mode (cB) = 120", /* voice-speaker-mic */
		"control Audio DSP Mode = Telephony",
		"control EP4 TX Mixer INTERNAL_MIC_TX = 1",
		"control MIC Record Soft Gain (dB) = 1",             /* voice-speaker-mic-post */
		"control TDM_0_RX Mixer EP5 = 1",
		"control Incall Mic Mute = 0",
		"control Incall Sink Mute = 0",
		"stream 4 playback",
		"stream 11 capture",
		"tuning 19 3 18 = 4",
		"playback 1 no" }));
}

static void test_order_of_media()
{
	static Setup s;
	std::string const t = s.run(state(Call::IDLE));

	/* report 02, section 2 (i): amplifier path, routing switch, then the PCM */
	CHECK(ordered(t, {
		"control Audio DSP Mode = Ambient",
		"control Boost Peak Current Limit = 2.50A",          /* speaker */
		"control R Boost Peak Current Limit = 2.50A",
		"control Main AMP Enable Switch = 1",
		"control R Main AMP Enable Switch = 1",
		"control TDM_0_RX Mixer EP2 = 1",                    /* low-latency-playbackP */
		"playback 1 yes",
		"capture 8 no" }));
}

static void test_defaults_lead_every_config()
{
	static Setup s;

	std::string const head =
		"default TDM_0_RX Chan = Four\n"
		"default Main AMP Enable Switch = 0\n"
		"default R Main AMP Enable Switch = 0\n"
		"default BUILDIN MIC ID CAPTURE LIST [0] = -1\n"
		"default BUILDIN MIC ID CAPTURE LIST [1] = -1\n"
		"default Incall Mic Mute = 0\n";

	CHECK(s.run(state(Call::IDLE)).find("route media\n" + head) == 0);
	CHECK(s.run(state(Call::ACTIVE)).find("route handset band wb\n" + head) == 0);
	CHECK(s.run(state(Call::ACTIVE, true)).find("route speaker band wb\n" + head) == 0);

	/* the complete list, also for controls the route sets */
	CHECK(count(s.run(state(Call::ACTIVE)), "default ") == 6);
	CHECK(count(s.run(state(Call::IDLE)),   "default ") == 6);
}

/*
 * Amplifier mode
 */

struct Route_case { State st; char const *header; char const *first; };

/* one state per route, with the first action of the route itself */
static std::vector<Route_case> const &route_cases()
{
	static std::vector<Route_case> const cases {
		{ state(Call::IDLE),         "route media",           "control Audio DSP Mode = Ambient" },
		{ state(Call::ACTIVE),       "route handset band wb", "control Boost Peak Current Limit = 3.50A" },
		{ state(Call::ACTIVE, true), "route speaker band wb", "control ASPTX1 Slot Position = 1" } };
	return cases;
}

/*
 * Without the amplifier firmware the source of both amplifiers must stay ASP:
 * the driver reverts a control the next config does not name to its initial
 * value (source DSP), and an amplifier that is on with source DSP makes the
 * codec driver power the DSP and ask for its firmware (device, 2026-10-03:
 * speaker -> earpiece stopped the audio driver). So in mode ASP every route
 * names both sources, after the paths of the route.
 */
static void test_every_asp_route_names_both_amplifier_sources()
{
	static Setup s;

	for (Route_case const &c : route_cases()) {
		std::string const t = s.run(c.st, asp());
		CHECK(has(t, "control PCM Source = ASP"));
		CHECK(has(t, "control R PCM Source = ASP"));
		CHECK(count(t, "control R PCM Source") == 1);
		CHECK(count(t, "control PCM Source = DSP") == 0);
		CHECK(count(t, "control R PCM Source = DSP") == 0);
	}

	State const earpiece = media(Output::EARPIECE);
	std::string const e = s.run(earpiece, asp());
	CHECK(ordered(e, { "control Main AMP Enable Switch = 1",   /* handset */
	                   "control R PCM Source = ASP",
	                   "playback 1 yes" }));

	/* mode dsp is as before: the bottom amplifier's source is not named */
	CHECK(count(s.run(earpiece), "control R PCM Source") == 0);
}


static std::string const firmware_lines =
	"control DSP Booted = 0\n"
	"control R DSP Booted = 0\n"
	"control DSP1 Preload Switch = 1\n"
	"control R DSP1 Preload Switch = 1\n";

static void test_dsp_mode_loads_the_amplifier_firmware()
{
	static Setup s;

	/* the vendor's behaviour is the default */
	CHECK(Settings { }.amp_mode == Amp_mode::DSP);

	for (Route_case const &c : route_cases()) {
		std::string const t = s.run(c.st);

		CHECK(has(t, c.header));
		CHECK(s.config.amp_mode == Amp_mode::DSP);

		/* after the last default, before the first action of the route */
		CHECK(t.find("default Incall Mic Mute = 0\n" + firmware_lines + c.first + "\n")
		      != std::string::npos);

		/*
		 * Each control once, with the value it has after both paths: a
		 * later config repeats the values, so the driver skips them and
		 * the firmware stays loaded.
		 */
		CHECK(count(t, "control DSP Booted")            == 1);
		CHECK(count(t, "control R DSP Booted")          == 1);
		CHECK(count(t, "control DSP1 Preload Switch")   == 1);
		CHECK(count(t, "control R DSP1 Preload Switch") == 1);
		CHECK(count(t, "missing ") == 0);

		/* an explicit 'dsp' is the default, the gain of the other mode has no effect */
		Settings dsp;
		dsp.amp_mode = Amp_mode::DSP;
		dsp.asp_gain = 3;
		CHECK(s.run(c.st, dsp) == t);
	}

	/* the loudspeaker routes leave the amplifiers on their DSP ... */
	for (State const &st : { state(Call::IDLE), state(Call::ACTIVE, true) }) {
		std::string const t = s.run(st);
		CHECK(count(t, "control PCM Source")     == 0);
		CHECK(count(t, "control R PCM Source")   == 0);
		CHECK(count(t, "control AMP PCM Gain")   == 0);
		CHECK(count(t, "control R AMP PCM Gain") == 0);
	}

	/* ... the handset takes what its path says */
	std::string const h = s.run(state(Call::ACTIVE));
	CHECK(has(h, "control PCM Source = ASP"));
	CHECK(has(h, "control AMP PCM Gain = 7"));
	CHECK(count(h, "control PCM Source") == 1);
}

static void test_firmware_paths_are_merged()
{
	static Paths  p;
	static Config c;

	/* indexed and whole-control writes, a control only the first path has */
	add(p, "cs35l41-load-protection-firmware-start", {
		{ "A",  -1, "0" },      /* overridden */
		{ "B",   0, "1" },      /* element 0 stays, element 1 is overridden */
		{ "B",   1, "2" },
		{ "C",   0, "3" },      /* overridden by a write of the whole control */
		{ "D",  -1, "4" },      /* an element of it is written later: both stay */
		{ "E",  -1, "5" } });   /* not in the second path */
	add(p, "cs35l41-load-protection-firmware-end", {
		{ "A",  -1, "1" },
		{ "B",   1, "7" },
		{ "C",  -1, "8" },
		{ "D",   2, "9" } });

	generate(state(Call::IDLE), Settings { }, p, c);
	std::string const t = text(c);

	CHECK(t.find("route media\n"
	             "control B [0] = 1\n"
	             "control D = 4\n"
	             "control E = 5\n"
	             "control A = 1\n"
	             "control B [1] = 7\n"
	             "control C = 8\n"
	             "control D [2] = 9\n"
	             "control Audio DSP Mode = Ambient\n") == 0);
}

static void test_missing_firmware_path()
{
	static Setup full, no_start("cs35l41-load-protection-firmware-start"),
	                   no_end  ("cs35l41-load-protection-firmware-end");

	for (Route_case const &c : route_cases()) {

		/* the other path is applied as it is, the route is complete */
		std::string const e = no_end.run(c.st);
		CHECK(count(e, "missing ") == 1);
		CHECK(has(e, "missing cs35l41-load-protection-firmware-end"));
		CHECK(e.find("default Incall Mic Mute = 0\n"
		             "control DSP Booted = 0\n"
		             "control R DSP Booted = 0\n"
		             "control DSP1 Preload Switch = 0\n"
		             "control R DSP1 Preload Switch = 0\n"
		             + std::string(c.first) + "\n") != std::string::npos);
		CHECK(count(e, "control DSP1 Preload Switch") == 1);

		std::string const b = no_start.run(c.st);
		CHECK(count(b, "missing ") == 1);
		CHECK(has(b, "missing cs35l41-load-protection-firmware-start"));
		CHECK(b.find("default Incall Mic Mute = 0\n"
		             "control DSP1 Preload Switch = 1\n"
		             "control R DSP1 Preload Switch = 1\n"
		             + std::string(c.first) + "\n") != std::string::npos);
		CHECK(count(b, "control DSP Booted") == 0);

		/* nothing else differs from the complete table */
		std::string want = full.run(c.st);
		want = without_line(want, "control DSP Booted = 0");
		want = without_line(want, "control R DSP Booted = 0");
		CHECK(without_line(b, "missing cs35l41-load-protection-firmware-start") == want);

		/* not missed where it is not used */
		CHECK(count(no_end.run(c.st, asp()),   "missing ") == 0);
		CHECK(count(no_start.run(c.st, asp()), "missing ") == 0);
	}
}

static void test_asp_mode_bypasses_the_amplifier_dsp()
{
	static Setup s;

	std::string const asp_lines =
		"control AMP PCM Gain = 6\n"
		"control R AMP PCM Gain = 6\n"
		"control PCM Source = ASP\n"
		"control R PCM Source = ASP\n";

	/* no firmware in any route */
	for (Route_case const &c : route_cases()) {
		std::string const t = s.run(c.st, asp());
		CHECK(has(t, c.header));
		CHECK(s.config.amp_mode == Amp_mode::ASP);
		CHECK(count(t, "control DSP Booted")            == 0);
		CHECK(count(t, "control R DSP Booted")          == 0);
		CHECK(count(t, "control DSP1 Preload Switch")   == 0);
		CHECK(count(t, "control R DSP1 Preload Switch") == 0);
		CHECK(count(t, "missing ") == 0);
		CHECK(t.find(std::string("default Incall Mic Mute = 0\n") + c.first + "\n")
		      != std::string::npos);
	}

	/* media: after the paths of the route, before the PCM */
	std::string const m = s.run(state(Call::IDLE), asp());
	CHECK(m.find("control R Main AMP Enable Switch = 1\n"    /* speaker */
	             "control TDM_0_RX Mixer EP2 = 1\n"          /* low-latency-playbackP */
	             + asp_lines +
	             "playback 1 yes\n") != std::string::npos);

	/* nothing else differs from the dsp mode */
	std::string const m_dsp = s.run(state(Call::IDLE));
	CHECK(m.size() && m_dsp.size());
	{
		std::string a = m, d = m_dsp;
		a.erase(a.find(asp_lines), asp_lines.size());
		d.erase(d.find(firmware_lines), firmware_lines.size());
		CHECK(a == d);
	}

	/* speakerphone: after the last path, before the voice PCMs are opened */
	std::string const sp = s.run(state(Call::ACTIVE, true), asp());
	CHECK(sp.find("control TDM_0_RX Mixer EP5 = 1\n"         /* voice-callP */
	              + asp_lines +
	              "control Incall Mic Mute = 0\n") != std::string::npos);
	CHECK(ordered(sp, { "control ASPTX1 Slot Position = 1", "control PCM Source = ASP",
	                    "stream 4 playback", "stream 11 capture" }));
	{
		std::string a = sp, d = s.run(state(Call::ACTIVE, true));
		a.erase(a.find(asp_lines), asp_lines.size());
		d.erase(d.find(firmware_lines), firmware_lines.size());
		CHECK(a == d);
	}

	/*
	 * handset: the path of the table, plus the two sources (they are named
	 * by every config of mode asp so that the driver never reverts them to
	 * DSP); the bottom amplifier stays off and keeps its gain
	 */
	std::string const source_lines =
		"control PCM Source = ASP\n"
		"control R PCM Source = ASP\n";

	std::string const h = s.run(state(Call::ACTIVE), asp());
	{
		std::string d = s.run(state(Call::ACTIVE));
		d.erase(d.find(firmware_lines), firmware_lines.size());
		std::string a = h;
		a.erase(a.find(source_lines), source_lines.size());
		CHECK(a == d);
	}
	CHECK(has(h, "control PCM Source = ASP"));
	CHECK(has(h, "control AMP PCM Gain = 7"));
	CHECK(count(h, "control PCM Source")     == 2);   /* path and policy */
	CHECK(count(h, "control AMP PCM Gain")   == 1);
	CHECK(count(h, "control R PCM Source")   == 1);
	CHECK(count(h, "control R AMP PCM Gain") == 0);
	CHECK(count(h, "control R Main AMP Enable Switch") == 0);
}

static void test_asp_gain()
{
	static Setup s;

	/* the vendor's 'handset' value by default, limits of the kernel control */
	CHECK(DEFAULT_ASP_GAIN == 6);
	CHECK(MAX_ASP_GAIN == 20);
	CHECK(Settings { }.asp_gain == 6);

	struct Case { int gain; char const *text; };
	for (Case const &c : { Case { 0, "0" }, { 1, "1" }, { 6, "6" }, { 9, "9" },
	                       { 10, "10" }, { 17, "17" }, { 20, "20" },
	                       { 21, "20" }, { 1000, "20" }, { -1, "0" }, { -1000, "0" } })
		for (State const &st : { state(Call::IDLE), state(Call::ACTIVE, true) }) {
			std::string const t = s.run(st, asp(c.gain));
			CHECK(has(t, std::string("control AMP PCM Gain = ")   + c.text));
			CHECK(has(t, std::string("control R AMP PCM Gain = ") + c.text));
			CHECK(count(t, "control AMP PCM Gain")   == 1);
			CHECK(count(t, "control R AMP PCM Gain") == 1);
		}

	/* every value of the range has its text */
	for (int gain = 0; gain <= MAX_ASP_GAIN; gain++)
		CHECK(has(s.run(state(Call::IDLE), asp(gain)),
		          "control AMP PCM Gain = " + std::to_string(gain)));
}

static void test_asp_writes_follow_the_paths()
{
	static Paths  p;
	static Config c;

	/* a table whose loudspeaker path sets the controls itself */
	add(p, "speaker", {
		{ "PCM Source",             -1, "DSP" },
		{ "AMP PCM Gain",           -1, "17" },
		{ "Main AMP Enable Switch", -1, "1" } });
	add(p, "low-latency-playbackP", {
		{ "R AMP PCM Gain",     -1, "17" },
		{ "TDM_0_RX Mixer EP2", -1, "1" } });

	generate(state(Call::IDLE), asp(), p, c);
	std::string const t = text(c);

	/* the later write counts */
	CHECK(ordered(t, { "control PCM Source = DSP", "control PCM Source = ASP" }));
	CHECK(ordered(t, { "control AMP PCM Gain = 17", "control AMP PCM Gain = 6" }));
	CHECK(ordered(t, { "control R AMP PCM Gain = 17", "control R AMP PCM Gain = 6" }));
	CHECK(ordered(t, { "control Main AMP Enable Switch = 1", "control AMP PCM Gain = 6",
	                   "playback 1 yes" }));
}

static void test_amp_mode_names()
{
	CHECK(amp_mode_from_string("asp")   == Amp_mode::ASP);
	CHECK(amp_mode_from_string("dsp")   == Amp_mode::DSP);
	CHECK(amp_mode_from_string("")      == Amp_mode::DSP);   /* attribute absent */
	CHECK(amp_mode_from_string(nullptr) == Amp_mode::DSP);
	CHECK(amp_mode_from_string("ASP")   == Amp_mode::DSP);
	CHECK(amp_mode_from_string("aspx")  == Amp_mode::DSP);
	CHECK(std::strcmp(amp_mode_name(Amp_mode::ASP), "asp") == 0);
	CHECK(std::strcmp(amp_mode_name(Amp_mode::DSP), "dsp") == 0);
}


/*
 * Media volume: factor of the playback mixes in record_play_mixer
 */

static bool near(double a, double b)
{
	double const d = a - b;
	return d < 1e-12 && d > -1e-12;
}

static void test_media_volume_factor()
{
	CHECK(MAX_MEDIA_VOLUME == 15);
	CHECK(DEFAULT_MEDIA_VOLUME == 8);

	/* exact at both ends */
	for (double max : { 1.0, 0.05, 0.3, 0.7 }) {
		CHECK(media_volume_factor(0, max)  == 0.0);
		CHECK(media_volume_factor(15, max) == max);
	}

	/* strictly rising, never above the maximum */
	for (double max : { 1.0, 0.05, 0.3 })
		for (int step = 1; step <= 15; step++) {
			CHECK(media_volume_factor(step, max) > media_volume_factor(step - 1, max));
			CHECK(media_volume_factor(step, max) <= max);
		}

	/* quadratic: max * (step/15)^2 */
	CHECK(near(media_volume_factor(8,  1.0),  64.0/225.0));
	CHECK(near(media_volume_factor(1,  1.0),   1.0/225.0));
	CHECK(near(media_volume_factor(14, 1.0), 196.0/225.0));
	CHECK(near(media_volume_factor(5,  0.9), 0.1));
	CHECK(near(media_volume_factor(3,  0.05), 0.002));

	/* steps outside the range are clamped */
	CHECK(media_volume_factor(-1,   1.0) == 0.0);
	CHECK(media_volume_factor(-100, 0.5) == 0.0);
	CHECK(media_volume_factor(16,   1.0) == 1.0);
	CHECK(media_volume_factor(1000, 0.5) == 0.5);

	/* the maximum is a factor 0..1: never an amplification */
	CHECK(media_volume_factor(15, 2.0)  == 1.0);
	CHECK(media_volume_factor(15, -1.0) == 0.0);
	CHECK(media_volume_factor(8,  7.0)  == media_volume_factor(8, 1.0));
	for (int step = 0; step <= 15; step++)
		CHECK(media_volume_factor(step, 0.0) == 0.0);
}

static void test_media_is_muted_in_a_call()
{
	/* no call: the factor of the volume step */
	for (Call call : { Call::UNAVAILABLE, Call::IDLE, Call::INCOMING })
		for (bool speaker : { false, true })
			for (int step : { -3, 0, 1, 8, 15, 20 }) {
				CHECK(media_factor(state(call, speaker), step, 1.0)
				      == media_volume_factor(step, 1.0));
				CHECK(media_factor(state(call, speaker), step, 0.05)
				      == media_volume_factor(step, 0.05));
			}

	CHECK(media_factor(State { }, DEFAULT_MEDIA_VOLUME, 1.0) > 0.0);

	/* call: muted, whatever the step and the requests */
	for (Call call : { Call::OUTBOUND, Call::ALERTING, Call::ACTIVE })
		for (bool speaker : { false, true })
			for (int step : { 0, 8, 15, 20 })
				CHECK(media_factor(state(call, speaker, true, 2, Band::NB), step, 1.0) == 0.0);
}

static void test_paths_table()
{
	static Paths p;

	CHECK(p.lookup("a") == nullptr);
	CHECK(p.defaults().count == 0);

	/* writes before any section are dropped */
	CHECK(!p.add_write("X", -1, "1"));

	CHECK(p.begin_path("a"));
	CHECK(p.add_write("A", -1, "1"));
	CHECK(p.add_write("L", 2, "No MIC"));
	CHECK(p.begin_path("b c"));
	p.begin_defaults();
	CHECK(p.add_write("D", -1, "0"));
	CHECK(p.begin_path("d"));
	CHECK(p.add_write("B", -1, "2"));

	/* the first definition of a name counts, the second one is dropped */
	CHECK(!p.begin_path("a"));
	CHECK(!p.add_write("Z", -1, "9"));
	CHECK(p.duplicates() == 1);

	/* defaults continued */
	p.begin_defaults();
	CHECK(p.add_write("E", 1, "-3"));

	Path const *a = p.lookup("a");
	CHECK(a && a->count == 2);
	if (a) {
		CHECK(std::strcmp(p.write(*a, 0).control, "A") == 0);
		CHECK(std::strcmp(p.write(*a, 0).value,   "1") == 0);
		CHECK(p.write(*a, 0).index == -1);
		CHECK(std::strcmp(p.write(*a, 1).control, "L") == 0);
		CHECK(std::strcmp(p.write(*a, 1).value,   "No MIC") == 0);
		CHECK(p.write(*a, 1).index == 2);
	}
	Path const *bc = p.lookup("b c");
	CHECK(bc && bc->count == 0);
	Path const *d = p.lookup("d");
	CHECK(d && d->count == 1);
	CHECK(p.lookup("b") == nullptr);
	CHECK(p.lookup("defaults") == nullptr);

	CHECK(p.defaults().count == 2);
	CHECK(std::strcmp(p.write(p.defaults(), 0).control, "D") == 0);
	CHECK(std::strcmp(p.write(p.defaults(), 1).control, "E") == 0);
	CHECK(p.write(p.defaults(), 1).index == 1);
	CHECK(p.path_count() == 3);
	CHECK(!p.overflow());

	/* names given with a length need no terminator */
	CHECK(p.begin_path("exyz", 1));
	CHECK(p.add_write("Fxyz", 1, 7, "12345", 2));
	Path const *e = p.lookup("e");
	CHECK(e && e->count == 1);
	if (e) {
		CHECK(std::strcmp(p.write(*e, 0).control, "F") == 0);
		CHECK(std::strcmp(p.write(*e, 0).value,   "12") == 0);
	}

	p.clear();
	CHECK(p.lookup("a") == nullptr);
	CHECK(p.path_count() == 0);
	CHECK(p.defaults().count == 0);
	CHECK(p.duplicates() == 0);
}

static void test_paths_table_limits()
{
	static Paths p;

	/* more writes than the table holds: flagged, the rest stays usable */
	CHECK(p.begin_path("big"));
	unsigned added = 0;
	for (unsigned i = 0; i < MAX_WRITES + 10; i++)
		if (p.add_write("C", int(i), "1"))
			added++;
	CHECK(added == MAX_WRITES);
	CHECK(p.overflow());
	Path const *big = p.lookup("big");
	CHECK(big && big->count == MAX_WRITES);

	p.clear();
	CHECK(!p.overflow());

	/* more paths than the table holds */
	unsigned paths = 0;
	for (unsigned i = 0; i < MAX_PATHS + 5; i++) {
		std::string const name = "p" + std::to_string(i);
		if (p.begin_path(name.c_str()))
			paths++;
	}
	CHECK(paths == MAX_PATHS);
	CHECK(p.overflow());
	CHECK(p.lookup("p0") != nullptr);
	CHECK(p.lookup(("p" + std::to_string(MAX_PATHS)).c_str()) == nullptr);

	/* strings that exceed the pool */
	p.clear();
	CHECK(p.begin_path("long"));
	std::string const big_value(1000, 'v');
	unsigned stored = 0;
	for (unsigned i = 0; i < MAX_WRITES; i++)
		if (p.add_write("C", int(i), big_value.c_str()))
			stored++;
	CHECK(stored > 0 && stored < MAX_WRITES);
	CHECK(p.overflow());
	Path const *l = p.lookup("long");
	CHECK(l && l->count == stored);
	if (l && stored)
		CHECK(big_value == p.write(*l, stored - 1).value);
}

static void test_a_table_as_big_as_possible_fits_a_config()
{
	static Paths  p;
	static Config c;

	/* all writes in paths the handset route uses */
	p.begin_defaults();
	for (unsigned i = 0; i < MAX_WRITES/2; i++) p.add_write("D", int(i), "0");
	CHECK(p.begin_path("voice-handset"));
	for (unsigned i = 0; i < MAX_WRITES/2; i++) p.add_write("C", int(i), "1");
	CHECK(!p.overflow());

	generate(state(Call::ACTIVE), Settings { }, p, c);
	CHECK(!c.overflow);
	CHECK(c.count > MAX_WRITES);
	CHECK(c.actions[c.count - 1].type == Type::MISSING);

	generate(state(Call::ACTIVE, true), asp(), p, c);
	CHECK(!c.overflow);
}

static void test_names_seen()
{
	static Names_seen seen;

	CHECK(seen.first_time("voice-callP"));
	CHECK(!seen.first_time("voice-callP"));
	CHECK(seen.first_time("voice-callC"));
	CHECK(!seen.first_time("voice-callC"));
	CHECK(!seen.first_time("voice-callP"));

	/* by content, not by pointer */
	std::string const copy = "voice-callP";
	CHECK(!seen.first_time(copy.c_str()));

	seen.clear();
	CHECK(seen.first_time("voice-callP"));

	/* full: an unknown name is reported as new every time, never lost */
	seen.clear();
	for (unsigned i = 0; i < Names_seen::MAX; i++)
		CHECK(seen.first_time(("n" + std::to_string(i)).c_str()));
	CHECK(seen.first_time("one more"));
	CHECK(seen.first_time("one more"));
	CHECK(!seen.first_time("n0"));
}

static void test_state_compare()
{
	CHECK(state(Call::ACTIVE, true, true, 3, Band::NB) == state(Call::ACTIVE, true, true, 3, Band::NB));
	CHECK(state(Call::ACTIVE) != state(Call::IDLE));
	CHECK(state(Call::ACTIVE) != state(Call::ACTIVE, true));
	CHECK(state(Call::ACTIVE) != state(Call::ACTIVE, false, true));
	CHECK(state(Call::ACTIVE) != state(Call::ACTIVE, false, false, 5));
	CHECK(state(Call::ACTIVE) != state(Call::ACTIVE, false, false, 4, Band::NB));
}

static void test_normalized()
{
	/* states with the same config are equal after normalisation */
	CHECK(normalized(state(Call::IDLE, true, true, 0, Band::NB)) == normalized(state(Call::UNAVAILABLE)));
	CHECK(normalized(state(Call::INCOMING, true)) == normalized(State { }));
	CHECK(normalized(state(Call::OUTBOUND)) == normalized(state(Call::ACTIVE)));
	CHECK(normalized(state(Call::ALERTING, true)) == normalized(state(Call::ACTIVE, true)));
	CHECK(normalized(state(Call::ACTIVE, false, false, 9)) == normalized(state(Call::ACTIVE, false, false, 6)));
	CHECK(normalized(state(Call::ACTIVE, false, false, -2)) == normalized(state(Call::ACTIVE, false, false, 0)));

	/* states with different configs stay different */
	State const active = normalized(state(Call::ACTIVE));
	CHECK(active != normalized(state(Call::IDLE)));
	CHECK(active != normalized(state(Call::ACTIVE, true)));
	CHECK(active != normalized(state(Call::ACTIVE, false, true)));
	CHECK(active != normalized(state(Call::ACTIVE, false, false, 3)));
	CHECK(active != normalized(state(Call::ACTIVE, false, false, 4, Band::NB)));

	/* the config does not change by it */
	static Setup s;
	for (Call call : { Call::UNAVAILABLE, Call::IDLE, Call::INCOMING, Call::OUTBOUND,
	                   Call::ALERTING, Call::ACTIVE })
		for (bool speaker : { false, true })
			for (int volume : { -1, 0, 5, 77 }) {
				State const st = state(call, speaker, !speaker, volume, Band::NB);
				std::string const direct = s.run(st);
				CHECK(s.run(normalized(st)) == direct);
				CHECK(normalized(normalized(st)) == normalized(st));
			}
}


/*
 * Output of media playback: both loudspeakers or the earpiece
 */

static std::string const handset_lines =
	"control PCM Source = ASP\n"
	"control AMP PCM Gain = 6\n"
	"control Main AMP Enable Switch = 1\n";

static void test_output_names()
{
	CHECK(output_from_string("earpiece") == Output::EARPIECE);
	CHECK(output_from_string("speaker")  == Output::SPEAKER);
	CHECK(output_from_string("")         == Output::SPEAKER);   /* attribute absent */
	CHECK(output_from_string(nullptr)    == Output::SPEAKER);
	CHECK(output_from_string("EARPIECE") == Output::SPEAKER);
	CHECK(output_from_string("earpiecex") == Output::SPEAKER);
	CHECK(output_from_string("handset")  == Output::SPEAKER);
	CHECK(std::strcmp(output_name(Output::SPEAKER),  "speaker")  == 0);
	CHECK(std::strcmp(output_name(Output::EARPIECE), "earpiece") == 0);

	/* the loudspeakers and no recording unless asked for */
	CHECK(State { }.output == Output::SPEAKER);
	CHECK(State { }.record == false);
	CHECK(Config { }.output == Output::SPEAKER);
	CHECK(Config { }.record == false);
}

static void test_earpiece_output()
{
	static Setup s;
	std::string const t = s.run(media(Output::EARPIECE));

	CHECK(s.config.route  == Route::MEDIA);
	CHECK(s.config.output == Output::EARPIECE);
	CHECK(has(t, "route media output earpiece"));

	/* the vendor's 'handset' path instead of 'speaker', then the playback route */
	CHECK(t.find("control Audio DSP Mode = Ambient\n"
	             + handset_lines +
	             "control TDM_0_RX Mixer EP2 = 1\n"
	             "playback 1 yes\n"
	             "capture 8 no\n") != std::string::npos);
	CHECK(!has(t, "control Boost Peak Current Limit = 2.50A"));

	/* the bottom amplifier is not named: it stays off through its default */
	CHECK(has(t, "default R Main AMP Enable Switch = 0"));
	CHECK(count(t, "control R Main AMP Enable Switch")   == 0);
	CHECK(count(t, "control R Boost Peak Current Limit") == 0);
	CHECK(count(t, "control R PCM Source")               == 0);
	CHECK(count(t, "control R AMP PCM Gain")             == 0);

	CHECK(count(t, "stream ")  == 0);
	CHECK(count(t, "tuning ")  == 0);
	CHECK(count(t, "missing ") == 0);
	CHECK(!s.config.overflow);

	/* the loudspeakers are what a state without the request gets */
	std::string const sp = s.run(media(Output::SPEAKER));
	CHECK(s.config.output == Output::SPEAKER);
	CHECK(sp == s.run(state(Call::IDLE)));
	CHECK(has(sp, "route media"));
	CHECK(sp != t);

	/* the other states without a call */
	for (Call call : { Call::UNAVAILABLE, Call::INCOMING }) {
		State st = media(Output::EARPIECE);
		st.call = call;
		CHECK(s.run(st) == t);
	}
}

static void test_earpiece_and_the_amplifier_mode()
{
	static Setup s;
	State const st = media(Output::EARPIECE);

	/*
	 * dsp: the firmware is loaded as in every config, the source of the top
	 * amplifier is the later write of the 'handset' path
	 */
	std::string const dsp = s.run(st);
	CHECK(dsp.find("default Incall Mic Mute = 0\n" + firmware_lines
	               + "control Audio DSP Mode = Ambient\n" + handset_lines)
	      != std::string::npos);
	CHECK(count(dsp, "control PCM Source")   == 1);
	CHECK(count(dsp, "control AMP PCM Gain") == 1);

	/*
	 * asp: no firmware. The path of the table, plus the two sources, which
	 * every config of mode asp names (see
	 * 'test_every_asp_route_names_both_amplifier_sources'). The bottom
	 * amplifier has a source that would play, but it is not switched on.
	 */
	std::string const a = s.run(st, asp());
	CHECK(s.config.amp_mode == Amp_mode::ASP);
	CHECK(s.config.output   == Output::EARPIECE);
	CHECK(count(a, "control DSP Booted")          == 0);
	CHECK(count(a, "control DSP1 Preload Switch") == 0);
	CHECK(count(a, "control PCM Source")     == 2);   /* path and policy */
	CHECK(count(a, "control AMP PCM Gain")   == 1);
	CHECK(count(a, "control R PCM Source")   == 1);
	CHECK(count(a, "control R AMP PCM Gain") == 0);
	CHECK(count(a, "control R Main AMP Enable Switch") == 0);
	CHECK(has(a, "control AMP PCM Gain = 6"));
	{
		std::string const source_lines =
			"control PCM Source = ASP\n"
			"control R PCM Source = ASP\n";

		std::string d = dsp;
		d.erase(d.find(firmware_lines), firmware_lines.size());
		std::string without_sources = a;
		without_sources.erase(without_sources.find(source_lines), source_lines.size());
		CHECK(without_sources == d);
	}

	/* the gain of the mode is the one of the loudspeakers, not of the earpiece */
	CHECK(s.run(st, asp(11)) == a);
	CHECK(s.run(st, asp(0))  == a);
}

static void test_output_is_ignored_in_a_call()
{
	static Setup s;

	for (bool speaker : { false, true })
		for (Settings const &settings : { Settings { }, asp() }) {
			State plain = state(Call::ACTIVE, speaker, true, 2, Band::NB);
			State other = plain;
			other.output = Output::EARPIECE;
			other.record = true;

			std::string const want = s.run(plain, settings);
			CHECK(s.run(other, settings) == want);
			CHECK(s.config.output == Output::SPEAKER);
			CHECK(s.config.record == false);
			CHECK(has(want, "capture 8 no"));
			CHECK(want.find(" output ") == std::string::npos);
			CHECK(normalized(other) == normalized(plain));
			CHECK(route(other) == (speaker ? Route::SPEAKER : Route::HANDSET));
		}

	/* 'speaker' of a call and 'output' of media are independent requests */
	State st = state(Call::IDLE, true);
	st.output = Output::EARPIECE;
	CHECK(s.run(st) == s.run(media(Output::EARPIECE)));
	st.output = Output::SPEAKER;
	CHECK(s.run(st) == s.run(state(Call::IDLE)));
}

/*
 * Recording with the built-in microphones
 */

static std::string const record_lines =
	"control Mic Spatial Module Enable = 1\n"        /* handset-mic */
	"control BUILDIN MIC ID CAPTURE LIST [0] = 0\n"
	"control BUILDIN MIC ID CAPTURE LIST [1] = 3\n"
	"control MIC DC Blocker = 1\n"
	"control EP1 TX Mixer INTERNAL_MIC_TX = 1\n"     /* audio-recordC */
	"control MIC Record Soft Gain (dB) = 22\n";      /* handset-mic-post */

static void test_record()
{
	static Setup s;

	for (Output output : { Output::SPEAKER, Output::EARPIECE })
		for (Settings const &settings : { Settings { }, asp() }) {

			std::string const plain = s.run(media(output), settings);
			std::string const t     = s.run(media(output, true), settings);

			CHECK(s.config.route  == Route::MEDIA);
			CHECK(s.config.record == true);
			CHECK(s.config.output == output);
			CHECK(has(t, output == Output::EARPIECE ? "route media output earpiece record"
			                                        : "route media record"));

			/* after everything of the output, in front of the PCMs; both PCMs on */
			CHECK(t.find(record_lines + "playback 1 yes\ncapture 8 yes\n")
			      != std::string::npos);
			CHECK(pos(t, "control TDM_0_RX Mixer EP2 = 1")
			    < pos(t, "control Mic Spatial Module Enable = 1"));
			if (settings.amp_mode == Amp_mode::ASP && output == Output::SPEAKER)
				CHECK(t.find("control R PCM Source = ASP\n" + record_lines)
				      != std::string::npos);

			CHECK(count(t, "control Audio DSP Mode") == 1);
			CHECK(has(t, "control Audio DSP Mode = Ambient"));
			CHECK(count(t, "stream ")  == 0);
			CHECK(count(t, "missing ") == 0);

			/* playback is what it is without the recording */
			std::string want = plain;
			size_t const at = want.find("playback 1 yes\ncapture 8 no\n");
			CHECK(at != std::string::npos);
			if (at != std::string::npos) {
				want.replace(at, std::strlen("playback 1 yes\ncapture 8 no\n"),
				             record_lines + "playback 1 yes\ncapture 8 yes\n");
				want.replace(want.find('\n'), 0, " record");
				CHECK(t == want);
			}

			CHECK(has(plain, "capture 8 no"));
			CHECK(count(plain, "control EP1 TX Mixer") == 0);
			CHECK(count(plain, "control MIC Record Soft Gain") == 0);
		}
}

static void test_record_paths_missing()
{
	static Setup no_mic("handset-mic"), no_post("handset-mic-post"),
	             no_route("audio-recordC"), no_handset("handset");

	std::string const m = no_mic.run(media(Output::SPEAKER, true));
	CHECK(count(m, "missing ") == 1);
	CHECK(has(m, "missing handset-mic"));
	CHECK(has(m, "control EP1 TX Mixer INTERNAL_MIC_TX = 1"));
	CHECK(has(m, "control MIC Record Soft Gain (dB) = 22"));
	CHECK(has(m, "capture 8 yes"));

	CHECK(has(no_post.run(media(Output::SPEAKER, true)),  "missing handset-mic-post"));
	CHECK(has(no_route.run(media(Output::SPEAKER, true)), "missing audio-recordC"));

	/* not missed where they are not used */
	for (Setup *setup : { &no_mic, &no_post, &no_route, &no_handset }) {
		CHECK(count(setup->run(state(Call::IDLE)),         "missing ") == 0);
		CHECK(count(setup->run(state(Call::ACTIVE)),       "missing ") == 0);
		CHECK(count(setup->run(state(Call::ACTIVE, true)), "missing ") == 0);
	}
	CHECK(count(no_handset.run(media(Output::SPEAKER, true)), "missing ") == 0);

	std::string const e = no_handset.run(media(Output::EARPIECE));
	CHECK(count(e, "missing ") == 1);
	CHECK(has(e, "missing handset"));
	CHECK(has(e, "control TDM_0_RX Mixer EP2 = 1"));
	CHECK(has(e, "playback 1 yes"));

	/* an empty table: every path of the request, in the order of use */
	static Paths  paths;
	static Config config;
	generate(media(Output::EARPIECE, true), asp(), paths, config);
	std::string const t = text(config);
	CHECK(count(t, "missing ") == 5);
	CHECK(ordered(t, { "missing handset", "missing low-latency-playbackP",
	                   "missing handset-mic", "missing audio-recordC",
	                   "missing handset-mic-post" }));
	CHECK(has(t, "playback 1 yes"));
	CHECK(has(t, "capture 8 yes"));
}

static void test_output_and_record_do_not_touch_the_media_volume()
{
	for (bool record : { false, true })
		for (Output output : { Output::SPEAKER, Output::EARPIECE }) {
			CHECK(near(media_factor(media(output, record), 15, 0.4), 0.4));
			CHECK(near(media_factor(media(output, record), 0,  0.4), 0.0));
		}
}

static void test_normalized_keeps_output_and_record()
{
	/* outside a call they select the config ... */
	CHECK(normalized(media(Output::EARPIECE)) != normalized(media(Output::SPEAKER)));
	CHECK(normalized(media(Output::SPEAKER, true)) != normalized(media(Output::SPEAKER)));
	CHECK(normalized(media(Output::EARPIECE, true)) != normalized(media(Output::EARPIECE)));
	CHECK(normalized(media(Output::EARPIECE, true)).output == Output::EARPIECE);
	CHECK(normalized(media(Output::EARPIECE, true)).record == true);

	/* ... and nothing else of the request does */
	State st = state(Call::INCOMING, true, true, 0, Band::NB);
	st.output = Output::EARPIECE; st.record = true;
	CHECK(normalized(st) == normalized(media(Output::EARPIECE, true)));

	/* compared as the rest of the state */
	CHECK(media(Output::EARPIECE) != media(Output::SPEAKER));
	CHECK(media(Output::SPEAKER, true) != media(Output::SPEAKER));
	CHECK(media(Output::EARPIECE, true) == media(Output::EARPIECE, true));

	static Setup s;
	for (Call call : { Call::IDLE, Call::INCOMING, Call::ACTIVE })
		for (Output output : { Output::SPEAKER, Output::EARPIECE })
			for (bool record : { false, true })
				for (Settings const &settings : { Settings { }, asp(9) }) {
					State r = state(call, record, !record, 3, Band::NB);
					r.output = output; r.record = record;

					std::string const direct = s.run(r, settings);
					CHECK(s.run(normalized(r), settings) == direct);
					CHECK(normalized(normalized(r)) == normalized(r));

					/* the same input gives the same list, whatever was there before */
					s.run(media(output == Output::SPEAKER ? Output::EARPIECE
					                                      : Output::SPEAKER, !record));
					CHECK(s.run(r, settings) == direct);
				}
}


int main()
{
	test_idle_is_the_media_route();
	test_unavailable_and_incoming_equal_idle();
	test_call_is_the_handset_route();
	test_speaker_in_a_call();
	test_requests_outside_a_call_change_nothing();
	test_mute();
	test_volume();
	test_band();
	test_call_names();
	test_missing_path();
	test_empty_table();
	test_deterministic();
	test_order_of_a_handset_call();
	test_order_of_a_speaker_call();
	test_order_of_media();
	test_defaults_lead_every_config();
	test_dsp_mode_loads_the_amplifier_firmware();
	test_firmware_paths_are_merged();
	test_missing_firmware_path();
	test_asp_mode_bypasses_the_amplifier_dsp();
	test_asp_gain();
	test_asp_writes_follow_the_paths();
	test_amp_mode_names();
	test_media_volume_factor();
	test_media_is_muted_in_a_call();
	test_paths_table();
	test_paths_table_limits();
	test_a_table_as_big_as_possible_fits_a_config();
	test_names_seen();
	test_state_compare();
	test_normalized();
	test_every_asp_route_names_both_amplifier_sources();
	test_output_names();
	test_earpiece_output();
	test_earpiece_and_the_amplifier_mode();
	test_output_is_ignored_in_a_call();
	test_record();
	test_record_paths_missing();
	test_output_and_record_do_not_touch_the_media_volume();
	test_normalized_keeps_output_and_record();

	if (failures) { std::printf("%d of %d checks FAILED\n", failures, checks); return 1; }
	std::printf("audio_policy: all tests passed (%d checks)\n", checks);
	return 0;
}
