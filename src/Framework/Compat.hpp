#pragma once

// ============================================================================
// Compat.hpp — compatibilidad entre toolchains del SDK.
//
// El SDK se desarrolla y se inyecta con cl.exe (MSVC). Para los builds de CI
// con GCC/MinGW o clang nativo hay piezas MSVC-only que hay que degradar:
//
//  1. SEH (__try/__except): extension MSVC. clang-cl la soporta (define
//     _MSC_VER); GCC/MinGW y clang nativo NO. MinGW tambien define
//     __try/__except en sus headers (windows.h/excpt.h) solo cuando se
//     habilita su SEH: anulamos esos simbolos y los remapeamos a ejecucion
//     directa (el cuerpo del __try se ejecuta, el del __except no). El build
//     final con cl.exe conserva la captura SEH real.
//
// Se incluye desde PCH.hpp DESPUES de windows.h para que nuestras
// definiciones tengan la ultima palabra.
// ============================================================================

#if !defined(_MSC_VER)
// Self-contained: aseguramos que los headers del sistema (que pueden definir
// __try/__except para su SEH) esten antes de remapear.
#    include <windows.h>

// NOTA: en GCC/MinGW-x64 (MSYS2) `__try` es una KEYWORD nativa del
// compilador que equivale a `try` — no se puede redefinir con #define.
// Por eso solo remapeamos __except para que forme un try/catch valido.
#    undef __except
#    ifndef __except
#        define __except(x) catch (...)
#    endif
#endif