// scr_sound.h - DirectSound-compatible sound buffers backed by a small software mixer.
//
// The original game drives its Amiga-style sound effects through IDirectSoundBuffer8
// (frequency changes for engine pitch, volume in hundredths of dB, pan, looping).
// ScrSound keeps exactly that call surface so car_behaviour.cpp etc. are unchanged,
// and ScrMixer produces float stereo output that the platform layer (SDL3 audio)
// pulls from its audio thread.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>
#include <atomic>

#define DSBPLAY_LOOPING  0x00000001
#define DSBPAN_LEFT     (-10000)
#define DSBPAN_RIGHT     (10000)
#define DSBPAN_CENTER        (0)
#define DSBVOLUME_MAX        (0)
#define DSBVOLUME_MIN   (-10000)

class ScrSound
{
public:
    // DirectSound-style interface used by the game code ------------------------
    int32_t Play(uint32_t reserved1, uint32_t priority, uint32_t flags);
    int32_t Stop();
    int32_t SetVolume(int32_t hundredths_db);     // 0 = full, -10000 = silent
    int32_t SetFrequency(uint32_t hz);            // playback sample rate
    int32_t SetPan(int32_t pan);                  // -10000 (left) .. 10000 (right)
    int32_t SetCurrentPosition(uint32_t byte_pos);
    int32_t GetCurrentPosition(uint32_t *play_byte_pos, uint32_t *write_byte_pos);
    void    Release() {}

    // Engine side ---------------------------------------------------------------
    bool    playing() const { return m_playing; }
    // Spatial placement relative to the listener's head, in metres (x right, y up,
    // z back - OpenXR convention). Used for VR spatial audio; (0,0,0) = in head.
    void    SetPosition3D(float x, float y, float z);

private:
    friend class ScrMixer;
    std::vector<float> m_pcm;          // mono samples, -1..1
    uint32_t m_native_rate = 8000;
    uint32_t m_bytes_per_frame = 1;    // of the source WAV, for byte positions
    double   m_pos = 0.0;              // in source frames
    double   m_rate = 8000.0;          // playback rate (frames per second)
    float    m_gain = 1.0f;
    float    m_pan = 0.0f;             // -1..1
    float    m_px = 0, m_py = 0, m_pz = 0;
    bool     m_spatial = false;
    bool     m_playing = false;
    bool     m_looping = false;
};

class ScrMixer
{
public:
    static ScrMixer &Get();

    // Decode a RIFF/WAVE (PCM 8/16 bit, mono or stereo) into a new ScrSound.
    ScrSound *CreateFromWav(const void *data, size_t size);
    // Load from the asset system (e.g. "Sounds/TickOver.wav").
    ScrSound *CreateFromAsset(const char *path);

    void SetOutputRate(uint32_t hz) { m_out_rate = hz; }
    uint32_t OutputRate() const { return m_out_rate; }
    void SetMasterGain(float g) { m_master = g; }
    void PauseAll();          // used when the game is paused / headset removed

    // Mix `frames` interleaved stereo float frames into `out` (overwrites).
    void Mix(float *out, int frames);

    std::mutex &mutex() { return m_mutex; }

private:
    std::vector<ScrSound *> m_sounds;
    std::mutex m_mutex;
    uint32_t m_out_rate = 48000;
    float m_master = 0.8f;
};
