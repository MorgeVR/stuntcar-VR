// road_surface.h - read-only height queries against the track's road surface mesh.
// Used to keep the drawn cars on the road between physics ticks (visual only).
#pragma once

#include "vmath.h"

namespace scr {

// Highest road surface at (x, z) that is no more than `max_above` above y_ref (game units).
// Returns false if there is no road there.
bool RoadHeightAt(float x, float z, float y_ref, float max_above, float *y_out);

// How far (game units) a car model at `pos`/`rot` must be raised so none of its wheels
// is more than `allowed_sink` below the road. 0 if it isn't touching the road.
float CarGroundLift(const vm::vec3 &pos, const vm::quat &rot, float allowed_sink);

} // namespace scr
