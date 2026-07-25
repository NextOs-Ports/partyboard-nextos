// NextOS PC audio sink for the MusyX software renderer.
// Opens an SDL3 audio device (32 kHz, stereo, s16) and pumps frames from the
// musyx PC backend (salPumpAudioFrame). The renderer itself lives in musyx.
#pragma once

namespace partyboard::pc_audio {
// Returns true if the SDL audio stream is open and running.
bool initialize();
void shutdown();
}
