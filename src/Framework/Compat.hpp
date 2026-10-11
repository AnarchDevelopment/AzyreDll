#pragma once

// ============================================================================
// Compat.hpp — compatibilidad entre toolchains del SDK.
//
// El SDK se desarrolla y se inyecta con cl.exe (MSVC). Para los builds de CI
// con GCC/MinGW o clang nativo hay piezas MSVC-only que hay que degradar:
//
//  1. SEH (__try/__except): extension MSVC. clang-cl la soporta (define
//     _MSC_VER); GCC/MinGW y clang nativo NO. En esos toolchains degradamos
//     el guard a ejecucion directa: el SDK compila igual (los guards quedan
//     sin captura, solo son relevantes para el build final con cl.exe).
//  2. Funciones seguras del CRT de Microsoft (*_s, _TRUNCATE, eh.h): no
//     existen en MinGW. Se resuelven en cada punto de uso con #ifdef.
//
// Se incluye desde PCH.hpp para que aplique a todos los .cpp del proyecto.
// ============================================================================

#if !defined(_MSC_VER)
#    ifndef __try
#        define __try if (true)
#    endif
#    ifndef __except
#        define __except(x) else
#    endif
#endif