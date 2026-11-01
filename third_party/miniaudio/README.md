# miniaudio (vendored)

- Upstream: https://github.com/mackron/miniaudio
- Version: 0.11.26
- License: public domain OR MIT-0 (see the header of `miniaudio.h`)

`miniaudio.c` compiles the implementation (it defines `MINIAUDIO_IMPLEMENTATION`).
The rest of the project only includes `miniaudio.h`.

On Linux the ALSA backend is used; the audio thread code is compiled with
`-pthread`. No system audio library is linked (`MA_NO_*` is not set, so
miniaudio picks PulseAudio / ALSA / JACK at runtime from dlopen'ed system
libraries).
