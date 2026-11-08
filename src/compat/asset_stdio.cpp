// asset_stdio.cpp - ScrLoadAsset for tools and tests (plain files).
// Looks in $SCR_ASSET_DIR, then ./assets, ../assets, ../../assets.

#include "scr_compat.h"
#include <string>

static std::string g_asset_root;

void ScrSetAssetRoot(const char *root) { g_asset_root = root ? root : ""; }

static void *TryLoad(const std::string &path, size_t *size_out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return nullptr; }
    void *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return nullptr; }
    ((char *)buf)[n] = 0;
    if (size_out) *size_out = (size_t)n;
    return buf;
}

void *ScrLoadAsset(const char *relative_path, size_t *size_out)
{
    std::string roots[5];
    int n = 0;
    if (!g_asset_root.empty()) roots[n++] = g_asset_root;
    if (const char *env = getenv("SCR_ASSET_DIR")) roots[n++] = env;
    roots[n++] = "assets";
    roots[n++] = "../assets";
    roots[n++] = "../../assets";
    for (int i = 0; i < n; i++)
        if (void *p = TryLoad(roots[i] + "/" + relative_path, size_out)) return p;
    return nullptr;
}
