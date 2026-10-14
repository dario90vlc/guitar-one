#pragma once
// ===========================================================================
//  nam.h - Perfiles NAM (.nam): lectura y ejecucion del modelo
// ---------------------------------------------------------------------------
//  Port del camino estandar del core oficial (NeuralAmpModelerCore, MIT,
//  (c) Steven Atkinson) para los tres modelos que se usan de verdad:
//    - WaveNet: arrays de capas dilatadas + mezcla de entrada + 1x1 + cabeza
//    - LSTM: celdas ifgo en cascada + cabeza
//    - ConvNet: convoluciones dilatadas (+ batchnorm) + cabeza
//
//  Dos garantias de seguridad, porque aqui no se adivina:
//    1. Se calcula el numero de pesos que el modelo DEBE tener segun su
//       config y se compara con el fichero. Si no cuadra, se rechaza.
//    2. Lo que el formato soporta pero este port no (FiLM/A2, gating,
//       slimmable, grupos, PReLU) se rechaza con mensaje claro.
//
//  Verificado contra el core compilado: wavenet_a1_standard.nam y lstm.nam
//  coinciden muestra a muestra (ver _namtest.cpp).
// ===========================================================================

#include <string>
#include <memory>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nam {

// ---------------------------------------------------------------------------
//  Lector JSON minimo: lo justo para .nam (objetos, arrays, cadenas, numeros)
// ---------------------------------------------------------------------------
namespace js {

inline size_t skipWs(const std::string& s, size_t p)
{
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
    return p;
}

// Fin (exclusivo) del valor que empieza en p.
inline size_t valueEnd(const std::string& s, size_t p)
{
    p = skipWs(s, p);
    if (p >= s.size()) return p;
    const char c = s[p];
    if (c == '"')
    {
        p++;
        while (p < s.size())
        {
            if (s[p] == '\\') p += 2;
            else if (s[p] == '"') { p++; break; }
            else p++;
        }
        return p;
    }
    if (c == '{' || c == '[')
    {
        const char close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (p < s.size())
        {
            const char d = s[p];
            if (d == '"') { p = valueEnd(s, p); continue; }
            if (d == c) depth++;
            else if (d == close) { depth--; p++; if (depth == 0) return p; continue; }
            p++;
        }
        return p;
    }
    while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']'
           && s[p] != ' ' && s[p] != '\n' && s[p] != '\r' && s[p] != '\t') p++;
    return p;
}

// Posicion del valor de "clave" dentro de [b, e). false si no esta.
inline bool find(const std::string& s, size_t b, size_t e, const char* key, size_t& valPos)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = b;
    while (true)
    {
        p = s.find(pat, p);
        if (p == std::string::npos || p >= e) return false;
        const size_t q = skipWs(s, p + pat.size());
        if (q < e && s[q] == ':') { valPos = skipWs(s, q + 1); return true; }
        p += pat.size();
    }
}

inline std::string str(const std::string& s, size_t b, size_t e, const char* key, const char* def = "")
{
    size_t p;
    if (!find(s, b, e, key, p) || p >= s.size() || s[p] != '"') return def;
    std::string out;
    for (size_t i = p + 1; i < s.size() && s[i] != '"'; i++)
    {
        if (s[i] == '\\' && i + 1 < s.size()) i++;
        out += s[i];
    }
    return out;
}

inline double num(const std::string& s, size_t b, size_t e, const char* key, double def = 0.0)
{
    size_t p;
    if (!find(s, b, e, key, p)) return def;
    const size_t q = valueEnd(s, p);
    return std::atof(s.substr(p, q - p).c_str());
}

inline bool boolean(const std::string& s, size_t b, size_t e, const char* key, bool def = false)
{
    size_t p;
    if (!find(s, b, e, key, p)) return def;
    if (s.compare(p, 4, "true") == 0) return true;
    if (s.compare(p, 5, "false") == 0) return false;
    return def;
}

inline int integer(const std::string& s, size_t b, size_t e, const char* key, int def = 0)
{
    return (int)num(s, b, e, key, (double)def);
}

