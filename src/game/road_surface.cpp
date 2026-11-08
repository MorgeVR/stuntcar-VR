// road_surface.cpp - see road_surface.h

#include "road_surface.h"
#include "scr_compat.h"
#include "track.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace scr {

namespace {

struct Tri { float x[3], y[3], z[3]; };

struct RoadGrid
{
    static const int N = NUM_TRACK_CUBES;
    static constexpr float kCell = (float)(CUBE_SIZE >> LOG_PRECISION);   // 4096 units per cube
    uint32_t version = 0xffffffff;
    std::vector<Tri> tris;
    std::vector<int> cells[N][N];

    void rebuild()
    {
        version = GetTrackMeshVersion();
        tris.clear();
        for (auto &row : cells) for (auto &c : row) c.clear();
        int32_t n = 0;
        const UTVERTEX *v = GetTrackVertices(&n);
        for (int32_t i = 0; i + 2 < n; i += 3)
        {
            if (v[i].tex < 0.0f) continue;   // road tops are textured; sides are not
            Tri t;
            for (int k = 0; k < 3; k++) { t.x[k] = v[i + k].pos.x; t.y[k] = v[i + k].pos.y; t.z[k] = v[i + k].pos.z; }
            int idx = (int)tris.size();
            tris.push_back(t);
            float minx = std::min({t.x[0], t.x[1], t.x[2]}), maxx = std::max({t.x[0], t.x[1], t.x[2]});
            float minz = std::min({t.z[0], t.z[1], t.z[2]}), maxz = std::max({t.z[0], t.z[1], t.z[2]});
            int cx0 = std::max(0, (int)(minx / kCell)), cx1 = std::min(N - 1, (int)(maxx / kCell));
            int cz0 = std::max(0, (int)(minz / kCell)), cz1 = std::min(N - 1, (int)(maxz / kCell));
            for (int cx = cx0; cx <= cx1; cx++)
                for (int cz = cz0; cz <= cz1; cz++) cells[cx][cz].push_back(idx);
        }
    }
};

RoadGrid g_grid;

} // namespace

bool RoadHeightAt(float x, float z, float y_ref, float max_above, float *y_out)
{
    if (g_grid.version != GetTrackMeshVersion()) g_grid.rebuild();
    int cx = (int)std::floor(x / RoadGrid::kCell), cz = (int)std::floor(z / RoadGrid::kCell);
    if (cx < 0 || cz < 0 || cx >= RoadGrid::N || cz >= RoadGrid::N) return false;
    bool found = false;
    float best = 0.0f;
    for (int i : g_grid.cells[cx][cz])
    {
        const Tri &t = g_grid.tris[i];
        // barycentric coordinates in x/z
        float d = (t.z[1] - t.z[2]) * (t.x[0] - t.x[2]) + (t.x[2] - t.x[1]) * (t.z[0] - t.z[2]);
        if (std::fabs(d) < 1e-6f) continue;
        float a = ((t.z[1] - t.z[2]) * (x - t.x[2]) + (t.x[2] - t.x[1]) * (z - t.z[2])) / d;
        float b = ((t.z[2] - t.z[0]) * (x - t.x[2]) + (t.x[0] - t.x[2]) * (z - t.z[2])) / d;
        float c = 1.0f - a - b;
        const float eps = -1e-4f;
        if (a < eps || b < eps || c < eps) continue;
        float y = a * t.y[0] + b * t.y[1] + c * t.y[2];
        if (y > y_ref + max_above) continue;   // a different (higher) piece of track
        if (!found || y > best) { best = y; found = true; }
    }
    if (found && y_out) *y_out = best;
    return found;
}

float CarGroundLift(const vm::vec3 &pos, const vm::quat &rot, float allowed_sink)
{
    // bottom of the four wheels and the front / rear of the body (car model units, see car.cpp)
    static const vm::vec3 pts[6] = {{-62, -40, 120}, {62, -40, 120}, {-62, -40, -120}, {62, -40, -120},
                                    {0, -20, 128}, {0, -20, -128}};
    float lift = 0.0f;
    for (const vm::vec3 &p : pts)
    {
        vm::vec3 w = pos + rot.rotate(p);
        float road = 0.0f;
        // only roads close below / slightly above the wheel count (not a track section overhead)
        if (RoadHeightAt(w.x, w.z, w.y, 150.0f, &road))
            lift = std::max(lift, road - w.y - allowed_sink);
    }
    return std::min(lift, 160.0f);
}

} // namespace scr
