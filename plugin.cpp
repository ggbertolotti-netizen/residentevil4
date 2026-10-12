// plugin.cpp - Plugin .asi: skate en RE4 UHD (prototipo).
//
// Controles (editar las constantes de abajo y recompilar):
//   Alternar tabla : F6 o boton Back del mando
//   Empujar        : Flecha arriba   | gatillo derecho
//   Frenar         : Flecha abajo    | gatillo izquierdo
//   Girar          : Flechas izq/der | stick izquierdo
//   Ollie          : Espacio / A     | flick-it con el stick derecho (abajo y arriba rapido)
//
// Esta version usa un hilo propio a ~120 Hz. Para una integracion final conviene
// enganchar el bucle principal del juego en lugar de un hilo aparte.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include "skate_core.hpp"
#include "re4_adapter.hpp"

namespace {

constexpr int   kToggleKey        = VK_F6;
constexpr BYTE  kTriggerThreshold = 30;
constexpr float kStickDeadzone    = 0.2f;
constexpr float kStep             = 1.0f / 120.0f;
constexpr float kProbeDist        = 0.5f;

FILE* g_log = nullptr;
void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list a; va_start(a, fmt);
    std::vfprintf(g_log, fmt, a);
    va_end(a);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
XInputGetStateFn LoadXInput() {
    const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (const char* d : dlls) {
        if (HMODULE m = LoadLibraryA(d)) {
            if (FARPROC f = GetProcAddress(m, "XInputGetState")) return reinterpret_cast<XInputGetStateFn>(f);
        }
    }
    return nullptr;
}

bool GameFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

float Axis(SHORT v) {
    const float f = std::max(-1.0f, std::min(1.0f, v / 32767.0f));
    return std::fabs(f) < kStickDeadzone ? 0.0f : f;
}

// Lee teclado + mando. 'toggle' se activa solo en el flanco de subida.
void ReadInput(XInputGetStateFn xget, skate::Input& in, bool& toggle) {
    static bool prevToggle = false;
    in = {};
    bool toggleHeld = false;

    if (GameFocused()) {
        auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        in.steer       = (down(VK_RIGHT) ? 1.0f : 0.0f) - (down(VK_LEFT) ? 1.0f : 0.0f);
        in.push        = down(VK_UP);
        in.brake       = down(VK_DOWN);
        in.ollieButton = down(VK_SPACE);
        toggleHeld     = down(kToggleKey);
    }
    if (xget) {
        XINPUT_STATE st{};
        if (xget(0, &st) == ERROR_SUCCESS) {
            const XINPUT_GAMEPAD& g = st.Gamepad;
            if (const float x = Axis(g.sThumbLX); x != 0.0f) in.steer = x;
            in.flick        = Axis(g.sThumbRY);
            in.push        |= g.bRightTrigger > kTriggerThreshold;
            in.brake       |= g.bLeftTrigger  > kTriggerThreshold;
            in.ollieButton |= (g.wButtons & XINPUT_GAMEPAD_A) != 0;
            toggleHeld     |= (g.wButtons & XINPUT_GAMEPAD_BACK) != 0;
        }
    }
    toggle = toggleHeld && !prevToggle;
    prevToggle = toggleHeld;
}

DWORD WINAPI Worker(LPVOID) {
    g_log = std::fopen("re4_skate.log", "a");
    Log("re4_skate cargado. Offsets de RE4 configurados: %s",
        re4::configured() ? "si" : "no (modo simulacion, no se toca la memoria del juego)");

    XInputGetStateFn xget = LoadXInput();
    const skate::Params params;
    skate::State st;
    bool active = false;
    float flatY = 0.0f;

    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);
    float acc = 0.0f, logTimer = 0.0f;

    for (;;) {
        Sleep(4);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt = std::min(0.05f, float(double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart)));
        prev = now;

        skate::Input in;
        bool toggle = false;
        ReadInput(xget, in, toggle);

        if (toggle) {
            active = !active;
            if (active) {
                skate::Vec3 pos{};
                float yaw = 0.0f;
                if (!re4::readPlayer(pos, yaw)) { pos = {}; yaw = 0.0f; }
                flatY = pos.y;
                skate::spawn(st, pos, yaw, 0.0f);
                re4::setNativeInputBlocked(true);
                Log("Tabla ON  pos=(%.2f, %.2f, %.2f) yaw=%.2f", pos.x, pos.y, pos.z, yaw);
            } else {
                re4::setNativeInputBlocked(false);
                Log("Tabla OFF");
            }
        }
        if (!active) { acc = 0.0f; continue; }

        auto ground = [&](float x, float z) { float y; return re4::sampleGround(x, z, y) ? y : flatY; };
        acc += dt;
        while (acc >= kStep) {
            skate::Ground g;
            g.probeDist   = kProbeDist;
            g.heightHere  = ground(st.pos.x, st.pos.z);
            g.heightAhead = ground(st.pos.x + std::sin(st.yaw) * kProbeDist,
                                   st.pos.z + std::cos(st.yaw) * kProbeDist);
            skate::step(st, in, g, params, kStep);
            if (st.bailed) Log("Bail (caida)");
            acc -= kStep;
        }
        re4::writePlayer(st.pos, st.yaw);

        logTimer += dt;
        if (logTimer >= 1.0f) {
            logTimer = 0.0f;
            Log("vel=%.2f m/s pos=(%.2f, %.2f, %.2f) yaw=%.2f %s", st.speed,
                st.pos.x, st.pos.y, st.pos.z, st.yaw, st.onGround ? "suelo" : "aire");
        }
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
