/*
 * Translation unit that instantiates miniaudio and the Ogg Vorbis decoder.
 *
 * stb_vorbis is consumed header-only first so miniaudio can register its Vorbis
 * backend, then the implementation is compiled once here. Keep this the only
 * place that defines MINIAUDIO_IMPLEMENTATION.
 */

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
