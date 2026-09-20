/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The single translation unit that instantiates the vendored dr_libs
 * decoders. Kept apart from our own code so their warnings can be silenced in
 * one place (see CMakeLists.txt) without loosening -Werror anywhere else.
 *
 * Features we do not use are compiled out: no stdio paths (everything is
 * decoded from a memory buffer we read ourselves, which also gives the fuzz
 * targets a single door to knock on), no Ogg-FLAC container.
 */
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_flac.h"
