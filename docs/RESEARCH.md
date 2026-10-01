# RC-20 Retro Color: research notes and the Afterglow design

This document records what XLN Audio's RC-20 Retro Color does, how Afterglow maps each feature, where Afterglow
deliberately goes further, and how the DSP was designed and verified. Afterglow is an original implementation:
it contains no RC-20 code, samples, graphics or preset data. "RC-20" and "Retro Color" are trademarks of XLN Audio
and are used here only to describe the reference product.

## 1. What RC-20 Retro Color is

RC-20 is a "creative effect" that adds the texture of old recording equipment. Its own manual describes the idea as
order versus chaos: the non-linearity and unpredictability of old gear (noise, wobble, distortion, early sampling,
worn tape) feel warm and alive. Its signature feature is the **Flux engine**, which adds random, organic
fluctuations to every module.

### 1.1 Interface and signal flow (from the official manual)

| Area | What it contains |
| --- | --- |
| Top section | Logo (opens Credits and Help), preset name, LOAD, SAVE, and the **Magnitude** slider |
| FX module section | Six modules, left to right in the signal path: Noise, Wobble, Distort, Digital, Space, Magnetic. Each has its own controls and a **Flux** slider |
| Big knob section | One big knob (the module amount) and one enable button per module. A switched-off module is covered by a "hatch"; clicking the hatch or moving the big knob switches it on. At 0 % a module is bypassed |
| Master section | IN (gain), EQ (enable, cut filter with soft/hard slopes at both ends, tone in Tilt or Mid mode), OUT (width 0 to 200 %, gain) |

**Magnitude** is a global amount control: it scales all six big knobs and the master section (In, Out, EQ) at once,
so one automatable slider goes from dry to full effect.

### 1.2 Module by module

| Module | RC-20 controls and behaviour |
| --- | --- |
| Noise | 16 noise types (vinyl in two flavours, tape speeds, cassette, VHS, hum, fuzz pedal noise, 8-bit computer, studio ambience, an Apollo-style transmission, white, pink and more). Tone (tilt), Follow (noise level follows the input, default 5 % since v1.4.0), Duck (noise pushed down by loud input, slow attack), Routing Pre/Post (post inserts the noise after the master EQ). Big knob: noise volume. The noise fades out when the host transport stops |
| Wobble | Pitch modulation like a tape deck or record player. Wow (slow, about 0.1 to 4 Hz) and Flutter (fast, about 6 to 20 Hz) with a Wow/Flutter balance and separate rates. Stereo mode for the wow turns it into a chorus, Mix around 50 % for a lush sound. Big knob: depth |
| Distort | Six saturation algorithms: valve (tube pair), transformer, broken speaker and three waveshapers. A Focus filter limits distortion to a frequency range (for example mids and highs only). Mix. Big knob: drive |
| Digital | Sample-rate and bit-depth reduction with a Rate/Bits balance, Smooth to polish harsh edges, a Focus filter with a CUT option that removes everything outside the focus range, Mix. Big knob: amount |
| Space | A reverb with resonator character. Decay, PreDelay, Focus (resonance and damping of the reverb itself), Stereo switch (mono to stereo). Big knob: dry/wet |
| Magnetic | Tape volume artefacts: Wear (slow, irregular level changes from worn oxide), Flutter (fast, capstan related), Wear/Flutter balance, Rate, Dropouts (sudden random level drops), Stereo. Big knob: amount |

### 1.3 Presets and workflow

* Preset browser with All, XLN and User lists, preset info (author), rename and delete for user presets.
* **Tweak while browsing**: big knobs, Magnitude and the master section stay live while the browser is open.
* OK keeps the result, Cancel returns to the state before the browser was opened.
* A startup preset can be chosen. Presets sync through XLN's cloud.
* Release history up to v1.5.1 (January 2026) added artist presets, keyboard browsing, rendering fixes and plugin
  scaling fixes; v1.4.0 changed the Noise Follow default to 5 %.

### 1.4 What reviewers praised and what they asked for

* Praised: musical results at any setting, very low CPU, the Flux engine, quick results from presets.
* Criticised: the Space reverb is "a pretty unremarkable reverb" (MusicRadar); no Linux version.
* Wish list (Everything Recording): a tape delay, tempo sync for Wobble and Magnetic, a soft-clip limiter.

## 2. How Afterglow maps and extends RC-20

