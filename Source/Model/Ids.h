#pragma once
#include <JuceHeader.h>

namespace gubee::ids
{
#define GUBEE_ID(name) inline const juce::Identifier name { #name };
// node types
GUBEE_ID (PROJECT) GUBEE_ID (CHANNELS) GUBEE_ID (CHANNEL) GUBEE_ID (PATTERNS) GUBEE_ID (PATTERN) GUBEE_ID (NOTE)
GUBEE_ID (PLAYLIST) GUBEE_ID (TRACK) GUBEE_ID (CLIP) GUBEE_ID (MIXER) GUBEE_ID (MTRACK) GUBEE_ID (SLOT)
GUBEE_ID (PAD) GUBEE_ID (JAM) GUBEE_ID (JTRACK) GUBEE_ID (JSLOT) GUBEE_ID (MODS) GUBEE_ID (MOD)
// common
GUBEE_ID (id) GUBEE_ID (name) GUBEE_ID (colour) GUBEE_ID (version)
// project
GUBEE_ID (tempo) GUBEE_ID (swing) GUBEE_ID (nextId) GUBEE_ID (loopStart) GUBEE_ID (loopEnd)
// channel / slot
GUBEE_ID (kind) GUBEE_ID (builtin) GUBEE_ID (plugin) GUBEE_ID (state) GUBEE_ID (sample) GUBEE_ID (slices)
GUBEE_ID (vol) GUBEE_ID (pan) GUBEE_ID (mute) GUBEE_ID (solo) GUBEE_ID (mixer) GUBEE_ID (root) GUBEE_ID (bypass)
GUBEE_ID (index)
// pattern / note
GUBEE_ID (length) GUBEE_ID (ch) GUBEE_ID (start) GUBEE_ID (len) GUBEE_ID (key) GUBEE_ID (vel)
// clip
GUBEE_ID (track) GUBEE_ID (pattern) GUBEE_ID (file) GUBEE_ID (offset) GUBEE_ID (gain) GUBEE_ID (fadeIn)
GUBEE_ID (fadeOut) GUBEE_ID (reverse) GUBEE_ID (stretch) GUBEE_ID (srcBpm) GUBEE_ID (pitch) GUBEE_ID (target)
GUBEE_ID (points)
// pads / jam / mods
GUBEE_ID (end) GUBEE_ID (choke) GUBEE_ID (output) GUBEE_ID (scene) GUBEE_ID (scenes) GUBEE_ID (shape)
GUBEE_ID (rate) GUBEE_ID (depth) GUBEE_ID (base) GUBEE_ID (enabled)
// midi tools / learn / sidechain
GUBEE_ID (arp) GUBEE_ID (arpRate) GUBEE_ID (arpOct) GUBEE_ID (arpGate) GUBEE_ID (chord) GUBEE_ID (scale) GUBEE_ID (scaleRoot)
GUBEE_ID (MIDIMAP) GUBEE_ID (MAP) GUBEE_ID (cc) GUBEE_ID (sc)
// mixer
GUBEE_ID (route) GUBEE_ID (sendTo) GUBEE_ID (sendLevel)
#undef GUBEE_ID
}
