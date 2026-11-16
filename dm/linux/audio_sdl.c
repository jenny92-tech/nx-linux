// INPUT:  Original interleaved PCM16 from the shared AudioOut SPSC queue.
// OUTPUT: Actual SDL audio callbacks, with preallocated channel conversion.
// POS:    Preserve buffer/event ownership; no timer sink or fabricated timestamps.
#include "audio_sdl.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct AAudioStream {
    SDL_AudioDeviceID device;
    SDL_AudioSpec native;
    AAudioStreamBuilder request;
    SDL_AudioCVT conversion;
    uint8_t *pcm;
    size_t capacity;
    _Atomic int fault;
};
static pthread_once_t audio_once=PTHREAD_ONCE_INIT;
static int audio_available;
static pthread_mutex_t streams_lock=PTHREAD_MUTEX_INITIALIZER;
static AAudioStream *streams[8];
static void initialize_audio(void) {
    if(SDL_InitSubSystem(SDL_INIT_AUDIO))return;
    const char *driver=SDL_GetCurrentAudioDriver();
    audio_available=driver&&strcmp(driver,"dummy")&&strcmp(driver,"disk");
    if(!audio_available)fprintf(stderr,"LINUX_AUDIO no physical/native audio driver: %s\n",driver?driver:SDL_GetError());
}
static void report_fault(AAudioStream *stream,int error) {
    if(!atomic_exchange(&stream->fault,1)&&stream->request.error)
        stream->request.error(stream,stream->request.error_context,error);
}
static void render(void *context,Uint8 *output,int bytes) {
    AAudioStream *stream=context;
    if(bytes<=0)return;
    memset(output,0,(size_t)bytes);
    unsigned stride=2u*stream->native.channels;
    if((unsigned)bytes%stride){report_fault(stream,AAUDIO_ERROR_INTERNAL);return;}
    unsigned frames=(unsigned)bytes/stride;
    size_t input=(size_t)frames*2u*(unsigned)stream->request.channels;
    if(frames>stream->native.samples||input>stream->capacity){report_fault(stream,AAUDIO_ERROR_INTERNAL);return;}
    if(atomic_load(&stream->fault))return;
    if(stream->request.data(stream,stream->request.data_context,stream->pcm,(int32_t)frames)!=AAUDIO_CALLBACK_RESULT_CONTINUE){
        report_fault(stream,AAUDIO_ERROR_INTERNAL);return;
    }
    if(stream->conversion.needed) {
        SDL_AudioCVT conversion=stream->conversion;
        conversion.buf=stream->pcm;conversion.len=(int)input;
        if(SDL_ConvertAudio(&conversion)||conversion.len_cvt!=bytes){report_fault(stream,AAUDIO_ERROR_INTERNAL);return;}
    } else if(input!=(size_t)bytes){report_fault(stream,AAUDIO_ERROR_INTERNAL);return;}
    memcpy(output,stream->pcm,(size_t)bytes);
}
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **out) {
    if(!out)return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    *out=calloc(1,sizeof **out);
    if(!*out)return AAUDIO_ERROR_UNAVAILABLE;
    **out=(AAudioStreamBuilder){.rate=48000,.channels=2,.format=AAUDIO_FORMAT_PCM_I16,
        .sharing=AAUDIO_SHARING_MODE_SHARED,.performance=AAUDIO_PERFORMANCE_MODE_NONE};
    return AAUDIO_OK;
}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder){free(builder);return AAUDIO_OK;}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder,AAudioStream **out) {
    if(!out)return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    *out=NULL;
    if(!builder||!builder->data||builder->rate!=48000||builder->channels<1||builder->channels>8||
        builder->format!=AAUDIO_FORMAT_PCM_I16||builder->direction!=AAUDIO_DIRECTION_OUTPUT||
        builder->sharing!=AAUDIO_SHARING_MODE_SHARED)return AAUDIO_ERROR_INVALID_FORMAT;
    pthread_once(&audio_once,initialize_audio);
    if(!audio_available)return AAUDIO_ERROR_UNAVAILABLE;
    AAudioStream *stream=calloc(1,sizeof *stream);
    if(!stream)return AAUDIO_ERROR_UNAVAILABLE;
    atomic_init(&stream->fault,0);
    stream->request=*builder;
    SDL_AudioSpec wanted={.freq=builder->rate,.format=AUDIO_S16SYS,.channels=(Uint8)builder->channels,
        .samples=512,.callback=render,.userdata=stream};
    stream->device=SDL_OpenAudioDevice(NULL,0,&wanted,&stream->native,SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if(!stream->device){fprintf(stderr,"LINUX_AUDIO device open failed: %s\n",SDL_GetError());free(stream);return AAUDIO_ERROR_UNAVAILABLE;}
    if(stream->native.freq!=builder->rate||stream->native.format!=AUDIO_S16SYS||!stream->native.channels||
        SDL_BuildAudioCVT(&stream->conversion,AUDIO_S16SYS,(Uint8)builder->channels,builder->rate,
            stream->native.format,stream->native.channels,stream->native.freq)<0)goto failure;
    /* SDL's S16 channel filters operate in this caller-provided buffer. There
     * is no resampling here and no allocation, locking or logging in render. */
    stream->capacity=(size_t)stream->native.samples*2u*(unsigned)builder->channels*(unsigned)stream->conversion.len_mult;
    stream->pcm=malloc(stream->capacity);
    if(!stream->pcm)goto failure;
    pthread_mutex_lock(&streams_lock);
    unsigned slot=0;while(slot<8&&streams[slot])++slot;
    if(slot<8)streams[slot]=stream;
    pthread_mutex_unlock(&streams_lock);
    if(slot==8)goto failure;
    fprintf(stderr,"LINUX_AUDIO driver=%s source=48000Hz/%dch/PCM16 sink=%dHz/%uch/PCM16 period=%u frames\n",
        SDL_GetCurrentAudioDriver(),builder->channels,stream->native.freq,stream->native.channels,stream->native.samples);
    *out=stream;return AAUDIO_OK;
failure:
    SDL_CloseAudioDevice(stream->device);free(stream->pcm);free(stream);return AAUDIO_ERROR_INVALID_FORMAT;
}
aaudio_result_t AAudioStream_close(AAudioStream *stream) {
    if(!stream)return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    pthread_mutex_lock(&streams_lock);
    for(unsigned i=0;i<8;i++)if(streams[i]==stream)streams[i]=NULL;
    pthread_mutex_unlock(&streams_lock);
    SDL_CloseAudioDevice(stream->device);free(stream->pcm);free(stream);return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *stream) {
    if(atomic_load(&stream->fault))return AAUDIO_STREAM_STATE_DISCONNECTED;
    SDL_AudioStatus status=SDL_GetAudioDeviceStatus(stream->device);
    return status==SDL_AUDIO_PLAYING?AAUDIO_STREAM_STATE_STARTED:status==SDL_AUDIO_PAUSED?AAUDIO_STREAM_STATE_PAUSED:AAUDIO_STREAM_STATE_DISCONNECTED;
}
aaudio_result_t AAudioStream_requestStart(AAudioStream *stream) {
    SDL_PauseAudioDevice(stream->device,0);
    return AAudioStream_getState(stream)==AAUDIO_STREAM_STATE_STARTED?AAUDIO_OK:AAUDIO_ERROR_DISCONNECTED;
}
aaudio_result_t AAudioStream_requestPause(AAudioStream *stream) {
    SDL_PauseAudioDevice(stream->device,1);
    return AAudioStream_getState(stream)==AAUDIO_STREAM_STATE_PAUSED?AAUDIO_OK:AAUDIO_ERROR_DISCONNECTED;
}
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *stream,aaudio_stream_state_t before,aaudio_stream_state_t *after,int64_t timeout) {
    if(!after)return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    uint64_t start=SDL_GetPerformanceCounter(),frequency=SDL_GetPerformanceFrequency();
    for(;;){
        *after=AAudioStream_getState(stream);if(*after!=before)return AAUDIO_OK;
        if(timeout<=0||(double)(SDL_GetPerformanceCounter()-start)/(double)frequency>=(double)timeout/1e9)return AAUDIO_ERROR_TIMEOUT;
        SDL_Delay(1);
    }
}
int32_t AAudioStream_getSampleRate(AAudioStream *stream){return stream->request.rate;}
int32_t AAudioStream_getChannelCount(AAudioStream *stream){return stream->request.channels;}
int32_t AAudioStream_getFormat(AAudioStream *stream){return stream->request.format;}
int32_t AAudioStream_getFramesPerBurst(AAudioStream *stream){return stream->native.samples;}
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *stream,int32_t frames){return frames>0?stream->native.samples:AAUDIO_ERROR_ILLEGAL_ARGUMENT;}
int32_t AAudioStream_getDeviceId(AAudioStream *stream){return (int32_t)stream->device;}
int32_t AAudioStream_getPerformanceMode(AAudioStream *stream){(void)stream;return AAUDIO_PERFORMANCE_MODE_NONE;}
/* SDL exposes callback requests, not hardware playback positions. Preserve
 * that distinction instead of converting submitted buffers into timestamps. */
