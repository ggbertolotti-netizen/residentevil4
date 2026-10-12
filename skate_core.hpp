#pragma once
// skate_core.hpp - Mecanica de skate independiente del juego.
//
// Unidades: metros, segundos, radianes. Eje Y hacia arriba.
// yaw = 0 mira hacia +Z; adelante = (sin(yaw), 0, cos(yaw)).
// El adaptador del juego (re4_adapter.hpp) se encarga de convertir
// unidades y ejes desde/hacia RE4. Este archivo no incluye nada de Windows,
// asi que se puede compilar y probar en cualquier plataforma (ver skate_demo.cpp).

#include <algorithm>
#include <cmath>

namespace skate {

struct Vec3 { float x = 0, y = 0, z = 0; };

struct Params {
    float gravity       = 9.81f;
    float pushAccel     = 3.2f;   // m/s^2 al empujar
    float maxPushSpeed  = 7.0f;   // velocidad maxima solo empujando
    float rollingDrag   = 0.25f;  // 1/s, perdida exponencial rodando
    float brakeDecel    = 7.0f;   // m/s^2 al frenar
    float turnRateSlow  = 1.6f;   // rad/s a baja velocidad
    float turnRateFast  = 0.7f;   // rad/s a turnFastSpeed o mas
    float turnFastSpeed = 8.0f;   // m/s
    float airTurnFactor = 0.3f;   // fraccion del giro disponible en el aire
    float ollieSpeed    = 4.2f;   // m/s vertical (~0.9 m de altura)
    float flickDown     = -0.6f;  // stick derecho abajo (umbral)
    float flickUp       =  0.6f;  // stick derecho arriba (umbral)
    float flickWindow   = 0.25f;  // s maximos entre soltar "abajo" y llegar a "arriba"
    float snapDown      = 0.15f;  // m: caida de suelo mayor a esto => en el aire
    float bailVelocity  = 9.0f;   // m/s de caida al aterrizar que provoca una caida (bail)
};

struct Input {
    float steer = 0;          // stick izquierdo X, -1..1 (derecha = positivo)
    float flick = 0;          // stick derecho Y, -1..1 (arriba = positivo)
    bool  push  = false;
    bool  brake = false;
    bool  ollieButton = false;  // atajo de prueba (equivale a un flick-it)
};

// Datos del terreno que el adaptador muestrea cada frame.
struct Ground {
    float heightHere  = 0;     // altura del suelo bajo la tabla
    float heightAhead = 0;     // altura del suelo probeDist metros adelante
    float probeDist   = 0.5f;
};

struct State {
    Vec3  pos;
    float yaw = 0;
    float speed = 0;           // velocidad horizontal hacia adelante (m/s)
    float vy = 0;              // velocidad vertical (m/s)
    bool  onGround = true;
    bool  bailed = false;      // true durante el frame en que ocurre una caida
    // estado interno
    float flickTimer = 0;
    bool  prevOllieButton = false;
    float prevHeight = 0;      // altura del suelo en el frame anterior
    bool  haveHeight = false;
    float lastRiseRate = 0;    // m/s con que subia el suelo bajo la tabla (para salir de rampas)
};

inline void spawn(State& s, Vec3 pos, float yaw, float speed = 0) {
    s = State{};
    s.pos = pos;
    s.yaw = yaw;
    s.speed = speed;
}

inline void step(State& s, const Input& in, const Ground& g, const Params& p, float dt) {
    s.bailed = false;

    // --- flick-it: stick derecho abajo y luego arriba dentro de la ventana ---
    bool ollie = false;
    if (in.flick <= p.flickDown) {
        s.flickTimer = p.flickWindow;            // armado mientras se mantiene abajo
    } else if (s.flickTimer > 0) {
        s.flickTimer -= dt;
        if (in.flick >= p.flickUp) { ollie = true; s.flickTimer = 0; }
    }
    if (in.ollieButton && !s.prevOllieButton) ollie = true;
    s.prevOllieButton = in.ollieButton;

    // --- pendiente del suelo (seno del angulo, + = cuesta arriba) ---
    const float dh = g.heightAhead - g.heightHere;
    const float slopeSin = dh / std::sqrt(dh * dh + g.probeDist * g.probeDist);

    if (s.onGround && ollie) {
        s.onGround = false;
        s.vy = p.ollieSpeed;
    }

    const float fx = std::sin(s.yaw);
    const float fz = std::cos(s.yaw);

    if (s.onGround) {
        // direccion: giro mas lento a mas velocidad
        const float t = std::clamp(s.speed / p.turnFastSpeed, 0.0f, 1.0f);
        const float turnRate = p.turnRateSlow + (p.turnRateFast - p.turnRateSlow) * t;
        s.yaw += in.steer * turnRate * dt;

        // velocidad
        if (in.push && s.speed < p.maxPushSpeed)
            s.speed += p.pushAccel * (1.0f - s.speed / p.maxPushSpeed) * dt;
        if (in.brake)
            s.speed = std::max(0.0f, s.speed - p.brakeDecel * dt);
        s.speed -= s.speed * p.rollingDrag * dt;
        s.speed -= p.gravity * slopeSin * dt;
        s.speed = std::max(0.0f, s.speed);

        s.pos.x += fx * s.speed * dt;
        s.pos.z += fz * s.speed * dt;

        // altura: pegado al suelo, o al aire si el suelo desaparece (borde de rampa, escalon)
        const float rise = s.haveHeight ? (g.heightHere - s.prevHeight) / dt : 0.0f;
        if (s.pos.y - g.heightHere > p.snapDown) {
            s.onGround = false;
            s.vy = std::max(0.0f, s.lastRiseRate);             // sale con la velocidad vertical que traia la rampa
        } else {
            s.pos.y = g.heightHere;
            s.lastRiseRate = rise;
        }
        s.prevHeight = g.heightHere;
        s.haveHeight = true;
    } else {
        s.yaw += in.steer * p.turnRateSlow * p.airTurnFactor * dt;
        s.vy -= p.gravity * dt;
        s.pos.x += fx * s.speed * dt;
        s.pos.z += fz * s.speed * dt;
        s.pos.y += s.vy * dt;

        if (s.vy <= 0 && s.pos.y <= g.heightHere) {            // aterrizaje
            if (-s.vy > p.bailVelocity) { s.bailed = true; s.speed = 0; }
            s.pos.y = g.heightHere;
            s.vy = 0;
            s.onGround = true;
            s.haveHeight = false;
            s.lastRiseRate = 0;
        }
    }
}

} // namespace skate
