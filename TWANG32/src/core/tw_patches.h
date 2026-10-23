/*
  TWANG Next - instrument and sound-effect patches.
  Field order:
    wave, filter, bus, pw,
    attack, decay, sustain, release,
    pitchFrom, pitchTime, vibDepth, vibRate, vibDelay,
    cutoff, cutoffEnv, cutoffTime, res,
    detune, gain, echo, noise
  Times are seconds, pitch in semitones, cutoff in Hz.
*/
#pragma once
#include "tw_synth.h"

namespace tw {

static const Patch PATCHES[PATCH_COUNT] = {
    // ---------------------------------------------------------------- music
    // P_DRONE: two detuned saws through a dark breathing filter
    {W_SAW2, F_LP, BUS_MUSIC, 0, 1.2f, 0.6f, 0.85f, 1.8f, 0, 0, 0.06f, 0.25f, 0, 480, 1.0f, 1, 0.35f, 14, 0.30f, 0.12f, 0},
    // P_PAD: softer choir-ish pad with slow vibrato
    {W_SAW2, F_LP, BUS_MUSIC, 0, 0.9f, 0.7f, 0.7f, 1.6f, 0, 0, 0.10f, 4.5f, 0.6f, 850, 1.3f, 1.5f, 0.2f, 9, 0.17f, 0.35f, 0},
    // P_HEART: sub thump, pitch dropping like a heartbeat
    {W_SINE, F_OFF, BUS_MUSIC, 0, 0.002f, 0.30f, 0, 0.10f, 7, 0.03f, 0, 0, 0, 0, 1, 1, 0, 0, 0.55f, 0, 0},
    // P_BASS: plucky resonant saw bass
    {W_SAW, F_LP, BUS_MUSIC, 0, 0.004f, 0.22f, 0.30f, 0.12f, 0, 0, 0, 0, 0, 360, 3.5f, 0.08f, 0.45f, 0, 0.30f, 0, 0},
    // P_BELL: inharmonic bell, long tail into the echo
    {W_BELL, F_OFF, BUS_MUSIC, 0, 0.002f, 1.0f, 0, 0.7f, 0.15f, 0.01f, 0.05f, 5, 0.2f, 0, 1, 1, 0, 0, 0.15f, 0.55f, 0},
    // P_PLUCK: short pulse arp
    {W_PULSE, F_LP, BUS_MUSIC, 0.25f, 0.002f, 0.18f, 0, 0.12f, 0, 0, 0, 0, 0, 1300, 3, 0.06f, 0.3f, 0, 0.15f, 0.35f, 0},
    // P_HAT: filtered noise tick
    {W_NOISE, F_HP, BUS_MUSIC, 0, 0.001f, 0.045f, 0, 0.03f, 0, 0, 0, 0, 0, 3600, 1, 1, 0.2f, 0, 0.11f, 0.1f, 0},
    // P_KICK: sine with a fast pitch drop
    {W_SINE, F_OFF, BUS_MUSIC, 0, 0.001f, 0.32f, 0, 0.05f, 24, 0.025f, 0, 0, 0, 0, 1, 1, 0, 0, 0.62f, 0, 0},
    // P_SNARE: band-passed noise crack
    {W_NOISE, F_BP, BUS_MUSIC, 0, 0.001f, 0.16f, 0, 0.08f, 0, 0, 0, 0, 0, 1700, 1.6f, 0.05f, 0.25f, 0, 0.34f, 0.35f, 0},
    // P_TOM: pitched drum
    {W_SINE, F_OFF, BUS_MUSIC, 0, 0.002f, 0.35f, 0, 0.10f, 9, 0.06f, 0, 0, 0, 0, 1, 1, 0, 0, 0.42f, 0.2f, 0.05f},
    // P_TENSION: high shimmering cluster, fast tremolo-ish vibrato
    {W_SAW2, F_LP, BUS_MUSIC, 0, 1.4f, 0.5f, 0.8f, 1.2f, 0, 0, 0.22f, 7.5f, 0, 2100, 1, 1, 0.15f, 22, 0.07f, 0.4f, 0},
    // P_LEAD: mournful square lead with delayed vibrato
    {W_PULSE, F_LP, BUS_MUSIC, 0.5f, 0.01f, 0.4f, 0.45f, 0.4f, 0, 0, 0.18f, 5.2f, 0.35f, 1500, 1.8f, 0.2f, 0.25f, 0, 0.12f, 0.5f, 0},
    // P_ANVIL: metallic ping
    {W_PULSE, F_BP, BUS_MUSIC, 0.12f, 0.001f, 0.25f, 0, 0.10f, 0, 0, 0, 0, 0, 2500, 1, 1, 0.6f, 0, 0.12f, 0.45f, 0.15f},
    // P_WIND: whispering noise swell (vibrato sweeps the filter for noise patches)
    {W_NOISE, F_BP, BUS_MUSIC, 0, 1.5f, 1.0f, 0.6f, 2.0f, 0, 0, 0.6f, 0.15f, 0, 650, 1, 1, 0.75f, 0, 0.16f, 0.3f, 0},

    // ---------------------------------------------------------------- effects
    // S_BOING: the twang - a springy triangle with a decaying wobble
    {W_TRI, F_LP, BUS_SFX, 0, 0.002f, 0.42f, 0, 0.10f, -5, 0.05f, 1.6f, 13, -0.35f, 2600, 1.5f, 0.1f, 0.1f, 0, 0.42f, 0.2f, 0},
    // S_BLIP: kill blip, pitch drops an octave
    {W_SQUARE, F_LP, BUS_SFX, 0.5f, 0.001f, 0.12f, 0, 0.05f, 12, 0.02f, 0, 0, 0, 3000, 1, 1, 0.1f, 0, 0.26f, 0.25f, 0},
    // S_CRUNCH: kill crunch
    {W_NOISE, F_BP, BUS_SFX, 0, 0.001f, 0.09f, 0, 0.03f, 0, 0, 0, 0, 0, 1800, 2.2f, 0.03f, 0.4f, 0, 0.36f, 0.1f, 0},
    // S_CLANK: armor hit
    {W_PULSE, F_BP, BUS_SFX, 0.2f, 0.001f, 0.20f, 0, 0.08f, 7, 0.01f, 0.3f, 30, -0.1f, 2200, 1, 1, 0.7f, 0, 0.34f, 0.3f, 0.2f},
    // S_DIE: noise sweeping from bright to dark
    {W_NOISE, F_LP, BUS_SFX, 0, 0.001f, 1.1f, 0, 0.2f, 0, 0, 0, 0, 0, 150, 21, 0.3f, 0.3f, 0, 0.55f, 0.25f, 0},
    // S_THUD: low impact
    {W_SINE, F_OFF, BUS_SFX, 0, 0.001f, 0.6f, 0, 0.1f, 12, 0.08f, 0, 0, 0, 0, 1, 1, 0, 0, 0.6f, 0, 0},
    // S_HUM: movement thrum (sustained, pitch/volume driven by speed)
    {W_SAW2, F_LP, BUS_SFX, 0, 0.06f, 0.1f, 1.0f, 0.12f, 0, 0, 0.15f, 6, 0, 420, 1, 1, 0.3f, 18, 0.22f, 0, 0},
    // S_SIZZLE: lava ambience (sustained, volume driven by proximity)
    {W_NOISE, F_BP, BUS_SFX, 0, 0.2f, 0.1f, 1.0f, 0.3f, 0, 0, 0.5f, 3, 0, 2300, 1, 1, 0.5f, 0, 0.30f, 0, 0},
    // S_SPAWN: soft rising pop
    {W_SINE, F_OFF, BUS_SFX, 0, 0.001f, 0.10f, 0, 0.05f, -12, 0.04f, 0, 0, 0, 0, 1, 1, 0, 0, 0.16f, 0.2f, 0},
    // S_GEM: bright bell
    {W_BELL, F_OFF, BUS_SFX, 0, 0.001f, 0.5f, 0, 0.3f, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0.26f, 0.45f, 0},
    // S_IGNITE: whoosh that opens up
    {W_NOISE, F_LP, BUS_SFX, 0, 0.03f, 0.35f, 0, 0.1f, 0, 0, 0, 0, 0, 1800, 0.15f, 0.08f, 0.35f, 0, 0.85f, 0.2f, 0},
    // S_TICK: lava warning tick
    {W_SQUARE, F_HP, BUS_SFX, 0.5f, 0.001f, 0.03f, 0, 0.02f, 0, 0, 0, 0, 0, 1500, 1, 1, 0.1f, 0, 0.14f, 0.2f, 0},
    // S_ROAR: boss roar
    {W_SAW2, F_LP, BUS_SFX, 0, 0.02f, 0.7f, 0, 0.3f, -3, 0.2f, 0.8f, 9, 0, 700, 2.5f, 0.25f, 0.5f, 30, 0.45f, 0.3f, 0},
    // S_BOSSHIT: heavy impact
    {W_NOISE, F_LP, BUS_SFX, 0, 0.001f, 0.4f, 0, 0.1f, 0, 0, 0, 0, 0, 1200, 4, 0.05f, 0.3f, 0, 0.5f, 0.3f, 0},
    // S_RAGE: rising warning before the boss erupts
    {W_SAW, F_LP, BUS_SFX, 0, 0.6f, 0.1f, 1.0f, 0.2f, -12, 0.35f, 0.6f, 18, 0, 900, 1, 1, 0.6f, 0, 0.30f, 0.2f, 0},
    // S_STARTUP: the power-on swell (a richer take on the original warble sweep)
    {W_SAW2, F_LP, BUS_SFX, 0, 0.05f, 1.6f, 0, 0.3f, -20, 0.6f, 0.5f, 9, 0, 1800, 0.2f, 0.5f, 0.4f, 15, 0.35f, 0.4f, 0},
    // S_STING: stinger lead
    {W_PULSE, F_LP, BUS_SFX, 0.35f, 0.003f, 0.35f, 0.3f, 0.25f, 0, 0, 0.12f, 6, 0.15f, 3000, 1.5f, 0.1f, 0.15f, 0, 0.24f, 0.45f, 0},
    // S_STING_B: stinger bass
    {W_TRI, F_LP, BUS_SFX, 0, 0.003f, 0.6f, 0.2f, 0.3f, 0, 0, 0, 0, 0, 1200, 1, 1, 0.1f, 0, 0.36f, 0.1f, 0},
    // S_UI: interface blip
    {W_SINE, F_OFF, BUS_SFX, 0, 0.002f, 0.08f, 0, 0.04f, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0.25f, 0.1f, 0},
    // S_LAVAKILL: an enemy fizzles in lava
    {W_NOISE, F_HP, BUS_SFX, 0, 0.001f, 0.18f, 0, 0.05f, 0, 0, 0, 0, 0, 2600, 1, 1, 0.3f, 0, 0.18f, 0.2f, 0},
    // S_SPLIT: splitter pop
    {W_SQUARE, F_LP, BUS_SFX, 0.5f, 0.001f, 0.09f, 0, 0.04f, -7, 0.03f, 0, 0, 0, 2400, 1, 1, 0.2f, 0, 0.22f, 0.3f, 0},
    // S_THRUM: conveyor pulse
    {W_PULSE, F_LP, BUS_SFX, 0.3f, 0.003f, 0.08f, 0, 0.04f, 0, 0, 0, 0, 0, 500, 1, 1, 0.4f, 0, 0.12f, 0, 0},
};

}  // namespace tw
