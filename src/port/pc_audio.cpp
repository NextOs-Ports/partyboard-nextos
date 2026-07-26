#include "pc_audio.hpp"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <chrono>
#include <cstdio>
#include <thread>
extern "C" void salPumpAudioFrame(short* out);
namespace partyboard::pc_audio {
static SDL_AudioStream* g_stream = nullptr;
static constexpr int kSR = 32000, kCH = 2, kFS = 160 * 2;
static void SDLCALL cb(void*, SDL_AudioStream* s, int add, int) {
  short buf[kFS];
  int needed = add > 0 ? add : (int)sizeof(buf);
  int guard = 0;
  while (needed > 0 && guard++ < 64) {
    salPumpAudioFrame(buf);
    if (!SDL_PutAudioStreamData(s, buf, sizeof(buf))) return;
    needed -= (int)sizeof(buf);
  }
}
bool initialize() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
  const SDL_AudioSpec spec{SDL_AUDIO_S16LE, kCH, kSR};
  for (int i = 0; i < 13 && !g_stream; ++i) {
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, cb, nullptr);
    if (!g_stream && i < 12) std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  return g_stream && SDL_ResumeAudioStreamDevice(g_stream);
}
void shutdown() { if (g_stream) { SDL_DestroyAudioStream(g_stream); g_stream = nullptr; } }
}
