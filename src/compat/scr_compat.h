// scr_compat.h - Portable replacements for the Windows / DirectX types used by the
// original Stunt Car Racer remake sources.
//
// The game core was written against Win32 + Direct3D 9 + DirectSound. Rather than
// rewriting the Amiga-derived physics, we keep the original source and give it the
// small set of types it expects. Everything here is platform neutral.
//
// IMPORTANT: the original code assumed 32-bit `long` (Windows LLP64). The sources in
// src/game have had `long` replaced by `int32_t` so that the fixed-point physics
// behaves identically on 64-bit Linux / Android (LP64), e.g. on Meta Quest.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cwchar>

#if defined(_WIN32)
// On Windows the original sources were built with <windows.h>; use the real types.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
typedef uint32_t      DWORD;
typedef uint16_t      WORD;
typedef uint8_t       BYTE;
typedef int32_t       HRESULT;
typedef unsigned int  UINT;
typedef int           BOOL;
typedef wchar_t       WCHAR;
typedef const wchar_t *LPCWSTR;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define S_OK      ((HRESULT)0)
#define E_FAIL    ((HRESULT)0x80004005L)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

struct POINT { int32_t x, y; };
#endif

#define D3DX_PI 3.141592654f

// Minimal stand-in for D3DXVECTOR3, used by the mesh builders and shadow code.
struct D3DXVECTOR3
{
    float x, y, z;
    D3DXVECTOR3() : x(0), y(0), z(0) {}
    D3DXVECTOR3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    D3DXVECTOR3 operator-(const D3DXVECTOR3 &o) const { return D3DXVECTOR3(x - o.x, y - o.y, z - o.z); }
    D3DXVECTOR3 operator+(const D3DXVECTOR3 &o) const { return D3DXVECTOR3(x + o.x, y + o.y, z + o.z); }
};

inline D3DXVECTOR3 *D3DXVec3Cross(D3DXVECTOR3 *out, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
    D3DXVECTOR3 r(a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x);
    *out = r;
    return out;
}

inline D3DXVECTOR3 *D3DXVec3Normalize(D3DXVECTOR3 *out, const D3DXVECTOR3 *v)
{
    float l = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    if (l > 0.0f) *out = D3DXVECTOR3(v->x / l, v->y / l, v->z / l);
    else *out = *v;
    return out;
}

// Colour helpers matching D3DCOLOR_XRGB / D3DCOLOR_ARGB layout (0xAARRGGBB)
#define D3DCOLOR_ARGB(a, r, g, b) ((DWORD)((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_XRGB(r, g, b)    D3DCOLOR_ARGB(0xff, r, g, b)

// The game's vertex format (was D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1).
// `tex` selects a road texture layer (0..5) or -1 for untextured.
struct UTVERTEX
{
    D3DXVECTOR3 pos;   // game render units, Direct3D left-handed, +y up
    DWORD color;       // 0xAARRGGBB
    float tu, tv;      // texture co-ordinates
    float tex;         // road texture layer, or -1
};

// Debug log stream used throughout the original sources (was a file in DEBUG builds).
extern FILE *out;

// Error popups in the original code become log messages (Windows keeps the real MessageBox).
#if !defined(_WIN32)
#define MB_OK 0
inline int MessageBox(void *, const wchar_t *text, const wchar_t *caption, int)
{
    fwprintf(stderr, L"[StuntCarVR] %ls: %ls\n", caption ? caption : L"", text ? text : L"");
    return 0;
}
#endif

// Portable asset loading (implemented per platform: stdio for tools/tests, SDL for the app,
// which also reads from the APK on Android). Returns malloc'd memory or nullptr.
void *ScrLoadAsset(const char *relative_path, size_t *size_out);

#include "scr_sound.h"
