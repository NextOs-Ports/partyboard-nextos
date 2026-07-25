// NextOS PC audio sink for MusyX (software renderer in musyx hw_pc.c).
//
// The musyx PC target has no audio output of its own; salPumpAudioFrame() drives
// one DMA frame (PC_DMA_FRAMES = 160 stereo s16) through the software voice
// renderer and returns it. This file owns the SDL3 audio device and calls
// salPumpAudioFrame from the SDL callback.

#include "pc_audio.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <chrono>
#include <cstdio>
#include <thread>

extern "C" void salPumpAudioFrame(short* out); // musyx PC backend (hw_pc.c)

namespace partyboard::pc_audio {

static SDL_AudioStream* g_stream = nullptr;

static constexpr int kSampleRate = 32000;   // musyx outFreq
static constexpr int kChannels = 2;         // stereo
// PC_DMA_FRAMES = DMA_BUFFER_LEN/4 = 160 stereo s16 sample-pairs (640 bytes).
static constexpr int kFrameShorts = 160 * 2;

static void SDLCALL audio_callback(void* /*userdata*/, SDL_AudioStream* stream, int additional_amount,
                                   int /*total_amount*/) {
#if 0
  static unsigned calls = 0;
  if ((calls++ % 200) == 0) {
    fprintf(stderr, "[pc-audio] callback #%u additional=%d\n", calls, additional_amount);
  }
#endif
  short buf[kFrameShorts];
  int needed = additional_amount;
  /* Push whole 160-frame chunks; SDL buffers any excess. Always render at least
   * one frame so the musyx pump advances even when SDL asks for a tiny amount. */
  int guard = 0;
  while (needed > 0 && guard++ < 64) {
    salPumpAudioFrame(buf);
    if (!SDL_PutAudioStreamData(stream, buf, sizeof(buf))) {
      return;
    }
    needed -= (int)sizeof(buf);
  }
  if (needed > 0) {
    salPumpAudioFrame(buf);
    SDL_PutAudioStreamData(stream, buf, sizeof(buf));
  }
}

bool initialize() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    return false;
  }
  const SDL_AudioSpec spec{SDL_AUDIO_S16LE, kChannels, kSampleRate};
  // NextOS' frontend releases the raw Amlogic PCM asynchronously; a port launched
  // right after EmulationStation stops can briefly get EBUSY. Retry briefly.
  for (int attempt = 0; attempt < 13 && g_stream == nullptr; ++attempt) {
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, nullptr);
    if (g_stream == nullptr && attempt + 1 < 13) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
  }
  if (g_stream == nullptr) {
    return false;
  }
  return SDL_ResumeAudioStreamDevice(g_stream);
}

void shutdown() {
  if (g_stream != nullptr) {
    SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
  }
}

} // namespace partyboard::pc_audio
