#pragma once

namespace mc::placestats {

inline unsigned long long attempts = 0;
inline bool lastOk = false;
inline int lastX = 0;
inline int lastY = 0;
inline int lastZ = 0;
inline bool gmValid = false;
inline bool inputValid = false;
inline int fastPlaceMs = -1;

}