// Array de enteros (p.ej. dilations)
inline std::vector<int> ints(const std::string& s, size_t b, size_t e, const char* key)
{
    std::vector<int> out;
    size_t p;
    if (!find(s, b, e, key, p) || p >= s.size() || s[p] != '[') return out;
    const size_t stop = valueEnd(s, p);
    for (size_t i = p + 1; i < stop; i++)
    {
        while (i < stop && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
        if (i >= stop || s[i] == ']') break;
        out.push_back(std::atoi(s.c_str() + i));
        while (i < stop && s[i] != ',' && s[i] != ']') i++;
    }
    return out;
}

// Array (posiblemente anidado) de floats: el caso de "weights".
inline bool floats(const std::string& s, size_t b, size_t e, const char* key, std::vector<float>& out)
{
    size_t p;
    if (!find(s, b, e, key, p) || p >= s.size() || s[p] != '[') return false;
    const size_t stop = valueEnd(s, p);
    out.reserve((stop - p) / 8 + 1);
    for (size_t i = p + 1; i < stop; i++)
    {
        const char c = s[i];
        if (c == '-' || c == '+' || (c >= '0' && c <= '9') || c == '.')
        {
            char* endp = nullptr;
            const double v = std::strtod(s.c_str() + i, &endp);
            out.push_back((float)v);
            i = (size_t)(endp - s.c_str()) - 1;
        }
        else if (c == ']' && i + 1 < stop && s[i + 1] == ']') { /* cierre anidado */ }
    }
    return true;
}

// Objetos de primer nivel dentro de un array (p.ej. cada array de capas)
inline std::vector<std::pair<size_t, size_t>> objects(const std::string& s, size_t arrPos)
{
    std::vector<std::pair<size_t, size_t>> out;
    if (arrPos >= s.size() || s[arrPos] != '[') return out;
    const size_t stop = valueEnd(s, arrPos);
    size_t p = arrPos + 1;
    while (p < stop)
    {
        p = skipWs(s, p);
        if (p >= stop || s[p] == ']') break;
        const size_t vEnd = valueEnd(s, p);
        if (s[p] == '{') out.push_back({ p, vEnd });
        p = vEnd;
        while (p < stop && (s[p] == ',' || s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++;
    }
    return out;
}

} // namespace js

// ---------------------------------------------------------------------------
//  Activaciones
// ---------------------------------------------------------------------------
enum ActKind { ACT_TANH, ACT_LEAKY, ACT_RELU, ACT_SIGMOID, ACT_HARDTANH,
               ACT_ELU, ACT_SILU, ACT_LEAKYHARDTANH, ACT_SOFTSIGN, ACT_IDENTITY, ACT_UNKNOWN };

struct Act
{
    int   kind = ACT_TANH;
    float slope = 0.01f;
    float minv = -1.0f, maxv = 1.0f;
    float mins = 0.01f, maxs = 0.01f;
};

inline float actApply(const Act& a, float x)
{
    switch (a.kind)
    {
        case ACT_TANH:   return std::tanh(x);
        case ACT_LEAKY:  return x > 0.0f ? x : a.slope * x;
        case ACT_RELU:   return x > 0.0f ? x : 0.0f;
        case ACT_SIGMOID:return 1.0f / (1.0f + std::exp(-x));
        case ACT_ELU:    return x > 0.0f ? x : a.slope * (std::exp(x) - 1.0f);
        case ACT_SILU:   return x / (1.0f + std::exp(-x));
        case ACT_SOFTSIGN: return x / (1.0f + std::fabs(x));
        case ACT_HARDTANH:
            if (x < a.minv) return a.minv;
            if (x > a.maxv) return a.maxv;
            return x;
        case ACT_LEAKYHARDTANH:
            if (x < a.minv) return (x - a.minv) * a.mins + a.minv;
            if (x > a.maxv) return (x - a.maxv) * a.maxs + a.maxv;
            return x;
        default:         return x;
    }
}

// Nombre de una activacion del fichero -> Act
inline Act actFromJson(const std::string& s, size_t b, size_t e, const char* key)
{
    Act a;
    std::string name = js::str(s, b, e, key, "Tanh");
    // Puede venir como objeto: {"type":"LeakyReLU","negative_slope":0.01}
    size_t p;
    if (js::find(s, b, e, key, p) && p < s.size() && s[p] == '{')
    {
        const size_t stop = js::valueEnd(s, p);
        name = js::str(s, p, stop, "type", name.c_str());
        a.slope = (float)js::num(s, p, stop, "negative_slope", 0.01);
        a.minv  = (float)js::num(s, p, stop, "min_val", -1.0);
        a.maxv  = (float)js::num(s, p, stop, "max_val", 1.0);
    }
    if (name == "Tanh") a.kind = ACT_TANH;
    else if (name == "LeakyReLU") a.kind = ACT_LEAKY;
    else if (name == "ReLU") a.kind = ACT_RELU;
    else if (name == "Sigmoid") a.kind = ACT_SIGMOID;
    else if (name == "Hardtanh") a.kind = ACT_HARDTANH;
    else if (name == "ELU") a.kind = ACT_ELU;
    else if (name == "SiLU" || name == "Swish") a.kind = ACT_SILU;
    else if (name == "LeakyHardtanh") a.kind = ACT_LEAKYHARDTANH;
    else if (name == "Softsign") a.kind = ACT_SOFTSIGN;
    else if (name == "Identity" || name == "Linear") a.kind = ACT_IDENTITY;
    else a.kind = ACT_UNKNOWN;
    return a;
}

// --- FiLM: modulacion por canal desde la condicion -------------------------
//  escala, desplazamiento = Conv1x1(condicion) partido por canales; salida =
//  entrada*escala + desplazamiento. El 1x1 lleva sesgo y admite grupos.
struct Film
{
    bool active = false, shift = true;
    int  groups = 1, condDim = 1, inDim = 1;
    size_t oW = 0;
    std::vector<float> ss, out;
    inline int outDim() const { return (shift ? 2 : 1) * inDim; }
};

// aplica el FiLM: ss = W*cond + b ; dst = in*ss (o in*ss + ss[n..))
inline void filmApply(const Film& f, const float* w, const float* in, const float* cond, float* ss, float* dst)
{
    const int n = f.inDim;
    const int outN = f.outDim();
    const int inPer = f.condDim / f.groups;
    const int outPer = outN / f.groups;
    const float* bias = w + (size_t)outN * f.condDim;
    for (int o = 0; o < outN; o++)
    {
        const int g = o / outPer;
        const float* wi = w + ((size_t)g * outPer + (o % outPer)) * inPer;
        const float* cd = cond + (size_t)g * inPer;
        float acc = bias[o];
        for (int c = 0; c < inPer; c++) acc += wi[c] * cd[c];
        ss[o] = acc;
    }
    if (f.shift) for (int i = 0; i < n; i++) dst[i] = in[i] * ss[i] + ss[n + i];
    else         for (int i = 0; i < n; i++) dst[i] = in[i] * ss[i];
}

// ---------------------------------------------------------------------------
//  Modelo
// ---------------------------------------------------------------------------
// Techo de las dimensiones de un .nam. Es muy holgado para cualquier captura
// real --las de TONE3000 llegan a dilataciones de 512 y campos receptores de
// ~1e3 muestras-- pero evita que un fichero manipulado pida gigas de memoria
// al reservar los anillos, o millones de muestras de precalentamiento al
// cargar. El parseo rechaza, nunca recorta.
static const int kDimMax = 1 << 20;

class Model
{
public:
    // --- metadatos (para la UI) ---
    std::string arch, name, author, gearMake, gearModel, gearType, tone, license;
    bool a2 = false;      // tiene clave condition_dsp: la ruta A2 (es la que muestra tone3000)
    std::string err;
    double sampleRate = 48000.0;
    bool   hasLevels = false;
    float  inDbu = 0.0f, outDbu = 0.0f, loudness = 0.0f, gain = 0.0f;
    int    prewarmSamples = 1;
    int    inChannels = 1, outChannels = 1;
    bool   ok = false;

    bool load(const std::string& path);
    bool loadText(const std::string& text) { if (!_parse(text)) return false; ok = true; return true; }
    void reset();
    inline float tick(float x);
    // Salida multicanal de la ultima muestra (la usa la condicion A2 y, en el
    // harness, la verificacion contra el core oficial canal a canal).
    const std::vector<float>& outBuffer() const { return _outBuf; }

private:
    enum Arch { A_NONE = -1, A_WAVENET = 0, A_LSTM = 1, A_CONVNET = 2 };
    int  _arch = A_NONE;
    std::vector<float> _w;                  // todos los pesos del fichero

    // ---- WaveNet ----
    struct Ring
    {
        std::vector<float> b;
        int len = 1, pos = 0, ch = 1;
        void init(int channels, int length)
        {
            ch = channels; len = length > 0 ? length : 1;
            b.assign((size_t)ch * len, 0.0f); pos = 0;
        }
        // Muestras contiguas por canal: el lazo interno lee un vector seguido y
        // el compilador lo vectoriza (antes iba a saltos de `len`).
        inline void push(const float* v)
        {
            pos++; if (pos >= len) pos = 0;
            float* d = &b[(size_t)pos * ch];
            for (int c = 0; c < ch; c++) d[c] = v[c];
        }
        inline const float* at(int delay) const
        {
            int i = pos - delay;
            if (i < 0) i += len;
            // El parseo ya rechaza toda dimension y toda dilatacion <= 0, asi que
            // delay cae siempre en [0, len-1] y esta rama no llega a tomarse.
            // Se deja como cinturon: si algun dia cambia el calculo del anillo y
            // alguien se olvida de actualizar la validacion, esto degrada a una
            // lectura recortada en lugar de salirse del vector en el hilo de audio.
            else if (i >= len) i -= len;
            return &b[(size_t)i * ch];
        }
    };
    struct WnLayer
    {
        int  d = 1, k = 1, cin = 1, cout = 1, mixIn = 1;
        int  bnl = 1, groupsIn = 1, groupsMix = 1, l1groups = 1;
        bool has1x1 = false, hasH1 = false;
        int  h1out = 1, h1groups = 1;
        Act  act;
        size_t oConv = 0, oMix = 0, oL1 = 0, oH1 = 0;
        Ring ring;
        std::vector<float> y, z;
        std::vector<float> wp;              // conv reempaquetada (k, g, out, in)
        std::vector<float> wmp;             // mixin reempaquetado (g, out, in)
        std::vector<float> whp;             // head1x1 reempaquetado (g, out, in)
        // FiLM por hueco (A2)
        Film fConvPre, fConvPost, fMixPre, fMixPost, fActPre, fActPost, fL1Post, fH1Post;
        std::vector<float> ss, tmpA, tmpB, tmpC, headOut;
    };
    struct WnArray
    {
        int  inSize = 1, condSize = 1, headSize = 1, ch = 1, bnl = 1, headK = 1, headD = 1;
        bool headBias = false;
        size_t oRe = 0, oHeadRe = 0;
        std::vector<WnLayer> layers;
        Ring headRing;
        std::vector<float> xin, re, prev, headAcc, headRe;
        std::vector<float> whp;             // head rechannel (k, out, in)
        int  headOutSize = 1;               // head1x1.out si activo, si no bottleneck
    };
    std::vector<WnArray> _arrays;
    std::unique_ptr<Model> _condDsp;    // A2: submodelo que produce la condicion
    std::vector<float> _condBuf;        // su salida (condition_size canales)
    bool  _hasHead = false;
    int   _headOut = 1, _headCh = 1;
    std::vector<int> _headKs;
    Act   _headAct;
    std::vector<Ring> _headRings;
    std::vector<std::vector<float>> _headBuf;
    size_t _oHeadW = 0;
    float _headScale = 1.0f;
    // condition_dsp (A2): un modelo anidado que produce la condicion
    std::vector<float> _condScratch;

    // ---- LSTM ----
    struct LstmCell
    {
        int in = 1, h = 1;
        size_t oW = 0, oB = 0;
        std::vector<float> xh, c, hh;
    };
    std::vector<LstmCell> _cells;
    size_t _oLstmHead = 0;
    std::vector<float> _lstmOut;

    // ---- ConvNet ----
    struct CvBlock
    {
        int cin = 1, cout = 1, k = 1, d = 1;
        bool bn = false;
        Act  act;
        size_t oW = 0, oB = 0, oBn = 0;
        Ring ring;
    };
    std::vector<CvBlock> _blocks;
    size_t _oCvHead = 0;
    std::vector<float> _cvBuf;
    // buffers de trabajo (preasignados: en el hilo de audio no se reserva nada)
    std::vector<float> _gates, _headCur, _headDst, _cvCur, _cvOut;
    std::vector<float> _outBuf { 0.0f };   // salida multicanal (para la condicion A2)

    bool _parse(const std::string& s);
    bool _parseWaveNet(const std::string& s);
    bool _parseLstm(const std::string& s);
    bool _parseConvNet(const std::string& s);
    inline float _waveNetTick(float x);
    inline float _lstmTick(float x);
    inline float _convNetTick(float x);
};

// Producto punto con 4 acumuladores: sin esto el compilador no puede
// vectorizar (la suma tiene dependencia serial) y el lazo se queda escalar.
inline float dot4(const float* a, const float* b, int n)
{
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        s0 += a[i]*b[i]; s1 += a[i+1]*b[i+1];
        s2 += a[i+2]*b[i+2]; s3 += a[i+3]*b[i+3];
    }
    for (; i < n; i++) s0 += a[i]*b[i];
    return (s0 + s1) + (s2 + s3);
}


// --- convolucion 1x1: y[i] = sum_c w[i*cin + c] * x[c] -----------------------
inline void conv1x1(const float* w, int cin, int cout, const float* x, float* y)
{
    for (int i = 0; i < cout; i++)
    {
        y[i] = dot4(w + (size_t)i * cin, x, cin);
    }
}

// ---------------------------------------------------------------------------
//  Carga
// ---------------------------------------------------------------------------
inline bool Model::load(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "no se puede abrir el fichero"; return false; }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (long)(512 * 1024 * 1024)) { std::fclose(f); err = "tamano de fichero invalido"; return false; }
    std::string s;
    s.resize((size_t)sz);
    const size_t got = std::fread(&s[0], 1, (size_t)sz, f);
    std::fclose(f);
    s.resize(got);

    if (!_parse(s)) return false;

    ok = true;
    return true;
}

