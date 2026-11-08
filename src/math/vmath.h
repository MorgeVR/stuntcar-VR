// vmath.h - tiny vector / quaternion / matrix library (column-major, column vectors,
// right-multiplied: p' = M * p), matching OpenGL and OpenXR conventions.
#pragma once

#include <cmath>
#include <cstring>

namespace vm {

constexpr float kPi = 3.14159265358979323846f;

struct vec3
{
    float x = 0, y = 0, z = 0;
    vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    vec3 operator+(const vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3 &operator+=(const vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline float dot(const vec3 &a, const vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3 &a, const vec3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const vec3 &v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(const vec3 &v) { float l = length(v); return l > 0 ? v * (1.0f / l) : v; }
inline vec3 lerp(const vec3 &a, const vec3 &b, float t) { return a + (b - a) * t; }

struct quat
{
    float x = 0, y = 0, z = 0, w = 1;
    quat() = default;
    constexpr quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static quat axisAngle(const vec3 &axis, float radians)
    {
        vec3 a = normalize(axis);
        float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }
    quat operator*(const quat &b) const
    {
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }
    quat conjugate() const { return {-x, -y, -z, w}; }
    vec3 rotate(const vec3 &v) const
    {
        vec3 u(x, y, z);
        vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
};

inline quat normalize(const quat &q)
{
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 0 ? quat(q.x / l, q.y / l, q.z / l, q.w / l) : quat();
}

inline quat slerp(quat a, quat b, float t)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = quat(-b.x, -b.y, -b.z, -b.w); d = -d; }
    if (d > 0.9995f)
        return normalize(quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Angle (radians) between two orientations
inline float angleBetween(const quat &a, const quat &b)
{
    float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    if (d > 1.0f) d = 1.0f;
    return 2.0f * std::acos(d);
}

struct mat4
{
    float m[16]; // column-major: m[col*4 + row]

    mat4() { std::memset(m, 0, sizeof(m)); m[0] = m[5] = m[10] = m[15] = 1.0f; }
    float &operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }

    mat4 operator*(const mat4 &b) const
    {
        mat4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++)
            {
                float s = 0;
                for (int k = 0; k < 4; k++) s += (*this)(rr, k) * b(k, c);
                r(rr, c) = s;
            }
        return r;
    }
    vec3 transformPoint(const vec3 &p) const
    {
        float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (w != 0.0f && w != 1.0f) { x /= w; y /= w; z /= w; }
        return {x, y, z};
    }
    void transform4(const float in[4], float outv[4]) const
    {
        for (int r = 0; r < 4; r++)
            outv[r] = (*this)(r, 0) * in[0] + (*this)(r, 1) * in[1] + (*this)(r, 2) * in[2] + (*this)(r, 3) * in[3];
    }

    static mat4 translation(const vec3 &t) { mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static mat4 scale(const vec3 &s) { mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
    static mat4 rotation(const quat &q)
    {
        mat4 r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r(0, 0) = 1 - 2 * (yy + zz); r(0, 1) = 2 * (xy - wz);     r(0, 2) = 2 * (xz + wy);
        r(1, 0) = 2 * (xy + wz);     r(1, 1) = 1 - 2 * (xx + zz); r(1, 2) = 2 * (yz - wx);
        r(2, 0) = 2 * (xz - wy);     r(2, 1) = 2 * (yz + wx);     r(2, 2) = 1 - 2 * (xx + yy);
        return r;
    }
    static mat4 trs(const vec3 &t, const quat &q, const vec3 &s = vec3(1, 1, 1))
    {
        return translation(t) * rotation(q) * scale(s);
    }

    // Right-handed perspective (OpenGL clip space, z in -w..w), looking down -z.
    static mat4 perspective(float fovy_radians, float aspect, float zn, float zf)
    {
        float f = 1.0f / std::tan(fovy_radians * 0.5f);
        mat4 r; std::memset(r.m, 0, sizeof(r.m));
        r(0, 0) = f / aspect;
        r(1, 1) = f;
        r(2, 2) = (zf + zn) / (zn - zf);
        r(2, 3) = (2 * zf * zn) / (zn - zf);
        r(3, 2) = -1;
        return r;
    }
    // Asymmetric frustum from OpenXR XrFovf angles (radians), OpenGL clip space.
    static mat4 perspectiveFov(float left, float right, float up, float down, float zn, float zf)
    {
        float l = std::tan(left), r = std::tan(right), u = std::tan(up), d = std::tan(down);
        mat4 p; std::memset(p.m, 0, sizeof(p.m));
        p(0, 0) = 2.0f / (r - l);
        p(0, 2) = (r + l) / (r - l);
        p(1, 1) = 2.0f / (u - d);
        p(1, 2) = (u + d) / (u - d);
        p(2, 2) = -(zf + zn) / (zf - zn);
        p(2, 3) = -(2.0f * zf * zn) / (zf - zn);
        p(3, 2) = -1.0f;
        return p;
    }
    static mat4 ortho(float l, float r, float b, float t, float n, float f)
    {
        mat4 o;
        o(0, 0) = 2 / (r - l); o(1, 1) = 2 / (t - b); o(2, 2) = -2 / (f - n);
        o(0, 3) = -(r + l) / (r - l); o(1, 3) = -(t + b) / (t - b); o(2, 3) = -(f + n) / (f - n);
        return o;
    }
};

// Inverse of a rigid transform (rotation + translation, no scale)
inline mat4 rigidInverse(const vec3 &t, const quat &q)
{
    quat qi = q.conjugate();
    vec3 ti = qi.rotate(-t);
    return mat4::trs(ti, qi);
}

struct Pose
{
    vec3 position;
    quat orientation;
    vec3 transformPoint(const vec3 &p) const { return orientation.rotate(p) + position; }
    Pose operator*(const Pose &child) const // compose: this * child
    {
        return {orientation.rotate(child.position) + position, normalize(orientation * child.orientation)};
    }
    Pose inverse() const
    {
        quat qi = orientation.conjugate();
        return {qi.rotate(-position), qi};
    }
    mat4 matrix() const { return mat4::trs(position, orientation); }
};

inline Pose lerpPose(const Pose &a, const Pose &b, float t)
{
    return {lerp(a.position, b.position, t), slerp(a.orientation, b.orientation, t)};
}

} // namespace vm
