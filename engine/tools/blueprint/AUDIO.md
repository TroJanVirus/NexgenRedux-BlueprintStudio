# Blueprint audio authoring

Blueprint Studio uses Redux's existing sound engine, not an independent audio runtime. The generated synthesis helpers are ordinary AngelScript, included only in exported modules using the new nodes. They also run on stock Redux versions supporting Samples and AudioStream.

## Node workflow

- **Samples / Create samples:** allocate a 16-bit PCM buffer (1 or 2 channels). Choose its frame count and sample rate. The factory accepts 1,000–96,000 Hz and up to 32 MiB, but that ceiling is not a safe budget for a console.
- **Audio / Synthesis / Oscillator:** shape 0 = sine, 1 = square, 2 = saw, 3 = triangle. Phase is in cycles (0–1), not radians. Output is -1 to 1. These are simple oscillators; square and saw are not band-limited. Use the normal random nodes for noise.
- **Render tone / pitch sweep block:** write at most 4,096 frames into an existing Samples buffer. Frequency and endFrequency describe a linear sweep across the *whole buffer*; attack and release are seconds relative to its beginning and end. Gain is 0–1. For longer sounds, store the returned nextPhase and advance start by the returned count, then continue on another update. The mono waveform is written to both channels of stereo buffers; use the stereo Set node for independent channels.
- **Apply sample gain block:** multiply each channel independently, optionally invert polarity using negative gain. Process at most 4,096 frames per call, preserving the rest.
- **Reverse / Normalize sample region:** process an exact selected region up to 4,096 frames. They return 0 for an invalid region rather than silently processing the wrong range. Normalization measures the selected region's combined stereo peak and applies the same gain to both channels; it is not a limiter or whole-file loudness normalizer.
- **Fill stream oscillator block:** declare an AudioStream@ field, an array<float> scratch field, and a float phase field. Create the stream once, prefill it, then play it and retain its Voice. On Update, feed the stream and scratch array into the node, and store nextPhase. It fills no more than the requested frames, stream.room or 4,096 frames, whichever is smallest. It reports the actual accepted count and advances phase only for those frames. Never refill with a new stream every frame. Reuse the scratch array to reduce allocation churn.
- **Voice:** retain the playback result in a Voice field to stop, pause, fade, pan or change pitch later. Pitch changes both playback speed and pitch, not independent time stretching. Sound@ fields can retain loaded sound assets.
- **Audio effects:** choose BusEffects, BusMusic or BusInterface on the editable Bus pin. Audio::lowPass, highPass, bandPass, distortion, chorus, flanger, compressor, equalizer, echo, reverb, volume and clearEffects use Redux's actual bus controls. Spectrum (32 bands), wave (512 samples), output level, active voices and voice limit are already available as API nodes.

## Original Xbox profile

A stock original Xbox has 64 MiB shared system memory. The Redux Xbox backend submits **one** looping hardware DirectSound stereo PCM buffer (4 × 1,024 frames = 16 KiB at 48 kHz). Redux mixes up to 32 software voices, effects and analysis on the CPU; the hardware's own voice capabilities are not an additional pool exposed through these graph nodes. Lower source sample rates save sample storage and reduce graph synthesis work, but do not lower the final 48 kHz software mixing rate.

Suggested starting budgets, not engine guarantees or measured hardware limits:

| Workload | Retail Xbox starting point | Desktop |
|---|---|---|
| Short generated effect | 16–24 kHz mono, 0.1–0.5 sec | 44.1–48 kHz, mono/stereo as needed |
| Procedural stream | 24 kHz mono, 0.15–0.25 sec queue | 48 kHz, 0.10–0.25 sec queue |
| Simultaneous active voices | 8–12 initially, measure before raising | Up to the runtime-reported limit |
| Live effects | One bus, start with simple filtering | Enable more with profiling |
| Wave/spectrum | Only when visible; avoid continuous monitoring in game | As needed |
| Long music | Cook to ADPCM or Ogg, use normal Sound playback | Same formats, choose quality and size |

PCM bytes = frames × channels × 2, or seconds × sample rate × channels × 2. One second of 24 kHz mono is 48,000 bytes; one second of 48 kHz stereo is 192,000 bytes. Generated Samples.play copies the buffer for each voice, so account for the original plus every overlapping playback. Stream queue storage is separate from the reusable float scratch array. The engine allocates echo, reverb and chorus/flanger history lazily per bus: the echo history alone is about 141 KiB per bus, reverb about 57 KiB, chorus/flanger about 32 KiB. Clearing effects does not immediately free that history; these allocations remain until audio shutdown. The actual CPU cost of processing and decoding needs to be measured on the real target.

Use smaller blocks to spread generation across frames. At 24 kHz, 60 fps needs roughly 400 frames per update; at 30 fps, 800. A queue helps tolerate occasional stalls but adds latency. Prefer pre-generating reusable effects over regenerating them on every trigger. Worker scripts do not have the audio bindings; do not try to call Samples or AudioStream on a worker. They can perform non-audio calculations and communicate through channels, but sending huge PCM arrays as text is not a recommended Xbox workflow.

## Assets and migration

Runtime supports WAV, Ogg Vorbis and cooked ADPCM. Cook accepts WAV, Ogg, MP3 and FLAC inputs; convert MP3/FLAC before packaging. Long music should not be expanded into a full Samples buffer unless its decoded size and available memory are known. The sample API's 32 MiB hard limit is enforced before allocating or growing the decoded PCM buffer, not a recommendation to use 32 MiB on Xbox.

The branch includes regression tests for graph round trips, real exported graph execution, stream backpressure, sample processing and decoder memory caps. Physical console performance, hearing quality, underruns, editor waveform audition and a dedicated audio mixer workspace still need verification or implementation. These starting budgets must be profiled alongside the game's graphics and asset loading.
