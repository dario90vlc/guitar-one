// Renderizador de REFERENCIA para verificar nam.h: carga un .nam con el core
// OFICIAL de NeuralAmpModelerCore y procesa un fichero de floats crudos.
// Uso: ref.exe <modelo.nam> <entrada.f32> <salida.f32>
// La entrada son floats mono little-endian; la salida, un plano por canal.
// Se compila con /DNAM_SAMPLE_FLOAT para que el tipo coincida con nam.h (float32).
#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>

#include "get_dsp.h"
#include "dsp.h"

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("uso: ref <modelo.nam> <in.f32> <out.f32>\n"); return 2; }

    std::unique_ptr<nam::DSP> dsp;
    try
    {
        dsp = nam::get_dsp(std::filesystem::path(argv[1]));
    }
    catch (const std::exception& e)
    {
        std::printf("REF_FAIL carga: %s\n", e.what());
        return 1;
    }
    if (!dsp) { std::printf("REF_FAIL sin modelo\n"); return 1; }

    const double fs = dsp->GetExpectedSampleRate();
    const int outCh = dsp->NumOutputChannels();

    FILE* f = nullptr;
    if (fopen_s(&f, argv[2], "rb") != 0 || !f) { std::printf("REF_FAIL no abre la entrada\n"); return 1; }
    std::vector<float> in;
    float v = 0.0f;
    while (fread(&v, sizeof v, 1, f) == 1) in.push_back(v);
    fclose(f);
    const int N = (int)in.size();
    if (N <= 0) { std::printf("REF_FAIL entrada vacia\n"); return 1; }

    std::vector<std::vector<NAM_SAMPLE>> ob((size_t)outCh, std::vector<NAM_SAMPLE>((size_t)N, (NAM_SAMPLE)0));
    const int block = 64;                       // bloques pequenyos: mismo camino que por muestras
    dsp->Reset(fs, block);
    std::vector<NAM_SAMPLE*> ip((size_t)1), op((size_t)outCh, nullptr);
    std::vector<NAM_SAMPLE> ib((size_t)block, (NAM_SAMPLE)0);
    ip[0] = ib.data();
    for (int i = 0; i < N; i += block)
    {
        const int n = (i + block <= N) ? block : (N - i);
        for (int k = 0; k < n; ++k) ib[(size_t)k] = (NAM_SAMPLE)in[(size_t)(i + k)];
        for (int c = 0; c < outCh; ++c) op[(size_t)c] = ob[(size_t)c].data() + i;
        dsp->process(ip.data(), op.data(), n);
    }

    FILE* g = nullptr;
    if (fopen_s(&g, argv[3], "wb") != 0 || !g) { std::printf("REF_FAIL no escribe la salida\n"); return 1; }
    for (int c = 0; c < outCh; ++c)
        std::fwrite(ob[(size_t)c].data(), sizeof(NAM_SAMPLE), (size_t)N, g);
    fclose(g);

    std::printf("REF_OK fs=%.0f canales=%d muestras=%d\n", fs, outCh, N);
    return 0;
}
