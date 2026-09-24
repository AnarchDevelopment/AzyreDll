#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace mc {

constexpr float kPi = 3.14159265358979323846f;

inline float deg2rad(float d) { return d * kPi / 180.0f; }
inline float rad2deg(float r) { return r * 180.0f / kPi; }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

struct Vec4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct BlockPos
{
    int x = 0;
    int y = 0;
    int z = 0;

    BlockPos() = default;
    BlockPos(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const BlockPos& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator<(const BlockPos& o) const
    {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
};

struct BlockPosHash
{
    size_t operator()(const BlockPos& p) const
    {
        return (size_t)((uint32_t)p.x * 73856093u) ^
               (size_t)((uint32_t)p.y * 19349663u) ^
               (size_t)((uint32_t)p.z * 83492791u);
    }
};

inline float vecLength(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline float vecLength2D(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}

inline float distance3D(const Vec3& a, const Vec3& b)
{
    return vecLength(b - a);
}

inline Vec3 normalize(const Vec3& v)
{
    float len = vecLength(v);
    if (len <= 1e-6f)
        return {};
    return v * (1.0f / len);
}

inline float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float wrapDegrees(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

inline float angleDiff(float from, float to)
{
    return wrapDegrees(to - from);
}

struct AimAngles
{
    float yaw = 0.0f;
    float pitch = 0.0f;
};

inline AimAngles calcAngles(const Vec3& from, const Vec3& to)
{
    AimAngles out;
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float hDist = std::sqrt(dx * dx + dz * dz);
    out.yaw = rad2deg(std::atan2(-dx, dz));
    out.pitch = rad2deg(std::atan2(-dy, hDist));
    out.yaw = wrapDegrees(out.yaw);
    out.pitch = clampf(out.pitch, -90.0f, 90.0f);
    return out;
}

inline bool worldToScreen(const Vec3& world, const Vec3& cam, float yaw, float pitch,
                          float fovDeg, float screenW, float screenH, Vec3& out)
{
    float yR = deg2rad(yaw);
    float pR = deg2rad(pitch);
    float cosY = std::cos(yR), sinY = std::sin(yR);
    float cosP = std::cos(pR), sinP = std::sin(pR);

    float fx = -sinY * cosP;
    float fy = -sinP;
    float fz = cosY * cosP;

    float rx = -cosY;
    float ry = 0.0f;
    float rz = -sinY;

    float ux = -sinY * sinP;
    float uy = cosP;
    float uz = cosY * sinP;

    float dx = world.x - cam.x;
    float dy = world.y - cam.y;
    float dz = world.z - cam.z;

    float viewZ = dx * fx + dy * fy + dz * fz;
    if (viewZ <= 0.1f)
        return false;

    float viewX = dx * rx + dy * ry + dz * rz;
    float viewY = dx * ux + dy * uy + dz * uz;

    float fovVR = deg2rad(fovDeg);
    float tanHalfV = std::tan(fovVR * 0.5f);
    if (tanHalfV <= 0.001f)
        return false;
    float tanHalfH = tanHalfV * (screenW / screenH);

    float ndcX = (viewX / viewZ) / tanHalfH;
    float ndcY = (viewY / viewZ) / tanHalfV;

    out.x = (ndcX * 0.5f + 0.5f) * screenW;
    out.y = (0.5f - ndcY * 0.5f) * screenH;
    out.z = viewZ;
    return true;
}

inline Vec3 airStrafe(const Vec3& velocity, const Vec3& wishDir, float maxAirSpeed,
                      float airAccel, float dt)
{
    Vec3 d = normalize(wishDir);
    if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f)
        return velocity;

    float vProj = dot(velocity, d);
    float speed2D = vecLength2D(velocity);
    if (speed2D > maxAirSpeed)
        return velocity;

    float deltaLimit = maxAirSpeed - vProj;
    if (deltaLimit < 0.0f)
        deltaLimit = 0.0f;

    float wishSpeed = clampf(vecLength2D(wishDir), 0.0f, 1.0f);
    float deltaTeorica = airAccel * wishSpeed * dt;
    float delta = deltaTeorica < deltaLimit ? deltaTeorica : deltaLimit;

    Vec3 result = velocity + d * delta;
    return result;
}

inline float optimalAirStrafeAngle(float maxAirSpeed, float speed)
{
    if (speed <= maxAirSpeed)
        return 0.0f;
    float ratio = clampf(maxAirSpeed / speed, -1.0f, 1.0f);
    return rad2deg(std::acos(ratio));
}

inline Vec3 wishDirFromYaw(float yawDeg, float forward, float rightAmount)
{
    float y = deg2rad(yawDeg);
    float cosY = std::cos(y), sinY = std::sin(y);
    Vec3 fwd{-sinY, 0.0f, cosY};
    Vec3 right{-cosY, 0.0f, -sinY};
    return normalize(fwd * forward + right * rightAmount);
}

inline Vec3 applyFriction(const Vec3& velocity, float mu, float dt)
{
    float speed = vecLength(velocity);
    if (speed <= 1e-6f)
        return velocity;
    float frictionDist = speed * mu * dt;
    float scale = 1.0f - frictionDist / speed;
    if (scale < 0.0f)
        scale = 0.0f;
    return velocity * scale;
}

inline BlockPos blockFromVec(const Vec3& v)
{
    return {(int)std::floor(v.x), (int)std::floor(v.y), (int)std::floor(v.z)};
}

inline Vec3 blockCenter(const BlockPos& b)
{
    return {b.x + 0.5f, b.y + 0.5f, b.z + 0.5f};
}

inline void aabbCorners(const BlockPos& b, Vec3 out[8])
{
    float bx = (float)b.x, by = (float)b.y, bz = (float)b.z;
    int i = 0;
    for (int yi = 0; yi <= 1; ++yi)
        for (int zi = 0; zi <= 1; ++zi)
            for (int xi = 0; xi <= 1; ++xi)
                out[i++] = {bx + (float)xi, by + (float)yi, bz + (float)zi};
}

inline Vec3 predictVelocity(const Vec3& pos, const Vec3& vel, int ticks)
{
    return pos + vel * (float)ticks;
}

inline uint32_t floatBits(float v)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

inline float bitsFloat(uint32_t bits)
{
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

}
