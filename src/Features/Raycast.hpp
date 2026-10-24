#pragma once

// ============================================================================
// Raycast — traversal DDA (Amanatides & Woo) sobre el BlockSource.
//
// Devuelve el primer bloque NO aire en el camino. Se apoya en
// BlockSource::getBlockId, que a su vez usa el id candidato de off::block.
// Si la lectura de bloques no esta calibrada, getBlockId devuelve -1 y el
// raycast simplemente no encuentra nada (degradacion segura).
// ============================================================================

#include "Features/WorldChunks.hpp"
#include "Framework/Math.hpp"
#include "SDK/Classes/BlockSource.hpp"

#include <cmath>
#include <limits>

namespace mc {

struct RaycastHit
{
    bool hit = false;
    BlockPos pos;
    int face = -1;    // 0=abajo,1=arriba,2=norte(-z),3=sur(+z),4=oeste(-x),5=este(+x)
    int blockId = -1;
    Vec3 point;       // punto de entrada en el bloque
    float distance = 0.0f;
};

// dir: direccion (se normaliza). maxDist en bloques.
inline RaycastHit raycastBlock(const BlockSource& bs, const Vec3& origin, const Vec3& dir,
                               float maxDist)
{
    RaycastHit h;
    if (!bs.valid() || maxDist <= 0.0f)
        return h;

    Vec3 d = normalize(dir);
    if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f)
        return h;

    const float kInf = std::numeric_limits<float>::infinity();

    int x = (int)std::floor(origin.x);
    int y = (int)std::floor(origin.y);
    int z = (int)std::floor(origin.z);

    int stepX = (d.x > 0.0f) ? 1 : (d.x < 0.0f ? -1 : 0);
    int stepY = (d.y > 0.0f) ? 1 : (d.y < 0.0f ? -1 : 0);
    int stepZ = (d.z > 0.0f) ? 1 : (d.z < 0.0f ? -1 : 0);

    float tDeltaX = stepX != 0 ? std::fabs(1.0f / d.x) : kInf;
    float tDeltaY = stepY != 0 ? std::fabs(1.0f / d.y) : kInf;
    float tDeltaZ = stepZ != 0 ? std::fabs(1.0f / d.z) : kInf;

    float tMaxX = stepX > 0 ? ((x + 1 - origin.x) / d.x)
                            : (stepX < 0 ? ((x - origin.x) / d.x) : kInf);
    float tMaxY = stepY > 0 ? ((y + 1 - origin.y) / d.y)
                            : (stepY < 0 ? ((y - origin.y) / d.y) : kInf);
    float tMaxZ = stepZ > 0 ? ((z + 1 - origin.z) / d.z)
                            : (stepZ < 0 ? ((z - origin.z) / d.z) : kInf);

    int face = -1;
    float t = 0.0f;
    const int kMaxSteps = 512;

    for (int i = 0; i < kMaxSteps && t <= maxDist; ++i)
    {
        int id = bs.getBlockId(x, y, z);
        if (id <= 0)
            id = world::getBlockId(bs, x, y, z);

        if (id > 0)
        {
            h.hit = true;
            h.pos = {x, y, z};
            h.face = face;
            h.blockId = id;
            h.distance = t;
            h.point = {origin.x + d.x * t, origin.y + d.y * t, origin.z + d.z * t};
            return h;
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            face = stepX > 0 ? 4 : 5;
        }
        else if (tMaxY < tMaxZ)
        {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            face = stepY > 0 ? 0 : 1;
        }
        else
        {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            face = stepZ > 0 ? 2 : 3;
        }
    }

    return h;
}

}