inline bool Model::_parse(const std::string& s)
{
    const size_t B = 0, E = s.size();
    arch = js::str(s, B, E, "architecture", "");
    if (arch.empty()) { err = "sin campo architecture"; return false; }
    {   // A2: la ruta condicionada trae la clave "condition_dsp"
        size_t cf = 0;
        a2 = js::find(s, B, E, "condition_dsp", cf);
    }

    // metadatos
    size_t mp;
    if (js::find(s, B, E, "metadata", mp) && mp < s.size() && s[mp] == '{')
    {
        const size_t me = js::valueEnd(s, mp);
        name       = js::str(s, mp, me, "name");
        author     = js::str(s, mp, me, "modeled_by");
        gearMake   = js::str(s, mp, me, "gear_make");
        gearModel  = js::str(s, mp, me, "gear_model");
        gearType   = js::str(s, mp, me, "gear_type");
        tone       = js::str(s, mp, me, "tone_type");
        license    = js::str(s, mp, me, "license");
        const double idb = js::num(s, mp, me, "input_level_dbu", 1e9);
        const double odb = js::num(s, mp, me, "output_level_dbu", 1e9);
        loudness = (float)js::num(s, mp, me, "loudness", 0.0);
        gain     = (float)js::num(s, mp, me, "gain", 0.0);
        if (idb < 1e8 && odb < 1e8) { hasLevels = true; inDbu = (float)idb; outDbu = (float)odb; }
    }

    // La arquitectura se comprueba ANTES de los pesos: un contenedor
    // (SlimmableContainer, Sequential) no trae pesos arriba (viven en los
    // submodelos), y decir "weights vacio" hacia pensar en un fichero roto
    // cuando lo que pasa es que esa arquitectura no esta soportada.
    if (arch != "WaveNet" && arch != "LSTM" && arch != "ConvNet")
    {
        if (arch == "SlimmableContainer" || arch == "Sequential")
            err = "arquitectura " + arch + " (contenedor de submodelos) no soportada";
        else
            err = "arquitectura no soportada: " + arch;
        return false;
    }

    // Pesos: se buscan DESPUES del config, porque un A2 lleva dentro el
    // submodelo de condicion con SUS pesos y si no se leerian los suyos.
    size_t cfgB = 0, cfgE = 0;
    if (js::find(s, B, E, "config", cfgB) && cfgB < s.size() && s[cfgB] == '{')
        cfgE = js::valueEnd(s, cfgB);
    if (!js::floats(s, cfgE ? cfgE : B, E, "weights", _w))
    { err = "sin campo weights"; return false; }
    if (_w.empty()) { err = "weights vacio"; return false; }

    // config: cada arquitectura tiene su parser (y su verificacion de conteo)
    if (arch == "WaveNet")   { _arch = A_WAVENET; if (!_parseWaveNet(s)) return false; }
    else if (arch == "LSTM") { _arch = A_LSTM; if (!_parseLstm(s)) return false; }
    else                     { _arch = A_CONVNET; if (!_parseConvNet(s)) return false; }

    return true;
}

