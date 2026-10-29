/*
 * \brief  Set-up of the Genode side of the sound card (audio.cc)
 * \author Ivan Loskutov
 * \date   2026-10-01
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__AUDIO_MAIN_H_
#define _PIXEL_AUDIO__AUDIO_MAIN_H_

#include <base/env.h>

namespace Pixel_audio {

	/**
	 * Attach the 'config' ROM (the mixer policy) and
	 * install its signal handler. Sessions and the report are opened later,
	 * when the card task asks for them.
	 */
	void audio_init(Genode::Env &);
}

#endif /* _PIXEL_AUDIO__AUDIO_MAIN_H_ */