int64_t AAudioStream_getFramesWritten(AAudioStream *stream){(void)stream;return AAUDIO_ERROR_UNAVAILABLE;}
int64_t AAudioStream_getFramesRead(AAudioStream *stream){(void)stream;return AAUDIO_ERROR_UNAVAILABLE;}
aaudio_result_t AAudioStream_getTimestamp(AAudioStream *stream,clockid_t clock,int64_t *position,int64_t *timestamp){
    (void)stream;(void)clock;(void)position;(void)timestamp;return AAUDIO_ERROR_UNAVAILABLE;
}
const char *AAudio_convertResultToText(aaudio_result_t result) {
    if(result==AAUDIO_OK)return "SDL output ready";
    if(result==AAUDIO_ERROR_UNAVAILABLE)return "SDL native output/measurement unavailable";
    if(result==AAUDIO_ERROR_INVALID_FORMAT)return "SDL PCM conversion unavailable";
    if(result==AAUDIO_ERROR_DISCONNECTED)return "SDL audio device disconnected";
    return "SDL audio adapter error";
}
void dm_linux_audio_event(const SDL_Event *event) {
    if(event->type!=SDL_AUDIODEVICEREMOVED||event->adevice.iscapture)return;
    pthread_mutex_lock(&streams_lock);
    for(unsigned i=0;i<8;i++)if(streams[i]&&streams[i]->device==event->adevice.which)
        report_fault(streams[i],AAUDIO_ERROR_DISCONNECTED);
    pthread_mutex_unlock(&streams_lock);
}