// --- WaveNet ---------------------------------------------------------------
inline bool Model::_parseWaveNet(const std::string& s)
{
    const size_t B = 0, E = s.size();
    size_t cp;
    if (!js::find(s, B, E, "config", cp) || cp >= s.size() || s[cp] != '{')
    { err = "WaveNet sin config"; return false; }
    const size_t ce = js::valueEnd(s, cp);

    // El submodelo de condicion (A2) va ANTES de "layers" en el JSON: hay que
    // excluir su rango o se leen SUS arrays como si fueran los del modelo.
    size_t cdB = 0, cdE = 0;
    {
        size_t dp;
        if (js::find(s, cp, ce, "condition_dsp", dp) && dp < s.size() && s[dp] == '{')
        { cdB = dp; cdE = js::valueEnd(s, dp); }
    }
    auto findCfg = [&](const char* key, size_t& pos) -> bool
    {
        size_t p = cp;
        while (js::find(s, p, ce, key, pos))
        {
            if (cdE == 0 || pos < cdB || pos >= cdE) return true;
            p = cdE;
        }
        return false;
    };
    size_t lp;
    if (!findCfg("layers", lp) || lp >= s.size() || s[lp] != '[')
    { err = "WaveNet sin layers"; return false; }
    const auto arrs = js::objects(s, lp);
    if (arrs.empty()) { err = "WaveNet sin arrays de capas"; return false; }

    size_t consumed = 0;
    auto expect = [&](size_t n, const char* what) -> bool
    {
        if (consumed + n > _w.size())
        { char b2[160]; std::snprintf(b2, sizeof b2, "pesos insuficientes en %s: pide %d y quedan %d",
                                      what, (int)n, (int)(_w.size() - consumed)); err = b2; return false; }
        return true;
    };
    // FiLM de un hueco: {"active":bool,"shift":bool,"groups":int}
    auto parseFilm = [&](size_t b, size_t e, const char* key, int condDim, int inDim, Film& f) -> bool
    {
        size_t p;
        if (!js::find(s, b, e, key, p) || p >= s.size() || s[p] != '{') return true;
        const size_t pe = js::valueEnd(s, p);
        if (!js::boolean(s, p, pe, "active", false)) return true;
        f.active  = true;
        f.shift   = js::boolean(s, p, pe, "shift", true);
        f.groups  = js::integer(s, p, pe, "groups", 1);
        f.condDim = condDim; f.inDim = inDim;
        if (f.groups < 1 || condDim % f.groups || f.outDim() % f.groups)
        { char b2[160]; std::snprintf(b2, sizeof b2, "FiLM %s: grupos %d con cond %d y salida %d",
                                      key, f.groups, condDim, f.outDim()); err = b2; return false; }
        f.oW = consumed;
        const size_t n = (size_t)f.outDim() * condDim / f.groups + f.outDim();
        if (!expect(n, "FiLM")) return false;
        consumed += n;
        f.ss.assign(f.outDim(), 0.0f);
        return true;
    };

    _arrays.clear();
    for (size_t ai = 0; ai < arrs.size(); ai++)
    {
        const size_t b = arrs[ai].first, e = arrs[ai].second;
        WnArray a;
        a.inSize   = js::integer(s, b, e, "input_size", 1);
        a.condSize = js::integer(s, b, e, "condition_size", 1);
        a.ch       = js::integer(s, b, e, "channels", 1);
        a.bnl      = js::integer(s, b, e, "bottleneck", a.ch);
        const int k = js::integer(s, b, e, "kernel_size", 3);
        const std::string gm = js::str(s, b, e, "gating_mode", "none");
        if (gm != "none" && !gm.empty()) { err = "WaveNet con gating: no soportado"; return false; }
        if (js::boolean(s, b, e, "gated", false)) { err = "WaveNet con gating: no soportado"; return false; }

        // layer1x1 y head1x1
        bool l1active = true, h1active = false;
        int  l1groups = 1, h1groups = 1, h1out = 1;
        {
            size_t p;
            if (js::find(s, b, e, "layer1x1", p))
            {
                if (s[p] == '{') { const size_t pe = js::valueEnd(s, p);
                                   l1active = js::boolean(s, p, pe, "active", true);
                                   l1groups = js::integer(s, p, pe, "groups", 1); }
                else l1active = js::boolean(s, b, e, "layer1x1", true);
            }
            if (js::find(s, b, e, "head1x1", p) && s[p] == '{')
            {
                const size_t pe = js::valueEnd(s, p);
                h1active = js::boolean(s, p, pe, "active", false);
                h1out    = js::integer(s, p, pe, "out_channels", a.bnl);
                h1groups = js::integer(s, p, pe, "groups", 1);
            }
        }
        if (!l1active && a.bnl != a.ch) { err = "WaveNet: sin layer1x1 el bottleneck debe igualar channels"; return false; }
        a.headOutSize = h1active ? h1out : a.bnl;

        // cabeza del array: objeto nuevo (head) o campos antiguos
        size_t hp;
        if (js::find(s, b, e, "head", hp) && hp < s.size() && s[hp] == '{')
        {
            const size_t he = js::valueEnd(s, hp);
            a.headSize = js::integer(s, hp, he, "out_channels", 1);
            a.headK    = js::integer(s, hp, he, "kernel_size", 1);
            a.headD    = js::integer(s, hp, he, "head_dilation", 1);
            a.headBias = js::boolean(s, hp, he, "bias", false);
        }
        else
        {
            a.headSize = js::integer(s, b, e, "head_size", 1);
            a.headK = 1; a.headD = 1;
            a.headBias = js::boolean(s, b, e, "head_bias", false);
        }

        // Toda dimension del config acaba indexando un buffer. Si el fichero trae
        // un valor <= 0 --un .nam manipulado, no uno del core-- el motor leeria
        // fuera del heap en el hilo de audio. Se rechaza con mensaje, igual que
        // el gating: lo que no se sabe evaluar, se dice; nunca se recorta callado.
        if (a.inSize < 1 || a.condSize < 1 || a.ch < 1 || a.bnl < 1 || k < 1)
        { err = "WaveNet con dimension <= 0"; return false; }
        if (a.inSize > kDimMax || a.condSize > kDimMax || a.ch > kDimMax ||
            a.bnl > kDimMax || k > kDimMax)
        { err = "WaveNet con dimension fuera de rango"; return false; }
        if (a.headSize < 1 || a.headK < 1 || a.headD < 1)
        { err = "WaveNet con cabeza invalida"; return false; }
        if (a.headSize > kDimMax || a.headK > kDimMax || a.headD > kDimMax)
        { err = "WaveNet con cabeza fuera de rango"; return false; }
        if (h1active && (h1out < 1 || h1groups < 1))
        { err = "WaveNet con head1x1 invalido"; return false; }

        // rechannel: input_size -> channels, sin bias
        a.oRe = consumed;
        if (!expect((size_t)a.inSize * a.ch, "rechannel")) return false;
        consumed += (size_t)a.inSize * a.ch;

        const std::vector<int> dil = js::ints(s, b, e, "dilations");
        if (dil.empty()) { err = "WaveNet sin dilations"; return false; }
        // La dilatacion fija el retardo de cada capa: con d <= 0 el indice del
        // anillo se sale por arriba y el lazo lee fuera del vector. El rango
        // util es [0, d*(k-1)] con d >= 1, asi que ahi se queda validado.
        for (size_t li = 0; li < dil.size(); li++)
        {
            if (dil[li] < 1)     { err = "WaveNet con dilatacion <= 0"; return false; }
            if (dil[li] > kDimMax) { err = "WaveNet con dilatacion fuera de rango"; return false; }
        }

        for (size_t li = 0; li < dil.size(); li++)
        {
            WnLayer L;
            L.d = dil[li]; L.k = k; L.cin = a.ch; L.cout = a.bnl; L.mixIn = a.condSize;
            L.bnl = a.bnl;
            L.groupsIn  = js::integer(s, b, e, "groups_input", 1);
            L.groupsMix = js::integer(s, b, e, "groups_input_mixin", 1);
            L.act = actFromJson(s, b, e, "activation");
            if (L.act.kind == ACT_UNKNOWN) { err = "activacion no soportada en WaveNet"; return false; }
            if (L.groupsIn < 1 || L.cout % L.groupsIn || L.cin % L.groupsIn)
            { err = "grupos de entrada invalidos"; return false; }
            if (L.groupsMix < 1 || L.cout % L.groupsMix || L.mixIn % L.groupsMix)
            { err = "grupos del mixin invalidos"; return false; }

            // conv dilatada agrupada
            L.oConv = consumed;
            if (!expect((size_t)L.k * L.cout * L.cin / L.groupsIn + L.cout, "convolucion")) return false;
            {
                const int outPer = L.cout / L.groupsIn, inPer = L.cin / L.groupsIn;
                L.wp.resize((size_t)L.k * L.cout * L.cin / L.groupsIn);
                for (int g = 0; g < L.groupsIn; g++)
                    for (int i = 0; i < outPer; i++)
                        for (int j = 0; j < inPer; j++)
                            for (int kk = 0; kk < L.k; kk++)
                                L.wp[(((size_t)kk*L.groupsIn + g)*outPer + i)*inPer + j] =
                                    _w[consumed + ((size_t)(g*outPer + i)*inPer + j)*L.k + kk];
            }
            consumed += (size_t)L.k * L.cout * L.cin / L.groupsIn + L.cout;

            // input mixin (1x1 agrupada, sin bias)
            L.oMix = consumed;
            if (!expect((size_t)L.mixIn * L.cout / L.groupsMix, "input mixin")) return false;
            consumed += (size_t)L.mixIn * L.cout / L.groupsMix;

            // layer1x1 (bottleneck -> channels, con bias)
            L.has1x1 = l1active; L.l1groups = l1groups;
            if (L.has1x1)
            {
                L.oL1 = consumed;
                const size_t n = (size_t)a.bnl * a.ch / l1groups + a.ch;
                if (!expect(n, "layer1x1")) return false;
                consumed += n;
                L.y.assign(a.ch, 0.0f);
            }
            else L.y.assign(a.ch, 0.0f);

            // head1x1 (bottleneck -> h1out, con bias)
            L.hasH1 = h1active;
            L.h1out = h1out; L.h1groups = h1groups;
            if (L.hasH1)
            {
                L.oH1 = consumed;
                const size_t n = (size_t)h1out * a.bnl / h1groups + h1out;
                if (!expect(n, "head1x1")) return false;
                consumed += n;
            }
            L.headOut.assign(L.hasH1 ? h1out : a.bnl, 0.0f);

            // FiLM (en el orden del core)
            if (!parseFilm(b, e, "conv_pre_film",         a.condSize, a.ch,          L.fConvPre)) return false;
            if (!parseFilm(b, e, "conv_post_film",        a.condSize, a.bnl,         L.fConvPost)) return false;
            if (!parseFilm(b, e, "input_mixin_pre_film",  a.condSize, a.condSize,    L.fMixPre)) return false;
            if (!parseFilm(b, e, "input_mixin_post_film", a.condSize, a.bnl,         L.fMixPost)) return false;
            if (!parseFilm(b, e, "activation_pre_film",   a.condSize, a.bnl,         L.fActPre)) return false;
            if (!parseFilm(b, e, "activation_post_film",  a.condSize, a.bnl,         L.fActPost)) return false;
            if (L.has1x1 && !parseFilm(b, e, "layer1x1_post_film", a.condSize, a.ch, L.fL1Post)) return false;
            if (L.hasH1  && !parseFilm(b, e, "head1x1_post_film",  a.condSize, h1out, L.fH1Post)) return false;

            const int ringLen = L.d * (L.k - 1) + 1;
            L.ring.init(L.cin, ringLen);
            L.z.assign(L.cout, 0.0f);
            L.tmpA.assign(a.ch, 0.0f);
            L.tmpB.assign(a.condSize > a.ch ? a.condSize : a.ch, 0.0f);
            L.tmpC.assign(a.bnl > a.ch ? a.bnl : a.ch, 0.0f);
            a.layers.push_back(std::move(L));
        }

        // head rechannel: (bottleneck o head1x1.out) -> head_size
        a.oHeadRe = consumed;
        {
            const int hin = a.headOutSize;
            const size_t n = (size_t)a.headK * a.headSize * hin + (a.headBias ? a.headSize : 0);
            if (!expect(n, "head rechannel")) return false;
            a.whp.resize((size_t)a.headK * a.headSize * hin);
            for (int i = 0; i < a.headSize; i++)
                for (int c = 0; c < hin; c++)
                    for (int kk = 0; kk < a.headK; kk++)
                        a.whp[((size_t)kk * a.headSize + i) * hin + c] = _w[consumed + ((size_t)i * hin + c) * a.headK + kk];
            consumed += n;
            a.headRing.init(hin, a.headD * (a.headK - 1) + 1);
        }
        a.xin.assign(a.inSize, 0.0f);
        a.re.assign(a.ch, 0.0f);
        a.prev.assign(a.ch, 0.0f);
        a.headAcc.assign(a.headOutSize, 0.0f);
        a.headRe.assign(a.headSize, 0.0f);
        _arrays.push_back(std::move(a));
    }

    // cabeza post-stack (opcional)
    size_t hp2;
    if (findCfg("head", hp2) && hp2 < s.size() && s[hp2] == '{')
    {
        const size_t he = js::valueEnd(s, hp2);
        _hasHead = true;
        _headOut = js::integer(s, hp2, he, "out_channels", 1);
        _headCh  = js::integer(s, hp2, he, "channels", 1);
        _headKs  = js::ints(s, hp2, he, "kernel_sizes");
        _headAct = actFromJson(s, hp2, he, "activation");
        if (_headKs.empty()) { err = "WaveNet: head sin kernel_sizes"; return false; }
        if (_headAct.kind == ACT_UNKNOWN) { err = "activacion de cabeza no soportada"; return false; }
        _oHeadW = consumed;
        int cin = _arrays.back().headSize;
        for (size_t i = 0; i < _headKs.size(); i++)
        {
            const int cout = (i + 1 == _headKs.size()) ? _headOut : _headCh;
            const size_t n = (size_t)_headKs[i] * cout * cin + cout;
            if (!expect(n, "cabeza")) return false;
            consumed += n;
            _headRings.push_back(Ring());
            _headRings.back().init(cin, _headKs[i]);
            _headBuf.push_back(std::vector<float>(cout, 0.0f));
            cin = cout;
        }
    }

    // A2: el submodelo que produce la condicion. Verificado muestra a muestra
    // contra el core OFICIAL con el modo nam2 del harness (el mismo metodo con el
    // que se verifico A1). El core comprueba que la entrada del submodelo cuadra
    // con la del WaveNet y que su salida cuadra con el condition_size de CADA
    // capa; si no cuadra, no se adivina: se rechaza.
    if (cdE != 0)
    {
        _condDsp.reset(new Model());
        if (!_condDsp->loadText(s.substr(cdB, cdE - cdB)))
        { err = "condition_dsp no soportado: " + _condDsp->err; return false; }
        if (_condDsp->inChannels != _arrays.front().inSize)
        {
            char b2[160]; std::snprintf(b2, sizeof b2,
                "condition_dsp espera %d canales de entrada y el WaveNet da %d",
                _condDsp->inChannels, _arrays.front().inSize);
            err = b2; return false;
        }
        for (size_t ai = 0; ai < _arrays.size(); ai++)
            if (_arrays[ai].condSize != _condDsp->outChannels)
            {
                char b2[160]; std::snprintf(b2, sizeof b2,
                    "condition_dsp da %d canales y la capa %d pide %d",
                    _condDsp->outChannels, (int)ai, _arrays[ai].condSize);
                err = b2; return false;
            }
        _condBuf.assign(_arrays.front().condSize, 0.0f);
    }
    else if (_arrays.front().condSize != 1)
    { err = "WaveNet con condition_size > 1 y sin condition_dsp"; return false; }

    // head_scale: el ULTIMO peso del fichero
    if (consumed + 1 != _w.size())
    {
        char msg[180];
        std::snprintf(msg, sizeof msg,
                      "WaveNet: el fichero tiene %d pesos y la config pide %d (arquitectura distinta o no soportada)",
                      (int)_w.size(), (int)consumed + 1);
        err = msg;
        return false;
    }
    _headScale = _w[consumed];
    consumed++;

    // precalentamiento: 1 (o el del dsp de condicion) + campo receptivo de cada array
    // Se acumula en long y se acota al final: con muchas capas y dilataciones
    // el campo receptivo no cabe en un int, y el cast desbordaria (UB) antes de
    // que se acotara. El precalentamiento corre en la carga, en el hilo que
    // llama a load(): sin tope, un fichero manipulado lo deja colgado.
    long acc = _condDsp ? _condDsp->prewarmSamples : 1;
    for (size_t i = 0; i < _arrays.size(); i++)
    {
        const WnArray& a = _arrays[i];
        long rf = 0;
        for (size_t j = 0; j < a.layers.size(); j++)
            rf += (long)a.layers[j].d * (a.layers[j].k - 1);
        rf += (long)a.headD * (a.headK - 1);
        acc += rf;
        if (i > 0 && a.ch != _arrays[i - 1].headSize)
        { err = "WaveNet: canales del array no coinciden con el head_size del anterior"; return false; }
    }
    if (_hasHead)
    {
        long rf = 0;
        for (size_t i = 0; i < _headKs.size(); i++) rf += _headKs[i] - 1;
        acc += rf;
    }
    if (acc < 1) prewarmSamples = 1;
    else prewarmSamples = (acc > kDimMax) ? kDimMax : (int)acc;
    inChannels = 1;
    outChannels = _hasHead ? _headOut : _arrays.back().headSize;
    return true;
}

