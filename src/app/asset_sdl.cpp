// asset_sdl.cpp - ScrLoadAsset via SDL (desktop: <exe dir>/assets, Android: APK assets)

#include "scr_compat.h"
#include <SDL3/SDL.h>
#include <string>

void *ScrLoadAsset(const char *relative_path, size_t *size_out)
{
    size_t size = 0;
    void *data = nullptr;
#if defined(__ANDROID__)
    // SDL opens relative paths from the APK's assets folder
    data = SDL_LoadFile(relative_path, &size);
#else
    if (const char *env = SDL_getenv("SCR_ASSET_DIR"))
        data = SDL_LoadFile((std::string(env) + "/" + relative_path).c_str(), &size);
    if (!data)
    {
        const char *base = SDL_GetBasePath();
        if (base) data = SDL_LoadFile((std::string(base) + "assets/" + relative_path).c_str(), &size);
    }
    if (!data) data = SDL_LoadFile((std::string("assets/") + relative_path).c_str(), &size);
#endif
    if (!data) return nullptr;
    // SDL_LoadFile memory must be released with SDL_free; callers use free(), so copy.
    void *copy = malloc(size + 1);
    memcpy(copy, data, size);
    ((char *)copy)[size] = 0;
    SDL_free(data);
    if (size_out) *size_out = size;
    return copy;
}
