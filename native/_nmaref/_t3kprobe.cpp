// _t3kprobe.cpp — Sonda del flujo de conexion de TONE3000 contra el servidor REAL.
// Ejecuta exactamente lo que hace la app (deviceStart + bucle de devicePoll) y
// dice el veredicto de cada sondeo. Sirve para comprobar que el flujo SOBREVIVE al
// estado previo al login, que era donde moria.
//   cl /O2 /EHsc /std:c++17 _t3kprobe.cpp /Fe:_t3kprobe.exe /link winhttp.lib crypt32.lib
#include <cstdio>
#include <string>
#include "../t3k.h"

int main(int argc, char** argv)
{
    const std::string key = (argc > 1) ? argv[1] : "t3k_pub_wOZz7T1_CWFGt9INgGRv6vesTHIepQNn";
    t3k::Device d;
    std::string err;
    if (!t3k::deviceStart(key, d, err)) { std::printf("FALLA deviceStart: %s\n", err.c_str()); return 2; }
    std::printf("user_code=%s\nverification_uri_complete=%s\nexpires_in=%d intervalo=%d\n\n",
                d.userCode.c_str(), d.verifyUriComplete.c_str(), d.expiresIn, d.interval);
    std::fflush(stdout);
    if (argc > 2 && std::string(argv[2]) == "only")      // solo crear y decir el codigo
    {
        std::printf("DEVICE_CODE=%s\n", d.deviceCode.c_str());
        return 0;
    }

    const int step = (d.interval > 0 ? d.interval : 5);
    const DWORD t0 = GetTickCount();
    int pend = 0, granted = 0, fatal = 0;
    while (GetTickCount() - t0 < 25000)                       // 25 s de sonda
    {
        Sleep((DWORD)step*1000);
        t3k::Resp r;
        const int st = t3k::devicePoll(key, d.deviceCode, r, err);
        if (st == 0)      { ++pend;    std::printf("  sondeo -> PENDIENTE\n"); }
        else if (st == 1) { ++granted; std::printf("  sondeo -> TOKEN CONCEDIDO (%d bytes)\n", (int)r.body.size()); }
        else              { ++fatal;   std::printf("  sondeo -> ERROR FATAL: %s\n", err.c_str()); }
        std::fflush(stdout);
        if (st != 0) break;
    }
    std::printf("\nRESUMEN: pendientes=%d concedidos=%d fatales=%d\n", pend, granted, fatal);
    // Fatales = el flujo se habria cortado antes de que el usuario pueda escribir
    // el codigo. Pendientes = el flujo sigue vivo, que es lo correcto.
    return fatal ? 1 : 0;
}
