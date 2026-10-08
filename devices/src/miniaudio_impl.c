/* The single translation unit that compiles miniaudio and its Vorbis decoder.
   Built without the project's strict warning flags because it is third-party code. */
#define STB_VORBIS_HEADER_ONLY
#include <miniaudio-extras/stb_vorbis.c>

#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <miniaudio-extras/stb_vorbis.c>