// --- LSTM ------------------------------------------------------------------
inline bool Model::_parseLstm(const std::string& s)
{
    const size_t B = 0, E = s.size();
    size_t cp;
    if (!js::find(s, B, E, "config", cp) || cp >= s.size() || s[cp] != '{')
    { err = "LSTM sin config"; return false; }
    const size_t ce = js::valueEnd(s, cp);

    const int inputSize  = js::integer(s, cp, ce, "input_size", 1);
    const int hiddenSize = js::integer(s, cp, ce, "hidden_size", 1);
    const int numLayers  = js::integer(s, cp, ce, "num_layers", 1);
    if (hiddenSize <= 0 || numLayers <= 0 || numLayers > 16)
    { err = "LSTM con dimensiones invalidas"; return false; }
    if (inputSize < 1 || inputSize > kDimMax || hiddenSize > kDimMax)
    { err = "LSTM con dimensiones fuera de rango"; return false; }

    size_t consumed = 0;
    _cells.clear();
    for (int i = 0; i < numLayers; i++)
    {
        LstmCell c;
        c.in = (i == 0) ? inputSize : hiddenSize;
        c.h  = hiddenSize;
        const size_t n = (size_t)4 * c.h * (c.in + c.h) + 4 * c.h + 2 * c.h;
        if (consumed + n > _w.size()) { err = "LSTM: pesos insuficientes"; return false; }
        c.oW = consumed;
        c.oB = consumed + (size_t)4 * c.h * (c.in + c.h);
        consumed += n;
        c.xh.assign(c.in + c.h, 0.0f);
        c.c.assign(c.h, 0.0f);
        c.hh.assign(c.h, 0.0f);
        _cells.push_back(std::move(c));
    }
    // cabeza: (out=1 x hidden) + bias
    if (consumed + (size_t)hiddenSize + 1 != _w.size())
    { err = "LSTM: el numero de pesos no cuadra con la config"; return false; }
    _oLstmHead = consumed;
    _lstmOut.assign(1, 0.0f);
    _gates.assign((size_t)4 * hiddenSize, 0.0f);
    // La frecuencia la declara el fichero: se acota antes de convertirla a int,
    // o un sample_rate enorme desborda el cast y el precalentamiento se va.
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0))
    { err = "LSTM con sample_rate fuera de rango"; return false; }
    const double pw = 0.5 * sampleRate;
    prewarmSamples = (pw > (double)kDimMax) ? kDimMax : (int)pw;
    if (prewarmSamples < 1) prewarmSamples = 1;
    inChannels = 1;
    outChannels = 1;
    return true;
}

