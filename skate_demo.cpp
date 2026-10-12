// skate_demo.cpp - Prueba offline del nucleo de skate (no necesita el juego).
// Compilar:  g++ -std=c++17 -O2 src/skate_demo.cpp -o skate_demo && ./skate_demo
#include "skate_core.hpp"
#include <cstdio>
#include <cstdlib>

using namespace skate;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FALLO: %s\n", msg); ++g_fail; } \
                              else std::printf("  ok:    %s\n", msg); } while (0)

// Terreno de prueba: plano, con una rampa de 1 m de alto entre z=20 y z=22 y borde abrupto.
static float heightAt(float /*x*/, float z) {
    return (z >= 20.0f && z < 22.0f) ? (z - 20.0f) * 0.5f : 0.0f;
}

static Ground sample(const State& s, const Params& p) {
    Ground g;
    g.probeDist = 0.5f;
    g.heightHere = heightAt(s.pos.x, s.pos.z);
    g.heightAhead = heightAt(s.pos.x + std::sin(s.yaw) * g.probeDist,
                             s.pos.z + std::cos(s.yaw) * g.probeDist);
    (void)p;
    return g;
}

int main() {
    const Params p;
    const float dt = 1.0f / 60.0f;

    // ---- 1) empujar: acelera pero no pasa de maxPushSpeed ----
    std::printf("[1] Empujar 5 s en plano\n");
    {
        State s; spawn(s, {0, 0, 0}, 0);
        Input in; in.push = true;
        for (int i = 0; i < 300; ++i) step(s, in, sample(s, p), p, dt);
        std::printf("    velocidad = %.2f m/s, z = %.2f m\n", s.speed, s.pos.z);
        CHECK(s.speed > 4.0f, "alcanza velocidad razonable (> 4 m/s)");
        CHECK(s.speed < p.maxPushSpeed, "no supera la velocidad maxima de empuje");
        CHECK(s.pos.z > 10.0f, "avanza hacia adelante (+Z)");
    }

    // ---- 2) inercia y freno ----
    std::printf("[2] Rodar y frenar\n");
    {
        State s; spawn(s, {0, 0, 0}, 0, 6.0f);
        Input in;
        for (int i = 0; i < 120; ++i) step(s, in, sample(s, p), p, dt);
        const float coast = s.speed;
        std::printf("    tras 2 s rodando = %.2f m/s\n", coast);
        CHECK(coast < 6.0f && coast > 3.0f, "pierde velocidad de a poco rodando");
        in.brake = true;
        for (int i = 0; i < 60; ++i) step(s, in, sample(s, p), p, dt);
        CHECK(s.speed == 0.0f, "frena hasta detenerse");
    }

    // ---- 3) giro ----
    std::printf("[3] Girar a la derecha 1 s\n");
    {
        State s; spawn(s, {0, 0, 0}, 0, 5.0f);
        Input in; in.steer = 1.0f;
        for (int i = 0; i < 60; ++i) step(s, in, sample(s, p), p, dt);
        std::printf("    yaw = %.2f rad, x = %.2f m\n", s.yaw, s.pos.x);
        CHECK(s.yaw > 0.8f, "el yaw aumenta al girar a la derecha");
        CHECK(s.pos.x > 0.5f, "la trayectoria se curva hacia +X");
    }

    // ---- 4) ollie con flick-it (stick abajo y arriba) ----
    std::printf("[4] Ollie con flick-it\n");
    {
        State s; spawn(s, {0, 0, 0}, 0, 5.0f);
        Input in;
        for (int i = 0; i < 30; ++i) step(s, in, sample(s, p), p, dt);      // rodando
        in.flick = -1.0f;
        for (int i = 0; i < 6; ++i) step(s, in, sample(s, p), p, dt);        // abajo
        in.flick = 0.0f;
        step(s, in, sample(s, p), p, dt);                                    // neutro
        in.flick = 1.0f;
        float maxY = 0; bool wasAir = false, landed = false;
        for (int i = 0; i < 90; ++i) {
            step(s, in, sample(s, p), p, dt);
            in.flick = 0.0f;
            maxY = std::max(maxY, s.pos.y);
            if (!s.onGround) wasAir = true;
            if (wasAir && s.onGround) { landed = true; break; }
        }
        std::printf("    altura maxima = %.2f m\n", maxY);
        CHECK(wasAir, "el flick-it despega la tabla");
        CHECK(maxY > 0.8f && maxY < 1.0f, "altura de ollie entre 0.8 y 1.0 m");
        CHECK(landed && s.pos.y == 0.0f, "aterriza de nuevo en el suelo sin bail");
        CHECK(!s.bailed, "no hay caida en un ollie normal");
    }

    // ---- 5) flick demasiado lento no cuenta ----
    std::printf("[5] Flick lento (stick abajo, pausa larga, arriba)\n");
    {
        State s; spawn(s, {0, 0, 0}, 0, 5.0f);
        Input in; in.flick = -1.0f;
        for (int i = 0; i < 6; ++i) step(s, in, sample(s, p), p, dt);
        in.flick = 0.0f;
        for (int i = 0; i < 30; ++i) step(s, in, sample(s, p), p, dt);       // 0.5 s en neutro
        in.flick = 1.0f;
        step(s, in, sample(s, p), p, dt);
        CHECK(s.onGround, "no hace ollie si pasa mas de flickWindow");
    }

    // ---- 6) rampa: sale volando por el borde ----
    std::printf("[6] Salida de rampa a ~6 m/s\n");
    {
        State s; spawn(s, {0, 0, 15}, 0, 6.5f);
        Input in; in.push = true;
        float maxY = 0; bool wasAir = false, landed = false, bail = false;
        for (int i = 0; i < 400; ++i) {
            step(s, in, sample(s, p), p, dt);
            maxY = std::max(maxY, s.pos.y);
            bail = bail || s.bailed;
            if (!s.onGround) wasAir = true;
            if (wasAir && s.onGround) { landed = true; break; }
        }
        std::printf("    altura maxima = %.2f m, z final = %.2f m\n", maxY, s.pos.z);
        CHECK(wasAir, "despega al llegar al borde de la rampa");
        CHECK(maxY > 1.0f, "pasa la altura de la rampa (1 m)");
        CHECK(landed && !bail, "aterriza sin bail");
    }

    // ---- 7) caida desde mucha altura => bail ----
    std::printf("[7] Aterrizaje desde 6 m\n");
    {
        State s; spawn(s, {0, 6.0f, 0}, 0, 3.0f);
        s.onGround = false;
        Input in; bool bail = false;
        for (int i = 0; i < 200 && !bail; ++i) {
            step(s, in, sample(s, p), p, dt);
            bail = s.bailed;
        }
        CHECK(bail, "una caida fuerte provoca bail y frena la tabla");
    }

    std::printf("\n%s (%d fallos)\n", g_fail ? "HAY FALLOS" : "TODO OK", g_fail);
    return g_fail ? 1 : 0;
}