| RC-20 feature | Afterglow | Beyond RC-20 |
| --- | --- | --- |
| Six modules in the same order | Noise, Wobble, Distort, Digital, Space, Magnetic | |
| Flux engine | Per-module Flux built on smooth random curves (Catmull-Rom between jittered random points) with a slow wander and a rougher fast layer that only appears at high settings | Flux targets are module specific (see section 3) |
| 16 noise types from recordings | 16 types synthesised in real time: Vinyl, Shellac, Tape, Cassette, VHS, Hum 50, Hum 60, Buzz, Fuzz, Room, Radio, Transmission, 8-Bit, White, Pink, Brown | No sample library, never loops, every type level-calibrated to the same loudness |
| Wobble wow/flutter, stereo chorus, mix | Same controls | **Tempo sync** with phase lock to the host position, depth defined as pitch deviation so it is constant across rates, 16-tap windowed-sinc interpolation |
| Distort: 6 algorithms, focus, mix | 8 algorithms: Tube, Transformer, Speaker, Tape, Fuzz, Clip, Fold, Rectify. Two-handle focus band, Mix | **Oversampling** (1x to 8x), **automatic level matching**, post-distortion **Tone**, latency-compensated recombination |
| Digital: rate/bits, smooth, focus with CUT, mix | Same controls | **Mu-law companding** (classic 8-bit sampler sound), clock **jitter** from Flux, 2nd-order anti-alias and 4th-order reconstruction filters for Smooth |
| Space: one reverb/resonator | 16-line feedback delay network with six characters: Ambience, Room, Plate, Hall, Spring, Resonator | Answers the "unremarkable reverb" criticism: early reflections, modulated delay lines, dispersive spring, a 12-note chromatic resonator, energy-normalised output |
| Magnetic: wear, flutter, dropouts, stereo | Same controls | Wear and dropouts also remove treble (as real oxide loss does), **tempo sync** for the flutter rate |
| Magnitude | Scales every big knob, input and output gain, EQ cut frequencies, tone and width | Amber markers on the big knobs show the effective value after Magnitude |
| Hatch on disabled modules | Perforated steel cover with a brass name plate; click it or grab the big knob to enable | |
| Master: In, EQ (cut soft/hard, tilt/mid tone), Width, Out | Same | **Global Mix** (dry/wet against a latency-aligned dry signal), **soft safety limiter**, analogue **VU meters** for input and output |
| Preset browser, tweak while browsing, OK/Cancel, startup preset | Same | Categories, user presets as plain XML files in Documents/Afterglow/Presets (easy to back up and share), **undo/redo** |
| Plugin scaling | Freely resizable window (60 % to 220 %) with fixed aspect ratio, all graphics are vector or procedurally generated at the exact pixel density | |