// --- ConvNet ---------------------------------------------------------------
inline bool Model::_parseConvNet(const std::string& s)
{
    const size_t B = 0, E = s.size();
    size_t cp;
    if (!js::find(s, B, E, "config", cp) || cp >= s.size() || s[cp] != '{')
    { err = "ConvNet sin config"; return false; }
    const size_t ce = js::valueEnd(s, cp);

    const int chans = js::integer(s, cp, ce, "channels", 1);
    const bool bn   = js::boolean(s, cp, ce, "batchnorm", false);
    const std::vector<int> dil = js::ints(s, cp, ce, "dilations");
    const Act act = actFromJson(s, cp, ce, "activation");
    if (act.kind == ACT_UNKNOWN) { err = "activacion no soportada en ConvNet"; return false; }
    if (dil.empty()) { err = "ConvNet sin dilations"; return false; }
    // Misma regla que en WaveNet: toda dimension y toda dilatacion tienen que
    // ser >= 1 o el bloque lee fuera de su vector en el hilo de audio.
    if (chans < 1 || chans > kDimMax) { err = "ConvNet con dimension fuera de rango"; return false; }
    for (size_t li = 0; li < dil.size(); li++)
    {
        if (dil[li] < 1)     { err = "ConvNet con dilatacion <= 0"; return false; }
        if (dil[li] > kDimMax) { err = "ConvNet con dilatacion fuera de rango"; return false; }
    }

    size_t consumed = 0;
    _blocks.clear();
    int cin = 1;
    for (size_t i = 0; i < dil.size(); i++)
    {
        CvBlock b;
        b.cin = cin; b.cout = chans; b.k = 2; b.d = dil[i]; b.bn = bn; b.act = act;
        size_t n = (size_t)b.k * b.cout * b.cin;
        if (!bn) n += b.cout;
        if (consumed + n > _w.size()) { err = "ConvNet: pesos insuficientes"; return false; }
        b.oW = consumed;
        b.oB = consumed + (size_t)b.k * b.cout * b.cin;
        consumed += n;
        if (bn)
        {
            // running_mean, running_var, weight, bias, eps (4 vectores + 1 suelto)
            if (consumed + (size_t)4 * b.cout + 1 > _w.size()) { err = "ConvNet: batchnorm incompleta"; return false; }
            b.oBn = b.oB;
            consumed += (size_t)4 * b.cout + 1;
        }
        b.ring.init(b.cin, b.d * (b.k - 1) + 1);
        _blocks.push_back(std::move(b));
        cin = chans;
    }
    // cabeza: (C -> 1, k=1) + bias
    if (consumed + (size_t)chans + 1 != _w.size())
    { err = "ConvNet: el numero de pesos no cuadra con la config"; return false; }
    _oCvHead = consumed;
    _cvBuf.assign(chans, 0.0f);
    // Igual que en WaveNet: se acumula en long y se acota, para que una
    // captura con muchisimas capas no desborde el int ni cuelgue la carga.
    long pw = 1;
    for (size_t i = 0; i < dil.size(); i++) pw += dil[i];
    prewarmSamples = (pw > (long)kDimMax) ? kDimMax : (int)pw;
    inChannels = 1;
    outChannels = 1;
    return true;
}

