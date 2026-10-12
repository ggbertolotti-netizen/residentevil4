#pragma once
// re4_adapter.hpp - Puente entre skate_core y Resident Evil 4 UHD (Steam, 32 bits).
//
// IMPORTANTE: las direcciones y offsets de abajo estan en 0 A PROPOSITO.
// No los conozco con certeza y prefiero no inventarlos: un offset equivocado
// escribe basura en memoria del juego. Se obtienen con ingenieria inversa
// (Cheat Engine / Ghidra / x32dbg) o mirando las estructuras que ya documenta
// el proyecto re4_tweaks para la version de Steam.
//
// Mientras kPlayerPtrAddr sea 0 el plugin corre en "modo simulacion": lee tus
// teclas/mando, mueve una tabla virtual y escribe el resultado en re4_skate.log,
// sin tocar la memoria del juego. Sirve para comprobar que el .asi carga y que
// los controles responden antes de conectar nada.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include "skate_core.hpp"

namespace re4 {

// ---- A COMPLETAR ----------------------------------------------------------
constexpr uintptr_t kPlayerPtrAddr = 0;      // direccion del puntero global al objeto del jugador (Leon)
constexpr uint32_t  kOffPosX = 0;            // offsets dentro del objeto jugador
constexpr uint32_t  kOffPosY = 0;
constexpr uint32_t  kOffPosZ = 0;
constexpr uint32_t  kOffYaw  = 0;            // rotacion alrededor del eje vertical
constexpr float     kUnitsPerMeter = 100.0f; // VERIFICAR: cuantas unidades del juego son 1 metro
constexpr float     kYawSign   = 1.0f;       // ajustar si el juego gira al reves que skate_core
constexpr float     kYawOffset = 0.0f;       // ajustar si el yaw 0 del juego no mira a +Z
// Si el eje vertical del juego no es Y, cambiarlo en readPlayer/writePlayer.
// ---------------------------------------------------------------------------

constexpr bool configured() { return kPlayerPtrAddr != 0; }

// Acceso a memoria tolerante a punteros invalidos (SEH en MSVC).
template <class T> inline bool rd(uintptr_t addr, T& out) {
#if defined(_MSC_VER)
    __try { out = *reinterpret_cast<const T*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    out = *reinterpret_cast<const T*>(addr); return true;
#endif
}
template <class T> inline bool wr(uintptr_t addr, const T& v) {
#if defined(_MSC_VER)
    __try { *reinterpret_cast<T*>(addr) = v; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    *reinterpret_cast<T*>(addr) = v; return true;
#endif
}

inline bool playerBase(uintptr_t& base) {
    if (!configured()) return false;
    uintptr_t p = 0;
    if (!rd(kPlayerPtrAddr, p) || p == 0) return false;
    base = p;
    return true;
}

// Posicion en metros y yaw en radianes (convencion de skate_core).
inline bool readPlayer(skate::Vec3& pos, float& yaw) {
    uintptr_t b;
    float x, y, z, r;
    if (!playerBase(b)) return false;
    if (!rd(b + kOffPosX, x) || !rd(b + kOffPosY, y) || !rd(b + kOffPosZ, z) || !rd(b + kOffYaw, r)) return false;
    pos = { x / kUnitsPerMeter, y / kUnitsPerMeter, z / kUnitsPerMeter };
    yaw = (r - kYawOffset) * kYawSign;
    return true;
}

inline bool writePlayer(const skate::Vec3& pos, float yaw) {
    uintptr_t b;
    if (!playerBase(b)) return false;
    return wr(b + kOffPosX, pos.x * kUnitsPerMeter) && wr(b + kOffPosY, pos.y * kUnitsPerMeter) &&
           wr(b + kOffPosZ, pos.z * kUnitsPerMeter) && wr(b + kOffYaw, yaw * kYawSign + kYawOffset);
}

// Altura del suelo en (x, z) en metros. false = no disponible (el plugin usa suelo plano).
// TODO: llamar a la consulta de colisiones del juego.
inline bool sampleGround(float /*x*/, float /*z*/, float& /*y*/) { return false; }

// TODO: bloquear el movimiento/acciones nativas de Leon mientras se patina (si no, el juego
// sigue interpretando el stick y los gatillos). Normalmente se hace enganchando la funcion que
// lee el pad o la actualizacion del jugador.
inline void setNativeInputBlocked(bool /*blocked*/) {}

} // namespace re4
