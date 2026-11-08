// scr_sound.cpp - software mixer behind the DirectSound-compatible ScrSound API.

#include "scr_sound.h"
#include "scr_compat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ------------------------------------------------------------------------------
// ScrSound
// ------------------------------------------------------------------------------
int32_t ScrSound::Play(uint32_t, uint32_t, uint32_t flags)
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    // DirectSound semantics: Play() on an already playing buffer just updates flags,
    // a stopped buffer resumes from its current position (which is the end, for a
    // one-shot that finished - so rewind in that case).
    if (!m_playing && m_pos >= (double)m_pcm.size()) m_pos = 0.0;
    m_looping = (flags & DSBPLAY_LOOPING) != 0;
    m_playing = !m_pcm.empty();
    return 0;
}

int32_t ScrSound::Stop()
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    m_playing = false;
    return 0;
}

int32_t ScrSound::SetVolume(int32_t hundredths_db)
{
    if (hundredths_db > 0) hundredths_db = 0;
    float g = (hundredths_db <= DSBVOLUME_MIN) ? 0.0f : std::pow(10.0f, (float)hundredths_db / 2000.0f);
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    m_gain = g;
    return 0;
}

int32_t ScrSound::SetFrequency(uint32_t hz)
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    m_rate = hz ? (double)hz : (double)m_native_rate;   // 0 = DSBFREQUENCY_ORIGINAL
    return 0;
}

int32_t ScrSound::SetPan(int32_t pan)
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    m_pan = std::max(-1.0f, std::min(1.0f, (float)pan / 10000.0f));
    return 0;
}

int32_t ScrSound::SetCurrentPosition(uint32_t byte_pos)
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    double frames = (double)(byte_pos / std::max<uint32_t>(1, m_bytes_per_frame));
    if (!m_pcm.empty()) frames = std::fmod(frames, (double)m_pcm.size());
    m_pos = frames;
    return 0;
}

int32_t ScrSound::GetCurrentPosition(uint32_t *play, uint32_t *write)
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    uint32_t b = (uint32_t)m_pos * m_bytes_per_frame;
    if (play) *play = b;
    if (write) *write = b;
    return 0;
}

void ScrSound::SetPosition3D(float x, float y, float z)
{
    std::lock_guard<std::mutex> lk(ScrMixer::Get().mutex());
    m_px = x; m_py = y; m_pz = z;
    m_spatial = (x != 0.0f || y != 0.0f || z != 0.0f);
}

// ------------------------------------------------------------------------------
// ScrMixer
// ------------------------------------------------------------------------------
ScrMixer &ScrMixer::Get()
{
    static ScrMixer m;
    return m;
}

ScrSound *ScrMixer::CreateFromWav(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (size < 12 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0)
        return nullptr;

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *pcm = nullptr;
    uint32_t pcm_bytes = 0;

    size_t off = 12;
    while (off + 8 <= size)
    {
        uint32_t len = rd32(p + off + 4);
        const uint8_t *body = p + off + 8;
        if (off + 8 + len > size) len = (uint32_t)(size - off - 8);
        if (memcmp(p + off, "fmt ", 4) == 0 && len >= 16)
        {
            format = rd16(body);
            channels = rd16(body + 2);
            rate = rd32(body + 4);
            bits = rd16(body + 14);
        }
        else if (memcmp(p + off, "data", 4) == 0)
        {
            pcm = body;
            pcm_bytes = len;
        }
        off += 8 + len + (len & 1);
    }

    if (format != 1 || !pcm || channels == 0 || (bits != 8 && bits != 16))
        return nullptr;

    ScrSound *s = new ScrSound();
    s->m_native_rate = rate;
    s->m_rate = rate;
    s->m_bytes_per_frame = channels * (bits / 8);
    uint32_t frames = pcm_bytes / s->m_bytes_per_frame;
    s->m_pcm.resize(frames);
    for (uint32_t i = 0; i < frames; i++)
    {
        float acc = 0.0f;
        for (uint16_t c = 0; c < channels; c++)
        {
            if (bits == 8)
                acc += ((float)pcm[i * channels + c] - 128.0f) / 128.0f;   // unsigned 8-bit
            else
                acc += (float)(int16_t)rd16(pcm + (i * channels + c) * 2) / 32768.0f;
        }
        s->m_pcm[i] = acc / channels;
    }

    std::lock_guard<std::mutex> lk(m_mutex);
    m_sounds.push_back(s);
    return s;
}

ScrSound *ScrMixer::CreateFromAsset(const char *path)
{
    size_t size = 0;
    void *data = ScrLoadAsset(path, &size);
    if (!data) return nullptr;
    ScrSound *s = CreateFromWav(data, size);
    free(data);
    return s;
}

void ScrMixer::PauseAll()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    for (ScrSound *s : m_sounds) s->m_playing = false;
}

void ScrMixer::Mix(float *out, int frames)
{
    std::memset(out, 0, sizeof(float) * 2 * frames);
    std::lock_guard<std::mutex> lk(m_mutex);
    const double out_rate = (double)m_out_rate;

    for (ScrSound *s : m_sounds)
    {
        if (!s->m_playing || s->m_pcm.empty()) continue;

        const size_t n = s->m_pcm.size();
        const double step = s->m_rate / out_rate;

        // Pan: DirectSound pan, or a simple azimuth from the 3D position for VR.
        float pan = s->m_pan;
        float dist_gain = 1.0f;
        if (s->m_spatial)
        {
            float d = std::sqrt(s->m_px * s->m_px + s->m_py * s->m_py + s->m_pz * s->m_pz);
            pan = (d > 0.001f) ? std::max(-1.0f, std::min(1.0f, s->m_px / d)) : 0.0f;
            dist_gain = 1.0f / std::max(1.0f, d);
        }
        // constant power pan
        const float a = (pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = std::cos(a) * s->m_gain * dist_gain * m_master;
        const float gr = std::sin(a) * s->m_gain * dist_gain * m_master;

        double pos = s->m_pos;
        for (int i = 0; i < frames; i++)
        {
            size_t i0 = (size_t)pos;
            if (i0 >= n)
            {
                if (!s->m_looping) { s->m_playing = false; pos = (double)n; break; }
                pos = std::fmod(pos, (double)n);
                i0 = (size_t)pos;
            }
            size_t i1 = i0 + 1;
            if (i1 >= n) i1 = s->m_looping ? 0 : i0;
            const float f = (float)(pos - (double)i0);
            const float v = s->m_pcm[i0] + (s->m_pcm[i1] - s->m_pcm[i0]) * f;
            out[i * 2 + 0] += v * gl;
            out[i * 2 + 1] += v * gr;
            pos += step;
        }
        s->m_pos = pos;
    }

    for (int i = 0; i < frames * 2; i++)
        out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
}
