// INPUT:  The AAudio subset used by the existing AudioOut queue implementation.
// OUTPUT: Logical PCM streams backed by an actual SDL output device.
// POS:    Internal Linux adapter, not a general Android AAudio implementation.
#pragma once
#include <stdint.h>
#include <time.h>
#include <SDL2/SDL.h>
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_stream_state_t;
typedef int32_t aaudio_data_callback_result_t;
typedef struct AAudioStream AAudioStream;
typedef aaudio_data_callback_result_t (*DmAudioData)(AAudioStream *,void *,void *,int32_t);
typedef void (*DmAudioError)(AAudioStream *,void *,aaudio_result_t);
typedef struct {
    int rate,channels,format,direction,sharing,performance,usage,content;
    DmAudioData data; void *data_context;
    DmAudioError error; void *error_context;
} AAudioStreamBuilder;
enum {AAUDIO_OK=0,AAUDIO_ERROR_DISCONNECTED=-899,AAUDIO_ERROR_ILLEGAL_ARGUMENT=-898,
    AAUDIO_ERROR_INTERNAL=-896,AAUDIO_ERROR_UNAVAILABLE=-889,AAUDIO_ERROR_TIMEOUT=-885,
    AAUDIO_ERROR_INVALID_FORMAT=-883,AAUDIO_CALLBACK_RESULT_CONTINUE=0,
    AAUDIO_DIRECTION_OUTPUT=0,AAUDIO_SHARING_MODE_SHARED=1,AAUDIO_FORMAT_PCM_I16=1,
    AAUDIO_USAGE_GAME=14,AAUDIO_CONTENT_TYPE_MUSIC=2,
    AAUDIO_PERFORMANCE_MODE_NONE=10,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY=12,
    AAUDIO_STREAM_STATE_STARTED=4,AAUDIO_STREAM_STATE_PAUSED=6,AAUDIO_STREAM_STATE_DISCONNECTED=13};
#define DM_AUDIO_SETTER(name,field) static inline void AAudioStreamBuilder_set##name(AAudioStreamBuilder *b,int value){b->field=value;}
DM_AUDIO_SETTER(Direction,direction)
DM_AUDIO_SETTER(SharingMode,sharing)
DM_AUDIO_SETTER(SampleRate,rate)
DM_AUDIO_SETTER(ChannelCount,channels)
DM_AUDIO_SETTER(Format,format)
DM_AUDIO_SETTER(Usage,usage)
DM_AUDIO_SETTER(ContentType,content)
DM_AUDIO_SETTER(PerformanceMode,performance)
#undef DM_AUDIO_SETTER
static inline void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b,DmAudioData fn,void *ctx){b->data=fn;b->data_context=ctx;}
static inline void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b,DmAudioError fn,void *ctx){b->error=fn;b->error_context=ctx;}
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **out);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder,AAudioStream **out);
aaudio_result_t AAudioStream_close(AAudioStream *stream);
aaudio_result_t AAudioStream_requestStart(AAudioStream *stream);
aaudio_result_t AAudioStream_requestPause(AAudioStream *stream);
aaudio_stream_state_t AAudioStream_getState(AAudioStream *stream);
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *stream,aaudio_stream_state_t before,aaudio_stream_state_t *after,int64_t timeout);
int32_t AAudioStream_getSampleRate(AAudioStream *stream);
int32_t AAudioStream_getChannelCount(AAudioStream *stream);
int32_t AAudioStream_getFormat(AAudioStream *stream);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *stream);
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *stream,int32_t frames);
int32_t AAudioStream_getDeviceId(AAudioStream *stream);
int32_t AAudioStream_getPerformanceMode(AAudioStream *stream);
int64_t AAudioStream_getFramesWritten(AAudioStream *stream);
int64_t AAudioStream_getFramesRead(AAudioStream *stream);
aaudio_result_t AAudioStream_getTimestamp(AAudioStream *stream,clockid_t clock,int64_t *position,int64_t *timestamp);
const char *AAudio_convertResultToText(aaudio_result_t result);
void dm_linux_audio_event(const SDL_Event *event);