// ---------------------------------------------------------------------------
//  Ejecucion
// ---------------------------------------------------------------------------
inline void Model::reset()
{
    for (auto& a : _arrays)
    {
        for (auto& L : a.layers) L.ring.init(L.cin, L.d * (L.k - 1) + 1);
        // headRing con headOutSize (lo que consumo push/dot4), NO con bottleneck:
        // con head1x1 activo y out_channels != bottleneck el anillo quedaba con
        // el numero de canales equivocado y el hilo de audio leia fuera de rango.
        a.headRing.init(a.headOutSize, a.headD * (a.headK - 1) + 1);
        std::fill(a.xin.begin(), a.xin.end(), 0.0f);
        std::fill(a.re.begin(), a.re.end(), 0.0f);
        std::fill(a.prev.begin(), a.prev.end(), 0.0f);
        std::fill(a.headAcc.begin(), a.headAcc.end(), 0.0f);
        std::fill(a.headRe.begin(), a.headRe.end(), 0.0f);
    }
    for (size_t i = 0; i < _headRings.size(); i++) _headRings[i].init(_headRings[i].ch, _headRings[i].len);
    for (auto& c : _cells)
    {
        std::fill(c.xh.begin(), c.xh.end(), 0.0f);
        // estado inicial exportado en el propio fichero (h0 y c0 tras el burn-in)
        const size_t oH0 = c.oB + (size_t)4 * c.h;
        for (int i = 0; i < c.h; i++)
        {
            c.hh[i] = _w[oH0 + i];
            c.c[i]  = _w[oH0 + c.h + i];
        }
    }
    for (auto& b : _blocks) b.ring.init(b.cin, b.d * (b.k - 1) + 1);
    if (_condDsp) { _condDsp->reset(); for (auto& v : _condBuf) v = 0.0f; }
}

inline float Model::_waveNetTick(float x)
{
    // Condicion: la propia entrada (A1) o la salida del submodelo A2.
    const float* cond;
    if (_condDsp)
    {
        _condDsp->tick(x);
        for (int i = 0; i < (int)_condBuf.size(); i++) _condBuf[i] = _condDsp->_outBuf[i];
        cond = _condBuf.data();
    }
    else cond = &x;

    float lastOut = 0.0f;
    bool first = true;
    for (size_t ai = 0; ai < _arrays.size(); ai++)
    {
        WnArray& a = _arrays[ai];
        if (first) { a.xin[0] = x; first = false; }
        conv1x1(&_w[a.oRe], a.inSize, a.ch, a.xin.data(), a.re.data());
        std::copy(a.re.begin(), a.re.end(), a.prev.begin());
        if (ai > 0)
        {
            const WnArray& pv = _arrays[ai - 1];
            const int n = a.headOutSize < (int)pv.headRe.size() ? a.headOutSize : (int)pv.headRe.size();
            for (int i = 0; i < n; i++) a.headAcc[i] = pv.headRe[i];
            for (int i = n; i < a.headOutSize; i++) a.headAcc[i] = 0.0f;
        }
        else std::fill(a.headAcc.begin(), a.headAcc.end(), 0.0f);

        for (size_t li = 0; li < a.layers.size(); li++)
        {
            WnLayer& L = a.layers[li];
            // entrada de la conv (con FiLM previo)
            const float* convIn = a.prev.data();
            if (L.fConvPre.active)
            {
                filmApply(L.fConvPre, &_w[L.fConvPre.oW], a.prev.data(), cond, L.fConvPre.ss.data(), L.tmpA.data());
                convIn = L.tmpA.data();
            }
            L.ring.push(convIn);
            const float* wc = &_w[L.oConv];
            const float* bias = wc + (size_t)L.k * L.cout * L.cin / L.groupsIn;
            const int outPer = L.cout / L.groupsIn, inPer = L.cin / L.groupsIn;
            for (int o = 0; o < L.cout; o++)
            {
                const int g = o / outPer, oi = o % outPer;
                float acc = bias[o];
                for (int kk = 0; kk < L.k; kk++)
                {
                    const float* wkk = &L.wp[(((size_t)kk*L.groupsIn + g)*outPer + oi)*inPer];
                    const float* xkk = L.ring.at(L.d * (L.k - 1 - kk)) + (size_t)g*inPer;
                    acc += dot4(wkk, xkk, inPer);
                }
                L.z[o] = acc;
            }
            if (L.fConvPost.active)
                filmApply(L.fConvPost, &_w[L.fConvPost.oW], L.z.data(), cond, L.fConvPost.ss.data(), L.z.data());

            // mixin: su entrada es la condicion (con FiLM previo)
            const float* mixIn = cond;
            if (L.fMixPre.active)
            {
                filmApply(L.fMixPre, &_w[L.fMixPre.oW], cond, cond, L.fMixPre.ss.data(), L.tmpB.data());
                mixIn = L.tmpB.data();
            }
            const float* wm = &_w[L.oMix];
            const int moutPer = L.cout / L.groupsMix, minPer = L.mixIn / L.groupsMix;
            float* mixOut = L.tmpC.data();
            for (int o = 0; o < L.cout; o++)
            {
                const int g = o / moutPer, oi = o % moutPer;
                const float* wi = wm + ((size_t)g*moutPer + oi)*minPer;
                const float* cd = mixIn + (size_t)g*minPer;
                float acc = 0.0f;
                for (int c = 0; c < minPer; c++) acc += wi[c]*cd[c];
                mixOut[o] = acc;
            }
            if (L.fMixPost.active)
                filmApply(L.fMixPost, &_w[L.fMixPost.oW], mixOut, cond, L.fMixPost.ss.data(), mixOut);
            for (int o = 0; o < L.cout; o++) L.z[o] += mixOut[o];
            if (L.fActPre.active)
                filmApply(L.fActPre, &_w[L.fActPre.oW], L.z.data(), cond, L.fActPre.ss.data(), L.z.data());
            for (int i = 0; i < L.cout; i++) L.z[i] = actApply(L.act, L.z[i]);
            if (L.fActPost.active)
                filmApply(L.fActPost, &_w[L.fActPost.oW], L.z.data(), cond, L.fActPost.ss.data(), L.z.data());

            // residual: entrada + layer1x1(activada)
            if (L.has1x1)
            {
                const float* w1 = &_w[L.oL1];
                const float* b1 = w1 + (size_t)a.bnl * a.ch / L.l1groups;
                const int loutPer = a.ch / L.l1groups, linPer = a.bnl / L.l1groups;
                for (int o = 0; o < a.ch; o++)
                {
                    const int g = o / loutPer, oi = o % loutPer;
                    const float* wi = w1 + ((size_t)g*loutPer + oi)*linPer;
                    const float* zd = L.z.data() + (size_t)g*linPer;
                    float acc = b1[o];
                    for (int c = 0; c < linPer; c++) acc += wi[c]*zd[c];
                    L.y[o] = acc;
                }
                if (L.fL1Post.active)
                    filmApply(L.fL1Post, &_w[L.fL1Post.oW], L.y.data(), cond, L.fL1Post.ss.data(), L.y.data());
                for (int i = 0; i < a.ch; i++) a.prev[i] += L.y[i];
            }
            // salida de cabeza: head1x1(z) o la propia z
            if (L.hasH1)
            {
                const float* w1 = &_w[L.oH1];
                const float* b1 = w1 + (size_t)L.h1out * a.bnl / L.h1groups;
                const int houtPer = L.h1out / L.h1groups, hinPer = a.bnl / L.h1groups;
                for (int o = 0; o < L.h1out; o++)
                {
                    const int g = o / houtPer, oi = o % houtPer;
                    const float* wi = w1 + ((size_t)g*houtPer + oi)*hinPer;
                    const float* zd = L.z.data() + (size_t)g*hinPer;
                    float acc = b1[o];
                    for (int c = 0; c < hinPer; c++) acc += wi[c]*zd[c];
                    L.headOut[o] = acc;
                }
                if (L.fH1Post.active)
                    filmApply(L.fH1Post, &_w[L.fH1Post.oW], L.headOut.data(), cond, L.fH1Post.ss.data(), L.headOut.data());
                for (int i = 0; i < a.headOutSize; i++) a.headAcc[i] += L.headOut[i];
            }
            else for (int i = 0; i < a.bnl; i++) a.headAcc[i] += L.z[i];
        }

        // head rechannel (convolucion, bias opcional) sobre la acumulacion
        a.headRing.push(a.headAcc.data());
        const float* wh = &_w[a.oHeadRe];
        const float* bh = wh + (size_t)a.headSize * a.headOutSize * a.headK;
        for (int i = 0; i < a.headSize; i++)
        {
            float acc = a.headBias ? bh[i] : 0.0f;
            for (int kk = 0; kk < a.headK; kk++)
                acc += dot4(&a.whp[((size_t)kk * a.headSize + i) * a.headOutSize],
                            a.headRing.at(a.headD * (a.headK - 1 - kk)), a.headOutSize);
            a.headRe[i] = acc;
        }
        if (ai + 1 < _arrays.size())
        {
            WnArray& nx = _arrays[ai + 1];
            const int n = nx.inSize < a.ch ? nx.inSize : a.ch;
            for (int i = 0; i < n; i++) nx.xin[i] = a.prev[i];
        }
        lastOut = a.headRe[0];
        if (a.headSize > 1)
        {
            _outBuf.assign(_outBuf.size() >= (size_t)a.headSize ? _outBuf.size() : (size_t)a.headSize, 0.0f);
            for (int i = 0; i < a.headSize; i++) _outBuf[i] = _headScale * a.headRe[i];
        }
    }

    float out = _headScale * lastOut;
    if (_hasHead)
    {
        const int n0 = _arrays.back().headSize;
        _headCur.assign(_headCur.size() >= (size_t)n0 ? _headCur.size() : (size_t)n0, 0.0f);
        for (int i = 0; i < n0; i++) _headCur[i] = _arrays.back().headRe[i];
        int curN = n0;
        size_t off = 0;
        for (size_t i = 0; i < _headKs.size(); i++)
        {
            for (int j = 0; j < curN; j++) _headCur[j] = actApply(_headAct, _headCur[j]);
            _headRings[i].push(_headCur.data());
            const int cin = _headRings[i].ch;
            const int cout = (i + 1 == _headKs.size()) ? _headOut : _headCh;
            _headDst.assign(_headDst.size() >= (size_t)cout ? _headDst.size() : (size_t)cout, 0.0f);
            const float* w = &_w[_oHeadW];
            for (int o = 0; o < cout; o++)
            {
                float acc = w[off + (size_t)cout * cin * _headKs[i] + o];
                for (int kk = 0; kk < _headKs[i]; kk++)
                {
                    const float* xkk = _headRings[i].at(_headKs[i] - 1 - kk);
                    for (int c = 0; c < cin; c++)
                        acc += w[off + ((size_t)o * cin + c) * _headKs[i] + kk] * xkk[c];
                }
                _headDst[o] = acc;
            }
            for (int j = 0; j < cout; j++) _headCur[j] = _headDst[j];
            curN = cout;
            off += (size_t)_headKs[i] * cout * cin + cout;
        }
        out = _headCur[0];
        _outBuf.assign(_outBuf.size() >= (size_t)curN ? _outBuf.size() : (size_t)curN, 0.0f);
        for (int i = 0; i < curN; i++) _outBuf[i] = _headCur[i];
    }
    else if (outChannels <= 1)
    {
        // Modelo SIN head post-stack y de UN canal: la salida es el escalar. Si el
        // modelo es multicanal, el buffer ya lo llenaron los arrays (headSize > 1)
        // y no se toca: `assign(n, 0.0f)` pone a cero TODO el vector, asi que este
        // camino destruia la condicion multicanal de A2 (los canales 1..N salian
        // en cero y el modelo exterior recibia media condicion).
        _outBuf.assign(_outBuf.size() >= 1 ? _outBuf.size() : 1, 0.0f);
        _outBuf[0] = out;
    }
    return out;
}

