/* Packed boundary for stdlib/audio.goose and the JIT symbol registry.
   Main-thread callers only; the SDL callback never enters Goose. */
#ifndef GS_AUDIO_API_H
#define GS_AUDIO_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 1)
typedef struct { uint8_t *data; int64_t len; } gs_audio_bytes;
typedef struct { float *data; int64_t len; } gs_audio_f32_slice;
typedef struct { uint64_t id; } gs_audio_sound;
typedef struct { uint64_t id; } gs_audio_voice;
#pragma pack(pop)

#define GS_AUDIO_CONSTANTS(X) \
    X(i32, MAX_SOUNDS, 256) \
    X(i32, MAX_VOICES, 64)
#define GS_AUDIO_CONST(type, name, value) enum { GS_AUDIO_##name = value };
GS_AUDIO_CONSTANTS(GS_AUDIO_CONST)
#undef GS_AUDIO_CONST

#define GS_AUDIO_API(X) \
    X(uint8_t, gs_audio_available, (void)) \
    X(uint8_t, gs_audio_open, (int64_t sample_rate, uint8_t offline)) \
    X(void, gs_audio_close, (void)) \
    X(int64_t, gs_audio_error, (gs_audio_bytes out)) \
    X(gs_audio_sound, gs_audio_create_sound, (gs_audio_f32_slice samples, int64_t sample_rate, int64_t channels)) \
    X(uint8_t, gs_audio_release_sound, (gs_audio_sound sound)) \
    X(gs_audio_voice, gs_audio_play, (gs_audio_sound sound, float volume, float pan, float pitch, uint8_t loop)) \
    X(uint8_t, gs_audio_playing, (gs_audio_voice voice)) \
    X(void, gs_audio_stop, (gs_audio_voice voice)) \
    X(void, gs_audio_stop_all, (void)) \
    X(uint8_t, gs_audio_set_voice, (gs_audio_voice voice, float volume, float pan, float pitch)) \
    X(uint8_t, gs_audio_render, (gs_audio_f32_slice out))

#define GS_AUDIO_PROTO(ret, name, params) ret name params;
GS_AUDIO_API(GS_AUDIO_PROTO)
#undef GS_AUDIO_PROTO
#ifdef __cplusplus
}
#endif
#endif