Not included yet: a tape delay module (reviewer wish, planned), cloud preset sync, AAX (needs Avid's SDK and signing).

## 3. DSP design

All processing is in 32-bit float, stereo internally, with parameter smoothing everywhere a step could click.
Every module is exactly transparent when its big knob is at 0 % (verified by null tests, section 4).

### Noise
* Impulsive types (Vinyl, Shellac, Radio) use a crackle generator: Poisson-distributed events with power-law sizes
  (many tiny ticks, few large ones), decaying noise bursts, separate rarer "pops" with a low thump, and band-limiting
  per type. Vinyl adds surface noise that swishes at the platter rotation rate (0.556 Hz at 33 1/3 rpm, 1.3 Hz for 78 rpm)
  and turntable rumble.
* Tape and Cassette shape Gaussian noise with filters matching their hiss spectra; the hiss breathes slightly
  (modulation noise). VHS adds the 15.734 kHz line whine, 59.94 Hz head-switching ticks and occasional tracking swells.
* Hum 50/60 use a harmonic series computed with a Chebyshev recurrence; Buzz models an SCR dimmer style buzz.
* Fuzz gates clipped low-frequency noise into sputtering bursts; Transmission adds squelch bursts and a short
  2525 Hz tone; 8-Bit is a 15-bit LFSR clocked like a home computer sound chip.
* Follow uses a 3 ms / 90 ms envelope, Duck a slow 35 ms / 280 ms envelope for the "pumping" feel. A transport stop
  fades the noise out over about 300 ms.

### Wobble
* A modulated delay read with a Kaiser-windowed sinc interpolator (16 taps, 512 phases).
* Depth is a pitch deviation (up to 3 % for wow, 0.7 % for flutter). The delay amplitude is derived from it
  (A = deviation / (2 pi f)), so the audible depth does not change when the rate changes.
* Flutter combines two incommensurate sines with band-limited noise (scrape flutter); wow has a slight second
  harmonic (eccentric capstan) and a random drift that Flux increases.
* The centre delay follows the needed modulation range smoothly, which sounds like a tape machine changing speed.

### Distort
* The focus band is isolated with 2nd-order high- and low-pass filters. Output = dry + mix * (shaped band - band),
  so with the shaper at unity the module is exactly transparent and the untouched spectrum stays phase-coherent.
* The shaper runs inside JUCE's polyphase FIR oversampler with integer latency; the dry and band paths are delayed
  by the same amount so the recombination stays aligned at every quality setting.
* Make-up gain is computed per block from the shaper's static curve at a reference level, so drive changes colour
  rather than loudness.
* Characters: Tube (asymmetric tanh with power-supply sag), Transformer (low frequencies saturate first),
  Speaker (asymmetric excursion limit, rattle on peaks, narrow honky response), Tape (pre-emphasis, soft knee,
  de-emphasis), Fuzz (starved, gated, asymmetric), Clip (hard with a short knee), Fold (sine wavefolder),
  Rectify (octave-up full-wave blend). Flux drifts drive, bias (even harmonics) and the focus band.

### Digital
* Sample-and-hold with a fractional phase accumulator (any target rate from the host rate down to 650 Hz),
  clock jitter from Flux, mid-tread quantisation from 16 bits down to 1.5 bits with optional mu-law companding.
* Smooth crossfades in an anti-alias filter before the hold and a 4th-order Butterworth reconstruction filter
  after it, both tuned to the target Nyquist frequency.
* CUT mode outputs only the crushed band.

### Space
* 16 delay lines with prime lengths, an orthonormal Hadamard feedback matrix, per-line damping and low cut in the
  loop (set by the type and narrowed by Focus), slow modulation of every line, input diffusion (four Schroeder
  allpasses per channel) and early reflections for Ambience and Room.
* Spring adds a six-stage dispersive allpass chain in every line (the "boing"). Resonator replaces the matrix with
  parallel combs tuned to a chromatic scale from C3, so it rings with any key.
* Output is normalised by the expected energy build-up of the loop, so changing Decay keeps a similar level.

### Magnetic
* Gain modulation computed at control rate and interpolated per sample: wear is irregular (two smooth random
  layers), flutter combines sines and scrape noise, dropouts are random events with their own attack, hold and
  release. Wear and dropouts also close a low-pass filter (treble is lost first on worn tape).

### Master section
* Cut filters: 2nd-order (soft, 12 dB/oct) and 4th-order Butterworth (hard, 24 dB/oct) state-variable filters that
  crossfade when switching. The extreme positions mean "off".
* Tone: Tilt uses +/-6 dB shelves at 300 Hz and 3 kHz, Mid uses a broad +/-9 dB bell at 1 kHz.
* Width in mid/side, output gain, global Mix against the latency-aligned dry input, and a soft limiter that is
  transparent below -3 dBFS and never exceeds -0.3 dBFS.

## 4. Verification (tests/TestMain.cpp)

The test runner is built with the plugin and runs in CI on Windows, macOS and Linux:

| Check | Result on the development machine |
| --- | --- |
| All modules off, Magnitude 0 %, global Mix 0 %: output equals the input delayed by the reported latency | max difference below 1e-6 at 44.1, 48 and 96 kHz and every quality setting |
| Every module on with its big knob at 0 % | transparent (below 1e-5) |
| Impulse position equals the reported latency | exact (0, 49, 61, 65 samples for 1x, 2x, 4x, 8x) |
| Fuzzing: random parameters, sample rates 22.05 to 192 kHz, block sizes 1 to 2048, changes mid-stream | always finite and bounded |
| All 40 factory presets | finite, peaks below 6, quieter band-limited presets level-compensated |
| State save and restore | all 68 parameters restored |
| Host bypass | delayed by exactly the reported latency |
| Preset state | modified flag, defaults for unlisted parameters, Quality never changed by presets |
| Noise calibration | every type within 0.1 dB of its target level |
| Distort level matching (sine at -12 dBFS) | within about +/-2.5 dB from 25 % to 100 % drive |
| Space level across decay settings | within about +/-2 dB (Resonator within 4 dB) |
| Wobble depth | measured pitch swing within 6 % of the design value |
| Aliasing (5 kHz sine, Clip at 80 %) | 1x: -12 dB, 2x: -25 dB, 4x: -30 dB relative to the harmonics |

CI also validates the VST3 with [pluginval](https://github.com/Tracktion/pluginval) at strictness level 8 on Windows, macOS
and Linux (including the editor tests on Windows and macOS); it also passes at strictness level 10 locally.

## 5. Sources

* XLN Audio, *RC-20 Retro Color manual* (Nov 24, 2016): https://assets.xlnaudio.com/documents/rc-20-retro-color_manual.pdf
* XLN Audio, release notes: https://www.xlnaudio.com/release_notes?filter=rc-20
* XLN Audio, product page: https://www.xlnaudio.com/products/addictive_fx/effect/rc-20_retro_color
* MusicRadar review (via Plugin Boutique): https://www.pluginboutique.com/articles/1348
* Everything Recording review (via Plugin Boutique): https://www.pluginboutique.com/articles/1792-XLN-Audio-RC-20-review-by-Everything-Recording
* Tape Op review: https://tapeop.com/reviews/gear/142/rc-20-retro-color-plug-in
* Audioblob review: https://audioblob.com/xln-audio-rc-20-retro-color-review/
* Sound On Sound news: https://www.soundonsound.com/news/xln-rc-20-retro-color-aims-bring-life-and-texture-your-audio