inline float Model::_lstmTick(float x)
{
    // capa 0: la entrada del modelo; siguientes: el vector h de la anterior
    _cells[0].xh[0] = x;
    for (size_t li = 0; li < _cells.size(); li++)
    {
        LstmCell& c = _cells[li];
        const int H = c.h;
        if (li > 0)
        {
            const LstmCell& pv = _cells[li - 1];
            for (int i = 0; i < c.in; i++) c.xh[i] = pv.hh[i];
        }
        for (int i = 0; i < H; i++) c.xh[c.in + i] = c.hh[i];
        const float* W = &_w[c.oW];
        const float* b = &_w[c.oB];
        const int N = c.in + H;
        for (int i = 0; i < 4 * H; i++)
        {
            float acc = b[i];
            const float* wi = W + (size_t)i * N;
            for (int j = 0; j < N; j++) acc += wi[j] * c.xh[j];
            _gates[i] = acc;
        }
        for (int i = 0; i < H; i++)
        {
            const float gi = 1.0f / (1.0f + std::exp(-_gates[i]));
            const float gf = 1.0f / (1.0f + std::exp(-_gates[H + i]));
            const float gg = std::tanh(_gates[2 * H + i]);
            const float go = 1.0f / (1.0f + std::exp(-_gates[3 * H + i]));
            c.c[i] = gf * c.c[i] + gi * gg;
            c.hh[i] = go * std::tanh(c.c[i]);
        }
    }
    const LstmCell& last = _cells.back();
    const float* wh = &_w[_oLstmHead];
    float acc = _w[_oLstmHead + last.h];
    for (int i = 0; i < last.h; i++) acc += wh[i] * last.hh[i];
    _outBuf.assign(1, acc);
    return acc;
}

inline float Model::_convNetTick(float x)
{
    _cvCur.assign(_cvCur.size() >= 1 ? _cvCur.size() : 1, 0.0f);
    _cvCur[0] = x;
    int curN = 1;
    for (size_t bi = 0; bi < _blocks.size(); bi++)
    {
        CvBlock& b = _blocks[bi];
        b.ring.push(_cvCur.data());
        _cvOut.assign(_cvOut.size() >= (size_t)b.cout ? _cvOut.size() : (size_t)b.cout, 0.0f);
        const float* w = &_w[b.oW];
        for (int o = 0; o < b.cout; o++)
        {
            float acc = 0.0f;
            for (int kk = 0; kk < b.k; kk++)
            {
                const int delay = b.d * (b.k - 1 - kk);
                const float* xkk = b.ring.at(delay);
                for (int c = 0; c < b.cin; c++)
                    acc += w[((size_t)o * b.cin + c) * b.k + kk] * xkk[c];
            }
            if (!b.bn) acc += _w[b.oB + o];
            else
            {
                // batchnorm: scale = gamma/sqrt(eps+var); loc = beta - scale*mean
                const float mean  = _w[b.oBn + o];
                const float var   = _w[b.oBn + b.cout + o];
                const float gamma = _w[b.oBn + 2 * b.cout + o];
                const float beta  = _w[b.oBn + 3 * b.cout + o];
                const float eps   = _w[b.oBn + 4 * b.cout];
                const float scale = gamma / std::sqrt(eps + var);
                acc = acc * scale + (beta - scale * mean);
            }
            _cvOut[o] = actApply(b.act, acc);
        }
        for (int o = 0; o < b.cout; o++) _cvCur[o] = _cvOut[o];
        curN = b.cout;
    }
    const float* wh = &_w[_oCvHead];
    float acc = _w[_oCvHead + curN];
    for (int c = 0; c < curN; c++) acc += wh[c] * _cvCur[c];
    return acc;
}

inline float Model::tick(float x)
{
    switch (_arch)
    {
        case A_WAVENET: return _waveNetTick(x);
        case A_LSTM:    return _lstmTick(x);
        case A_CONVNET: return _convNetTick(x);
        default:        return x;
    }
}

} // namespace nam
