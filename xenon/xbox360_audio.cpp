#include "../source/stubs.h"
// Reemplazo de source/xenos_audio/audio.c para XDK / Xbox 360 (.xex)
// Misma interfaz publica (AiDacrateChanged, AiLenChanged, InitiateAudio,
// etc. - ver AudioPlugin.h) que ya consume el core de mupen64plus, pero
// reproduciendo con XAudio2 en vez de xenon_sound_submit() de libxenon.
//
// NOTA IMPORTANTE: el original hace bswap_32 sobre las muestras porque
// libxenon reproducia en big-endian nativo de la PPC sin pasar por un
// mezclador que hiciera la conversion. Con XAudio2 (que espera PCM en
// el formato de canal que tu declares, normalmente little-endian intel
// aunque la CPU sea PPC/big-endian) hay que revisar si ese swap se
// sigue necesitando o si XAudio2 ya asume el formato correcto: dejar
// SWAP_SAMPLES a 1 y comprobar de oido/con un tono de prueba.

#include "main/main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <xtl.h>
#include <xaudio2.h>

#include "winlnxdefs.h"
#include "api/m64p_plugin.h"
#include "AudioPlugin.h"

#define SWAP_SAMPLES 1

AUDIO_INFO AudioInfo;

#define MAX_UNPLAYED 16384
#define BUFFER_SIZE 65536

static char buffer[BUFFER_SIZE];
static volatile unsigned int freq;
static volatile unsigned int real_freq;
static volatile double freq_ratio;
static volatile int is_60Hz;
static volatile int buffer_size = 1024;

static unsigned char thread_stack[0x100000];
static volatile unsigned char thread_buffer[65536];
static volatile int thread_bufsize = 0;
static volatile int thread_terminate = 0;
static HANDLE audio_thread_handle = NULL;

char audioEnabled = 1;

// ---- Backend XAudio2 ----
static IXAudio2 * g_pXAudio2 = NULL;
static IXAudio2MasteringVoice * g_pMasterVoice = NULL;
static IXAudio2SourceVoice * g_pSourceVoice = NULL;

static void xaudio2_init(unsigned int sampleRate)
{
    XAudio2Create(&g_pXAudio2, 0);
    g_pXAudio2->CreateMasteringVoice(&g_pMasterVoice);

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, g_pMasterVoice };
    XAUDIO2_VOICE_SENDS sendList = { 1, &sendDesc };

    g_pXAudio2->CreateSourceVoice(&g_pSourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, &sendList, NULL);
    g_pSourceVoice->Start(0);
}

static unsigned int xaudio2_get_unplayed()
{
    if (!g_pSourceVoice) return 0;
    XAUDIO2_VOICE_STATE state;
    g_pSourceVoice->GetState(&state);
    return state.BuffersQueued;
}

static void xaudio2_submit(void * data, unsigned int size)
{
    if (!g_pSourceVoice) return;

    XAUDIO2_BUFFER buf = {0};
    buf.AudioBytes = size;
    buf.pAudioData = (const BYTE*)data;
    g_pSourceVoice->SubmitSourceBuffer(&buf);
}

EXPORT void CALL
AiDacrateChanged( int SystemType )
{
    freq = 32000;
    switch (SystemType){
        case SYSTEM_NTSC:
            freq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1);
            break;
        case SYSTEM_PAL:
            freq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1);
            break;
        case SYSTEM_MPAL:
            freq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1);
            break;
    }

    real_freq = 48000;
    freq_ratio = (double)freq / real_freq;
    is_60Hz = (SystemType != SYSTEM_PAL);

    if (!g_pXAudio2)
        xaudio2_init(real_freq);
}

static void play_buffer(void)
{
    if (use_framelimit){
        while (xaudio2_get_unplayed() > (MAX_UNPLAYED / BUFFER_SIZE)) {}
    }

#if SWAP_SAMPLES
    int i;
    for (i=0;i<buffer_size/4;++i) ((int*)buffer)[i]=_byteswap_ulong(((int*)buffer)[i]);
#endif

    xaudio2_submit(buffer, buffer_size);
}

static short prevLastSample[2]={0,0};

void ResampleLinear(short* pStereoSamples, int oldsamples, short* pNewSamples, int newsamples)
{
    int newsampL, newsampR;
    int i;

    for (i = 0; i < newsamples; ++i)
    {
        int io = i * oldsamples;
        int old = io / newsamples;
        int rem = io - old * newsamples;

        old *= 2;
        if (old==0){
            newsampL = prevLastSample[0] * (newsamples - rem) + pStereoSamples[0] * rem;
            newsampR = prevLastSample[1] * (newsamples - rem) + pStereoSamples[1] * rem;
        }else{
            newsampL = pStereoSamples[old-2] * (newsamples - rem) + pStereoSamples[old] * rem;
            newsampR = pStereoSamples[old-1] * (newsamples - rem) + pStereoSamples[old+1] * rem;
        }
        pNewSamples[2 * i] = (short)(newsampL / newsamples);
        pNewSamples[2 * i + 1] = (short)(newsampR / newsamples);
    }

    prevLastSample[0]=pStereoSamples[oldsamples*2-2];
    prevLastSample[1]=pStereoSamples[oldsamples*2-1];
}

static void add_to_buffer(void* stream, unsigned int length)
{
    unsigned int lengthLeft = length >> 2;
    unsigned int rlengthLeft = (unsigned int)ceil(lengthLeft / freq_ratio);

    ResampleLinear((short *)stream,lengthLeft,(short *)buffer,rlengthLeft);
    buffer_size=rlengthLeft<<2;
    play_buffer();
}

static void thread_enqueue(void * buf,int size)
{
    while (thread_bufsize) { Sleep(0); }

    thread_bufsize=size;
    memcpy((void*)thread_buffer,buf,thread_bufsize);
}

static DWORD WINAPI thread_loop(LPVOID)
{
    while(!thread_terminate){
        if (thread_bufsize){
            add_to_buffer((void*)thread_buffer,(unsigned int)thread_bufsize);
            thread_bufsize=0;
        }
    }
    return 0;
}

EXPORT void CALL
AiLenChanged( void )
{
    if(!audioEnabled) return;

    short* stream = (short*)(AudioInfo.RDRAM +
                 (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));
    unsigned int length = *AudioInfo.AI_LEN_REG;

#ifdef USE_THREADED_AUDIO
    thread_enqueue(stream,length);
#else
    add_to_buffer(stream, length);
#endif
}

EXPORT DWORD CALL
AiReadLength( void )
{
    return buffer_size;
}

EXPORT void CALL
AiUpdate( BOOL Wait )
{
}

EXPORT void CALL
CloseDLL( void )
{
}

EXPORT void CALL RomOpen()
{
}

EXPORT void CALL
RomClosed( void )
{
    thread_terminate=1;
    if (audio_thread_handle)
    {
        WaitForSingleObject(audio_thread_handle, INFINITE);
        CloseHandle(audio_thread_handle);
        audio_thread_handle = NULL;
    }
}

EXPORT BOOL CALL
InitiateAudio( AUDIO_INFO Audio_Info )
{
    AudioInfo = Audio_Info;
  
	
    thread_bufsize=0;
    thread_terminate=0;

#ifdef USE_THREADED_AUDIO
    audio_thread_handle = CreateThread(NULL, sizeof(thread_stack), thread_loop, NULL, 0, NULL);
#endif

    atexit(RomClosed);
    return TRUE;
}

EXPORT void CALL
ProcessAlist( void )
{
}

void pauseAudio(void){
}

void resumeAudio(void){
}
