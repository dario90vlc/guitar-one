// ============================================================================
//  GuitarAmp Native - Interfaz estilo GUITAR ONE
//  - Win32 + GDI+ (ventana nativa, sin navegador)
//  - WASAPI event-driven, buffers configurables => latencia minima
//  - 13 amplificadores (12 voces de valvulas + PERFIL con cadenas NAM) y
//    explorador TONE3000
//  - 18 efectos en dos filas (antes y despues del ampli) + cabina con IR
//  - Cab con IR (.wav), afinador cromatico, visualizador, grabadora WAV
//  - Medidores INPUT/OUTPUT, presets, master
//  - Panel AJUSTES estilo app kAcc (motor, frecuencia, buffer, dispositivos)
//  - Compilar: cl /O2 /EHsc /std:c++17 GuitarAmpNative.cpp
//              /Fe:GuitarAmp.exe /link ole32.lib oleaut32.lib uuid.lib
//              user32.lib gdi32.lib propsys.lib gdiplus.lib avrt.lib
//              (avrt.lib = MMCSS "Pro Audio", la prioridad del hilo de audio)
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX              // sin macros min/max de windows: el DSP usa std::min/std::max
#define _USE_MATH_DEFINES
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>   // solo para LEER el volumen del endpoint (nunca escribirlo)
#include <audioclient.h>
#include <avrt.h>             // MMCSS: la prioridad de tiempo real del hilo de audio
#include <mmreg.h>
#include <propsys.h>
#include <propidl.h>
#include <wincrypt.h>          // DPAPI: el token de TONE3000 va cifrado en disco
#include <gdiplus.h>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <immintrin.h>
// comdlg32.h no viene en este SDK (instalacion parcial): declaraciones minimas.
// El ABI de OPENFILENAMEW/GetOpenFileNameW es estable desde Windows 2000 y el
// import lo aporta comdlg32.lib (ya esta en la linea de enlazado).
#ifndef _INC_COMMDLG
typedef struct tagOFNW {
    DWORD        lStructSize;
    HWND         hwndOwner;
    HINSTANCE    hInstance;
    LPCWSTR      lpstrFilter;
    LPWSTR       lpstrCustomFilter;
    DWORD        nMaxCustFilter;
    DWORD        nFilterIndex;
    LPWSTR       lpstrFile;
    DWORD        nMaxFile;
    LPWSTR       lpstrFileTitle;
    DWORD        nMaxFileTitle;
    LPCWSTR      lpstrInitialDir;
    LPCWSTR      lpstrTitle;
    DWORD        Flags;
    WORD         nFileOffset;
    WORD         nFileExtension;
    LPCWSTR      lpstrDefExt;
    LPARAM       lCustData;
    LPVOID       lpfnHook;
    LPCWSTR      lpTemplateName;
    void*        pvReserved;
    DWORD        dwReserved;
    DWORD        dwExFlags;
} OPENFILENAMEW, *LPOPENFILENAMEW;
#define OFN_READONLY             0x00000001
#define OFN_OVERWRITEPROMPT      0x00000002
#define OFN_HIDEREADONLY         0x00000004
#define OFN_NOCHANGEDIR          0x00000008
#define OFN_SHOWHELP             0x00000010
#define OFN_ENABLEHOOK           0x00000020
#define OFN_ENABLETEMPLATE       0x00000040
#define OFN_ENABLETEMPLATEHANDLE 0x00000080
#define OFN_NOVALIDATE           0x00000100
#define OFN_ALLOWMULTISELECT     0x00000200
#define OFN_EXTENSIONDIFFERENT   0x00000400
#define OFN_PATHMUSTEXIST        0x00000800
#define OFN_FILEMUSTEXIST        0x00001000
#define OFN_CREATEPROMPT         0x00002000
#define OFN_SHAREAWARE           0x00004000
#define OFN_NOREADONLYRETURN     0x00008000
#define OFN_NOTESTFILECREATE     0x00010000
#define OFN_NONETWORKBUTTON      0x00020000
#define OFN_NOLONGNAMES          0x00040000
#define OFN_EXPLORER             0x00080000
#define OFN_NODEREFERENCELINKS   0x00100000
#define OFN_LONGNAMES            0x00200000
#define OFN_ENABLEINCLUDENOTIFY  0x00400000
#define OFN_ENABLESIZING         0x00800000
#define OFN_DONTADDTORECENT      0x02000000
#define OFN_FORCESHOWHIDDEN      0x10000000
extern "C" BOOL __stdcall GetOpenFileNameW(LPOPENFILENAMEW);
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <new>

// Traduccion de textos de interfaz (tabla y funcion, mas abajo).
static const WCHAR* tr(const WCHAR* es);

// ============================================================================
//  AUDITORIA DE RESERVAS DEL CAMINO DE TIEMPO REAL (solo el banco de pruebas)
//  T13 y T15 comprueban que procesar un bloque de audio no llama a new. El hilo
//  de audio no puede reservar memoria: un malloc puede tardar cientos de
//  microsegundos y con buffer de 512 muestras a 48 kHz el bloque entero son
//  10,7 ms, asi que una reserva que caiga mal es un crujido audible mientras se
//  toca. La auditoria esta APAGADA salvo dentro de la ventana que mide la prueba,
//  que es cuando alguien mira el contador.
// ============================================================================
#ifdef GA_HARNESS
static std::atomic<long long> g_allocCount{0};
static std::atomic<bool>      g_allocAuditOn{false};
static void* hAlloc(size_t n) {
    if (g_allocAuditOn.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(size_t n) { return hAlloc(n); }
void* operator new[](size_t n) { return hAlloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#endif

namespace G = Gdiplus;

// ============================================================================
//  CONSOLA DE CRISTAL - el lenguaje visual de la aplicacion (v2).
//  La ventana es una consola de software, no un rack de hardware. Sobre un
//  lienzo casi negro se apilan capas de CRISTAL translucido con aire entre
//  ellas, y una sola luz -desde arriba- ordena el conjunto: el canto superior
//  de cada panel coge esa luz y cae en sombra, y los pozos se hunden. Fuera
//  el skeuomorfismo: no hay tornillos, ni carriles, ni tolex, ni chapa grabada.
//  El unico color fotografico lo pone el equipo -los frontales de los amplis,
//  la tela de la cabina y las caras de los pedales-, y la jerarquia la lleva la
//  tipografia, no el color: las secciones mandan, el cuerpo escucha.
//
//  Reglas del sistema (si algo las rompe, se ve como una maqueta):
//   1. Una sola luz, desde arriba. La profundidad la da esa luz y una sombra
//      ambiental ancha; nunca un bisel ni un filete "porque si".
//   2. El cristal se pinta en dos paradas (arriba iluminado, abajo en sombra)
//      con el lienzo intuible debajo: es translucido, no una placa opaca.
//   3. El ambar (kAcc) solo aparece en: valor activo, LED encendido, aguja,
//      seleccion y foco de teclado. Nunca como decoracion.
//   4. Nada se adorna dos veces. Si una superficie ya lleva luz arriba, no
//      lleva tambien halo; si ya lleva borde, no lleva tambien sombra dura.
// ============================================================================
static const G::Color kBack   (255, 0x0a, 0x0a, 0x0c);   // lienzo
static const G::Color kWellA  (255, 0x07, 0x07, 0x09);   // pozo: fondo
static const G::Color kWellLn (255, 0x22, 0x22, 0x28);
static const G::Color kAcc    (255, 0xff, 0xa5, 0x1f);   // ambar de filamento
static const G::Color kAccGlb (255, 0xff, 0xe0, 0xa0);   // ambar claro (especular)
static const G::Color kHot    (255, 0xff, 0x45, 0x4f);   // solo techo/clip
static const G::Color kBtnBg  (255, 0x26, 0x26, 0x2c);
static const G::Color kBtnBgH (255, 0x33, 0x33, 0x3a);
static const G::Color kBtnLn  (255, 0x35, 0x35, 0x3d);

// --- Lenguaje plano (v3): el lenguaje ACTUAL -------------------------------
// Un tono por superficie y un filete de 1px. Sin degradados, sin sombras, sin
// bandas de luz: la profundidad la da el TONO y la unica luz de la interfaz es
// el acento. (El cristal de arriba queda solo como referencia de lo que habia.)
static const G::Color kFBg   (255, 0x17, 0x17, 0x1a);   // lienzo
static const G::Color kFSurf (255, 0x1f, 0x1f, 0x23);   // placa
static const G::Color kFSurf2(255, 0x26, 0x26, 0x2b);   // control, pastilla, ranura
static const G::Color kFSurf3(255, 0x2e, 0x2e, 0x34);   // superficie alta (hover/activo)
static const G::Color kFLine (255, 0x2a, 0x2a, 0x30);   // filete de placa
// Tres niveles de texto, uno por cometido: TITULO, VALOR y ROTULO. Ademas
// del tamano, que es lo que manda, cada nivel baja un escalon de tono; los
// contrastes estan medidos sobre la placa (kFSurf), y el rotulo se queda justo
// en el umbral AA porque es lo mas tenue que puede ser sin dejar de leerse.
static const G::Color kFTxt  (255, 0xf2, 0xf0, 0xea);   // titulo: marfil (no blanco puro)
static const G::Color kFTxt2 (255, 0xa6, 0xa3, 0x9d);   // valor:  14:1 / 6,6:1
static const G::Color kFTxt3 (255, 0x8b, 0x87, 0x81);   // rotulo: 4,6:1
static const G::Color kFHot  (255, 0xff, 0x5f, 0x57);
// Rojo de AVISO, un escalon por debajo del de clip: es para el unico estado en
// el que la app NO suena (AUDIO OFF). No es un segundo acento decorativo -el
// ambar sigue siendo el unico "vivo"- sino la marca de "esto esta apagado y por
// eso no oyes nada", que es justo lo contrario de lo que dice el ambar.
static const G::Color kFOff  (255, 0xd9, 0x5c, 0x50);

// --- Radios y aire ----------------------------------------------------------
static const G::REAL kRPanel = 12.0f;   // paneles y unidades
static const G::REAL kRCard  = 10.0f;   // tarjetas y modulos
static const G::REAL kRWell  =  8.0f;   // pozos
static const G::REAL kRPill  =  9.0f;   // pastillas y botones

// --- Escala tipografica: UNA tabla, un PAPEL por texto ----------------------
// GDI+ construye Gdiplus::Font en PUNTOS (1pt = 1,333px a 96 dpi). Un sitio de
// texto no elige una medida a ojo: elige su papel (TxRole) y el embudo pone
// familia, tamano y suelo de ajuste. Antes esto era 19 tamanos distintos
// repartidos en 62 sitios, todos en Segoe UI: la jerarquia era accidental.
// Tres familias, y las tres las trae Windows (no hay archivos que incrustar):
//   Segoe UI .................. cuerpo y nombres
//   Bahnschrift SemiCondensed . rotulos y titulos (tecnica, DIN)
//   Cascadia Mono ............. cifras que cambian (tabulares: no bailan)
static const WCHAR* kFontUI   = L"Segoe UI";
static const WCHAR* kFontTec  = L"Bahnschrift SemiCondensed";
static const WCHAR* kFontMono = L"Cascadia Mono";

enum TxRole { TxMicro, TxPico, TxRotulo, TxCuerpo, TxValor, TxNombre, TxTitulo, TxMarca,
              TxRoleCount };
struct TxStep { const WCHAR* fam; G::REAL pt, min; const char* what; };
static const TxStep kTx[TxRoleCount] = {
    { kFontTec,   6.6f,  6.0f, "micro: dentro del arte de la tarjeta de pedal" },
    { kFontTec,   8.0f,  6.5f, "pico: chips, contadores, pastillas" },
    { kFontTec,   9.5f,  7.5f, "rotulo: mayusculas, botones, pestanas" },
    { kFontUI,   10.5f,  8.5f, "cuerpo: pistas, subtitulos, textos largos" },
    { kFontMono, 12.5f,  9.0f, "valor: cifras que cambian" },
    { kFontUI,   13.0f, 10.5f, "nombre: listas y presets" },
    { kFontTec,  15.0f, 11.0f, "titulo: titulos de zona" },
    { kFontTec,  20.0f, 12.0f, "marca y titulo de modal" },
};
// Medidas sueltas: SOLO las figuras de display del afinador, que no son texto
// de interfaz y se miden aparte de la escala.
static const G::REAL kTDisplay  = 88.0f;   // nota del afinador (display)
static const G::REAL kTDisplay2 = 26.0f;   // "--" del afinador en reposo
static const G::REAL kTValor  = kTx[TxValor].pt;    // 12,5
static const G::REAL kTPico   = kTx[TxPico].pt;     // 8,0
static const G::REAL kTRotulo = kTx[TxRotulo].pt;   // 9,5
static const G::REAL kTCuerpo = kTx[TxCuerpo].pt;   // 10,5
static const G::REAL kTNombre = kTx[TxNombre].pt;   // 13,0
static const G::REAL kTMarca  = kTx[TxMarca].pt;    // 20,0

// El grano de 1px se retira: sobre un fondo casi puro leia como suciedad, no
// como material, y barrerlo costaba ~150 ms en la construccion de escena. Con
// el equipo ya plano se va tambien hash21, que era su unico usuario (el tolex).
// Acento translucido reutilizable (velos, halos, marcas)
static inline G::Color accA(int a) {
    return G::Color((BYTE)a, kAcc.GetR(), kAcc.GetG(), kAcc.GetB());
}

// --- Frontales de amps (arte amp_frontales_v2: fondo + filmstrip de perillas) ---
// El zip trae por amp: fondo 1200x540 (panel sin perillas), filmstrip de 101
// fotogramas (perilla girando -135..+135; fotograma = round(valor)) y JSON con
// el centro de cada perilla. Si falta el arte, todo vuelve a la cara procedural.
#include "amp_frontales_data.h"
#include "t3k.h"

static std::wstring wstr(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static const std::wstring& wstr(const std::wstring& s) { return s; }

// Estrecha una cadena ancha a ASCII. Se usa solo para el payload de DPAPI, que
// es base64 y por construccion no tiene nada fuera de A-Z a-z 0-9 + / =. No es
// un cast (MSVC lo marca C4244 porque truncar un wchar_t puede comerse el byte
// siguiente): lo que no sea ASCII se degrada a '?', que es justo lo que
// devolverna las funciones W de la API de perfiles al escribir.
static std::string asciiOf(const wchar_t* w) {
    std::string s;
    for (const wchar_t* p = w; p && *p; ++p) s.push_back((*p < 128) ? (char)*p : '?');
    return s;
}

// ---------------------------------------------------------------------------
//  Perfiles NAM (.nam): cadena de capturas del cabezal PERFIL
// ---------------------------------------------------------------------------
//  El cabezal PERFIL ya no es un solo fichero: es una CADENA de bloques, como
//  la de TONE3000. Cada bloque es una captura, con su encendido y su nivel, y
//  el orden se cambia moviendolo. El audio los recorre en serie.
//  El hilo de audio NO toca los punteros compartidos (ver g_namAudio: coger un
//  shared_ptr atomico es un spin-lock). Lo que ve es una instantanea inmutable
//  por bloque; el hilo de UI es el que carga, reordena y publica.
#include "nam.h"
struct NamSlot {
    std::shared_ptr<nam::Model> model;
    std::wstring path, name;
    std::atomic<bool>  on    { true };
    std::atomic<float> inTrim{ 1.0f }, outTrim{ 1.0f }, level{ 1.0f };
};
using NamChain = std::vector<std::shared_ptr<NamSlot>>;
static NamChain g_namUi;                                   // la cadena que edita la UI
static std::shared_ptr<const NamChain> g_namLive;          // instantanea para el audio
static std::mutex g_namMutex;
// Cuantas veces ha cogido el hilo de audio la instantanea del perfil. Tiene que
// ser UNA por bloque: cogerla por muestra es un cerrojo por muestra (T12).
static std::atomic<long long> g_namSnapGets{0};
// Puntero CRUDO para el camino de tiempo real. std::atomic_load sobre un
// shared_ptr NO es libre de cerrojo (MSVC lo implementa con un spin-lock): ahi
// el hilo de audio se jugaba el buffer contra la interfaz, que es quien lo
// suelta, desde prioridad normal. Cargar este atomico es un MOV: sin cerrojo y
// sin tocar el contador de referencias. Lo que apunta se mantiene vivo con la
// lista de retirados (g_namRetiro).
static std::atomic<const NamChain*> g_namAudio{nullptr};
// Bloques de audio empezados. Lo lee la interfaz para no soltar una cadena que
// un bloque en vuelo todavia pueda estar usando.
static std::atomic<long long> g_audioBlocks{0};
struct NamRetiro { std::shared_ptr<const NamChain> ch; long long epoch; };
static std::vector<NamRetiro> g_namRetiro;                  // solo hilo UI
static int  g_namSel = -1;                                 // bloque seleccionado
static int  g_namDelArm = -1;                              // bloque armado para quitar (2 clics)
static int  g_namDropIdx = -1;                             // celda elegida para lo que se baje (-1 = al final)
static std::wstring g_namMsg;                              // estado para la chapa
static std::atomic<float> g_namInTrim{ 1.0f }, g_namOutTrim{ 1.0f };   // del bloque mostrado
static int  namBlkVis();                                   // cuantos bloques caben (geometria, mas abajo)
static int  namChainAddAt(const WCHAR* path, int pos, std::wstring& msg);

static void namPublish()
{
    std::lock_guard<std::mutex> lk(g_namMutex);
    std::shared_ptr<const NamChain> nu = std::make_shared<const NamChain>(g_namUi);
    std::atomic_store(&g_namLive, nu);                        // lo que pinta la interfaz
    // La cadena nueva entra en la lista de retirados (que es quien la mantiene
    // viva) y se publica el puntero crudo. La anterior no se suelta aqui: se
    // suelta en namRetiroFlush(), cuando el hilo de audio ya paso por delante.
    g_namRetiro.push_back({ nu, g_audioBlocks.load(std::memory_order_acquire) });
    g_namAudio.store(nu.get(), std::memory_order_release);
    g_namSel = (g_namSel >= (int)g_namUi.size()) ? (int)g_namUi.size() - 1 : g_namSel;
}

// Normalizacion por niveles del fichero (dos capturas pueden diferir 20 dB)
static void namComputeTrims(const nam::Model& m, NamSlot& sl)
{
    const float refIn = 12.0f, refOut = 12.0f;
    float inDb = 0.0f, outDb = 0.0f;
    if (m.hasLevels) { inDb = refIn - m.inDbu; outDb = refOut - m.outDbu; }
    else if (m.loudness != 0.0f) outDb = m.loudness + 20.0f;
    if (inDb < -24.0f) inDb = -24.0f;  if (inDb > 24.0f) inDb = 24.0f;
    if (outDb < -24.0f) outDb = -24.0f; if (outDb > 24.0f) outDb = 24.0f;
    sl.inTrim.store(std::pow(10.0f, inDb/20.0f));
    sl.outTrim.store(std::pow(10.0f, outDb/20.0f));
}

// ---------------------------------------------------------------------------
//  Conversion de frecuencia para las capturas
// ---------------------------------------------------------------------------
//  Un .nam esta entrenado a UNA frecuencia (casi siempre 48 kHz) y el motor puede
//  correr a otra (44,1 / 88,2 / 96 / 176,4 / 192). Ejecutar el modelo a la tasa
//  equivocada no es "casi igual": sus filtros, resonancias y el propio caracter
//  del ampli quedan desplazados en la proporcion entre las dos tasas (un 8,8 %
//  entre 44,1 y 48). Aqui la senal se lleva a la tasa del modelo y se devuelve
//  con un interpolador polifasico (sinc con ventana de Hann, 24 coeficientes,
//  256 fases): 24 multiplicaciones por muestra, nada al lado del modelo.
//
//  El modelo se ejecuta a SU tasa en el mismo bucle: por cada muestra del motor
//  salen 0, 1 o 2 del modelo. La salida se INTERPOLA entre los dos ultimos
//  valores del modelo (quedarse con el ultimo mete un salto de fase de hasta una
//  muestra del modelo cada ~11 muestras a 48k->44,1k, que se oye como grano en
//  las notas agudas). Interpolar un periodo de modelo por detras garantiza que
//  el instante pedido caiga SIEMPRE entre los dos valores que hay.
//  Latencia: 12 muestras del motor + 1 del modelo (0,27 ms a 48 kHz).
struct NamRateConv {
    static const int TAPS = 24;
    static const int PH   = 256;
    static const int RING = 64;      // potencia de 2 y > TAPS + lo que se adelanta
    static const int LAG  = 12;      // retardo de la ventana = TAPS/2

    float tab[PH][TAPS];             // 24 KB por bloque: se calcula al configurar
    float hist[RING];
    int   w = 0;
    float tCur = 0.0f, tNext = 0.0f; // instantes (en muestras del motor)
    float tauLast = -1e9f;           // instante del ultimo valor del modelo
    float outPrev = 0.0f, outLast = 0.0f;
    double fm = 48000.0, fd = 48000.0;
    bool  byb  = true;
    bool  ready = false;

    NamRateConv() { reset(); }

    void reset()
    {
        for (int i = 0; i < RING; ++i) hist[i] = 0.0f;
        w = 0; tCur = 0.0f;
        tNext = -(float)(LAG - 1);
        tauLast = -1e9f; outPrev = outLast = 0.0f;
    }

    void configure(double modelRate, double deviceRate)
    {
        if (ready && std::fabs(modelRate - fm) < 0.5 && std::fabs(deviceRate - fd) < 0.5) return;
        fm = modelRate; fd = deviceRate;
        byb = (std::fabs(modelRate - deviceRate) < 0.5);
        ready = true;
        reset();
        if (byb) return;
        // Corte en el menor de los dos Nyquist, con un 8 % de margen para que la
        // banda de transicion de 24 taps caiga por debajo del audible.
        const double c = 0.5 * (fm < fd ? fm/fd : 1.0) * 0.92;
        for (int p = 0; p < PH; ++p)
        {
            const double f = (double)p / (double)PH;
            double sum = 0.0;
            for (int t = 0; t < TAPS; ++t)
            {
                const double d = (double)t - (double)(LAG - 1) - f;
                const double s = (std::fabs(d) < 1e-9) ? 1.0
                               : std::sin(2.0*M_PI*c*d)/(M_PI*d);
                const double win = 0.5*(1.0 + std::cos(M_PI*d/(double)LAG));   // Hann
                tab[p][t] = (float)(s*win);
                sum += s*win;
            }
            // Ganancia unidad en continua por fase: sin esto cada fase pesa
            // distinto y el nivel baila con la posicion.
            if (std::fabs(sum) > 1e-9)
                for (int t = 0; t < TAPS; ++t) tab[p][t] = (float)((double)tab[p][t]/sum);
        }
    }

    // Valor interpolado a `d` muestras por detras de la ultima metida.
    inline float at(float d) const
    {
        int di = (int)d;
        float f = d - (float)di;
        if (f < 0.0f) { f += 1.0f; --di; }
        int ph = (int)(f*(float)PH);
        if (ph < 0) ph = 0;
        if (ph >= PH) ph = PH - 1;
        const float* h = tab[ph];
        float acc = 0.0f;
        for (int t = 0; t < TAPS; ++t)
        {
            const int k = di - (LAG - 1) + t;          // retraso absoluto
            acc += hist[(w - 1 - k) & (RING - 1)] * h[t];
        }
        return acc;
    }

    // Mete una muestra del motor y devuelve la salida del modelo a su tasa.
    template <class F>
    inline float tick(F&& modelTick, float x)
    {
        if (byb) return modelTick(x);
        hist[w] = x;
        w = (w + 1) & (RING - 1);
        tCur += 1.0f;
        const float step = (float)(fd/fm);        // muestras del motor por muestra del modelo
        const float lim  = tCur - (float)LAG;     // no se lee mas cerca que la ventana
        while (tNext <= lim)
        {
            outPrev = outLast;
            outLast = modelTick(at(tCur - tNext));
            tauLast = tNext;
            tNext += step;
        }
        // Salida interpolada un periodo de modelo por detras: asi el instante
        // pedido cae siempre entre outPrev y outLast (ver la cabecera).
        const float tOut = tCur - (float)LAG - step;
        if (tauLast < -1e8f) return outLast;              // aun no hay dos valores
        const float span = step;
        float a = (tOut - (tauLast - span)) / span;
        if (a < 0.0f) a = 0.0f; else if (a > 1.0f) a = 1.0f;
        return outPrev + (outLast - outPrev)*a;
    }
};

// Carga un .nam y lo ANADE como bloque (devuelve el indice, -1 si falla)
static int namChainAdd(const WCHAR* path, std::wstring& msg) { return namChainAddAt(path, -1, msg); }
// Igual, pero colocando el bloque en una CELDA concreta: es el "poner el elemento
// donde quiero" de TONE3000. pos < 0 o fuera de rango = al final.
static int namChainAddAt(const WCHAR* path, int pos, std::wstring& msg)
{
    // La fila del frontal solo dibuja y solo recibe clics para namBlkVis()
    // celdas. Dejar crecer la cadena mas alla metia capturas que SUENAN (y
    // cuestan CPU) sin que el usuario pudiera verlas ni apagarlas, asi que se
    // corta aqui y se dice por que.
    {
        std::lock_guard<std::mutex> lk(g_namMutex);
        if ((int)g_namUi.size() >= namBlkVis()) {
            msg = L"LAS CELDAS ESTAN LLENAS: QUITA UNA PARA PONER OTRA";
            return -1;
        }
    }
    // La ruta va en WCHAR de punta a punta: convertirla a UTF-8 y pasarla a
    // fopen NO funcionaba en Windows (fopen la lee con la pagina de codigos
    // local). Una captura en "C:\...\MÃºsica\..." o "Escritorio\AmpliaciÃ³n\"
    // decia "no se puede abrir el fichero" con el fichero ahi al lado.
    auto m = std::make_shared<nam::Model>();
    if (!m->loadW(path)) {
        msg = L"no se pudo cargar: " + wstr(m->err);
        return -1;
    }
    m->reset();
    for (int i = 0; i < m->prewarmSamples; ++i) m->tick(0.0f);
    auto sl = std::make_shared<NamSlot>();
    sl->model = m;
    sl->path = path;
    {
        std::wstring base(path);
        const size_t sl2 = base.find_last_of(L"\\/");
        if (sl2 != std::wstring::npos) base = base.substr(sl2 + 1);
        if (base.size() > 4 && base.compare(base.size() - 4, 4, L".nam") == 0) base.resize(base.size() - 4);
        sl->name = base.empty() ? wstr(m->name) : base;
    }
    namComputeTrims(*m, *sl);
    int at = 0;
    {
        std::lock_guard<std::mutex> lk(g_namMutex);
        if (pos < 0 || pos > (int)g_namUi.size()) pos = (int)g_namUi.size();
        g_namUi.insert(g_namUi.begin() + pos, sl);
        g_namSel = pos;
        at = pos;
    }
    namPublish();
    msg = L"BLOQUE PUESTO EN LA CELDA";
    return at;
}

// El candado se suelta ANTES de publicar: namPublish vuelve a pedirlo (el
// mutex no es recursivo) y bloquearlo dos veces dejaba la interfaz colgada.
static void namChainRemove(int idx)
{
    {
        std::lock_guard<std::mutex> lk(g_namMutex);
        if (idx >= 0 && idx < (int)g_namUi.size()) g_namUi.erase(g_namUi.begin() + idx);
    }
    namPublish();
}
static void namChainMove(int idx, int dir)
{
    {
        std::lock_guard<std::mutex> lk(g_namMutex);
        const int j = idx + dir;
        if (idx < 0 || j < 0 || j >= (int)g_namUi.size()) return;
        std::swap(g_namUi[idx], g_namUi[j]);
        g_namSel = j;
    }
    namPublish();
}
static void namChainClear() { { std::lock_guard<std::mutex> lk(g_namMutex); g_namUi.clear(); g_namSel = -1; } namPublish(); }

static G::Image* g_fondoImg[13] = {};
static G::Image* g_stripImg[13]  = {};   // solo el amp activo queda en RAM
static bool g_artOk[13] = {};
static WCHAR g_artDir[MAX_PATH] = L"";
// ---------------------------------------------------------------------------
//  Explorador de perfiles TONE3000
// ---------------------------------------------------------------------------
//  Los 7 tipos de equipo de su interfaz (los chips de la web), con el valor
//  exacto que espera la API. Es el mismo filtro que ves en tone3000.com.
struct T3kGearDef { const WCHAR* label; const char* gear; };
static const T3kGearDef kT3kGears[7] = {
    { L"Amp + Cab",    "amp-cab" },
    { L"Amp Head",     "amp" },
    { L"Cabinet",      "cab" },
    { L"Pedal",        "pedal" },
    { L"Outboard",     "outboard" },
    { L"Spaces",       "space" },
    { L"Experimental", "experimental" },
};
struct T3kItem {
    long long    id = 0;
    std::wstring title, gear, user;
    std::string  image;                                  // primera foto del tono
    int          models = 0, a1 = 0, a2 = 0, downloads = 0;
    long long    ageDays = -1;                           // antiguedad (como la web)
    bool         verified = false;
};
static bool                 g_t3kOpen = false;
static int                  g_t3kGear = -1;              // -1 = todos
static WCHAR                g_t3kQuery[64] = L"";
static WCHAR                g_t3kKey[200] = L"";        // clave publicable o token pegado
static int                  g_t3kFocus = 0;              // 1 = busqueda, 2 = conexion
static bool                 g_t3kConnOpen = false;       // hoja de conexion visible
static bool                 g_t3kLocalMode = false;      // la rejilla muestra la biblioteca en disco
// g_t3kPageAll guarda la ultima lista recibida TAL CUAL; g_t3kItems es lo que
// se pinta despues de aplicar los chips. Asi los filtros son locales y no
// dependen de volver a pedir la pagina (que sin clave no se puede).
static std::vector<T3kItem> g_t3kItems, g_t3kPageAll;
static bool                 g_t3kVerif = false;          // solo autores verificados
// Clave PUBLICABLE de TONE3000 (es el client_id de los flujos OAuth: su
// documentacion dice que va en el cliente, nunca la secreta). Con ella el
// boton CONECTAR pide el codigo sin que haya que pegar nada.
static const char*          kT3kPubKey = "t3k_pub_wOZz7T1_CWFGt9INgGRv6vesTHIepQNn";
static int                  g_t3kSel = -1, g_t3kPage = 1;
static long long            g_t3kTotal = 0;
static std::wstring         g_t3kStatus, g_t3kHint;
static bool                 g_t3kBusy = false;
static bool                 g_t3kConnected = false;
static bool                 g_t3kIsTrend = true;         // la lista actual es la publica
static std::string          g_t3kToken, g_t3kRefresh, g_t3kUser, g_t3kPending;
static std::string          g_t3kClientId;   // cliente que obtuvo la sesion en curso
static std::string          g_t3kUserCode, g_t3kVerifyUri;
static int                  g_t3kExpMin = 0;             // lo que declara el servidor
static bool                 g_t3kFromInstalled = false;  // credencial prestada por la app instalada
static bool                 g_t3kConsentInstalled = false; // el usuario ya vio y acepto el aviso de TONE3000 instalado
static std::mutex           g_t3kMutex;
// Cola de trabajos terminados. Un solo hueco perdia resultados: si una descarga
// terminaba mientras habia una busqueda en curso, la descarga se descartaba y
// g_t3kBusy se quedaba en true para siempre (el panel decia "CONSULTANDO..." y
// no volvia nunca). Ahora cada WM_T3K_DONE saca un trabajo y se aplica.
struct T3kJob { std::string kind, body, err; int status; };
static std::vector<T3kJob>  g_t3kQueue;
static std::wstring         g_t3kSavedPath, g_t3kSavedTitle;
static std::atomic<bool>    g_t3kImgRun{ false };        // hay un hilo bajando fotos
static const UINT           WM_T3K_DONE = WM_APP + 7;    // aviso de los hilos a la UI
static void saveIni();        // definidas mas abajo (persistencia y avisos)
static int  t3kCardMax();     // tarjetas que caben en el panel (geometria del explorador)
static int  t3kPageSize();    // y cuantas pide por pagina la busqueda
static void toast(const WCHAR* msg);
static bool copyToClipboard(const WCHAR* txt);   // definida mas abajo (hoja de conexion)

static WCHAR g_t3kProfilesDir[MAX_PATH] = L"";   // perfiles descargados (junto al exe)
static int   g_t3kLibCount = 0;                  // .nam en la carpeta (para la pantalla)
static HWND  g_hwnd = nullptr;                   // ventana principal (avisos desde los hilos)
static void t3kProfilesDirInit() {
    if (g_t3kProfilesDir[0]) return;
    WCHAR mod[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    WCHAR* sl = wcsrchr(mod, L'\\');
    if (sl) *sl = 0;
    swprintf_s(g_t3kProfilesDir, L"%s\\perfiles", mod);
    {   // Cuantos .nam hay en la libreria: la pantalla del PERFIL lo dice en vacio.
        // Se cuenta una vez al arrancar: la descarga avisa por toast, no por repintado.
        WIN32_FIND_DATAW fd{};
        WCHAR pat[MAX_PATH];
        swprintf_s(pat, L"%s\\*.nam", g_t3kProfilesDir);
        int n = 0;
        HANDLE hf = FindFirstFileW(pat, &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n; }
            while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
        g_t3kLibCount = n;
    }
}

// ===========================================================================
//  Explorador TONE3000: motor (red en hilos aparte, UI siempre viva)
// ---------------------------------------------------------------------------
//  Regla del proyecto: nada de red en el hilo de la interfaz. Cada peticion va
//  en su hilo, deja el resultado bajo candado y avisa con WM_T3K_DONE; la UI
//  lo recoge en t3kApplyResult(). Cargar fuera del hilo y publicar de golpe es
//  el mismo reparto que ya usa el IR de cabina y la cadena de capturas.
// ===========================================================================
static std::wstring u8w(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring o((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n);
    return o;
}
static std::string w8u(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}
static std::wstring t3kGearLabel(const std::string& g) {
    for (int i = 0; i < 7; ++i) if (g == kT3kGears[i].gear) return kT3kGears[i].label;
    return g.empty() ? std::wstring(L"-") : u8w(g);
}
// Antiguedad en el estilo de la web: hoy, 3 d, 1 sem, 2 mes, 1 a
static std::wstring t3kAge(long long days) {
    WCHAR b[32];
    if (days < 0) return std::wstring();
    if (days <= 0)       wcscpy_s(b, tr(L"hoy"));
    else if (days < 7)   swprintf_s(b, L"%lld d", days);
    else if (days < 30)  swprintf_s(b, tr(L"%lld sem"), days/7);
    else if (days < 365) swprintf_s(b, tr(L"%lld mes"), days/30);
    else                 swprintf_s(b, tr(L"%lld a"), days/365);
    return b;
}
// 100643 -> "100,6K" (como lo escribe la web)
static std::wstring t3kCount(long long v) {
    WCHAR b[32];
    if (v >= 1000000) swprintf_s(b, L"%.1fM", (double)v/1000000.0);
    else if (v >= 1000) swprintf_s(b, L"%.1fK", (double)v/1000.0);
    else swprintf_s(b, L"%lld", v);
    for (WCHAR* p = b; *p; ++p) if (*p == L'.') *p = L',';
    return b;
}
static std::wstring t3kSafeName(const std::wstring& in) {
    std::wstring o;
    for (WCHAR c : in) {
        if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')
            || c == L'-' || c == L'_' || c == L' ' || (c >= 0x00C0 && c <= 0x017F)) o += c;
        else o += L'_';
    }
    while (!o.empty() && (o.back() == L'_' || o.back() == L' ' || o.back() == L'.')) o.pop_back();
    while (!o.empty() && o.front() == L' ') o.erase(o.begin());
    if (o.size() > 60) o.resize(60);
    if (o.empty()) o = L"perfil";
    return o;
}
// Fotos de los tonos: se cachean en disco (perfiles\_img) y se decodifican una vez
static std::wstring t3kImgFile(long long id) {
    t3kProfilesDirInit();
    const std::wstring dir = std::wstring(g_t3kProfilesDir) + L"\\_img";
    CreateDirectoryW(dir.c_str(), nullptr);
    WCHAR b[MAX_PATH];
    swprintf_s(b, L"%s\\%lld.img", dir.c_str(), id);
    return b;
}
static std::vector<std::pair<long long, G::Bitmap*>> g_t3kImgCache;
static G::Bitmap* t3kImgGet(long long id) {
    // Un fallo cacheado solo vale mientras el hilo sigue bajando fotos: si ya
    // termino, el fichero esta en disco y hay que reintentarlo. Antes el fallo se
    // quedaba para SIEMPRE y en una instalacion nueva no aparecia ni una foto en
    // toda la sesion (solo el hueco con "T3K").
    const bool downloading = g_t3kImgRun.load();
    size_t slot = (size_t)-1;
    for (size_t i = 0; i < g_t3kImgCache.size(); ++i) {
        if (g_t3kImgCache[i].first != id) continue;
        if (g_t3kImgCache[i].second || downloading) return g_t3kImgCache[i].second;
        slot = i;
        break;
    }
    G::Bitmap* b = nullptr;
    const std::wstring f = t3kImgFile(id);
    if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) {
        b = G::Bitmap::FromFile(f.c_str());
        if (b && (b->GetLastStatus() != G::Ok || b->GetWidth() < 16 || b->GetHeight() < 16)) {
            delete b; b = nullptr;
        }
    }
    if (b || !downloading) {
        if (slot != (size_t)-1) g_t3kImgCache[slot].second = b;
        else                    g_t3kImgCache.emplace_back(id, b);
    }
    return b;
}
static void t3kImgFree() {
    for (auto& kv : g_t3kImgCache) delete kv.second;
    g_t3kImgCache.clear();
}
static void t3kPostDone() { if (g_hwnd) PostMessageW(g_hwnd, WM_T3K_DONE, 0, 0); }
// --- Token de TONE3000: en disco va CIFRADO (DPAPI, por usuario y maquina) ---
// El .ini es texto y vive junto al .exe: un token en claro ahi deja tu sesion de
// TONE3000 al alcance de cualquiera que copie la carpeta. El prefijo "enc:" marca
// lo cifrado; un valor sin prefijo es de una version anterior y se acepta tal cual
// (y se vuelve a guardar ya cifrado en el siguiente guardado).
static std::string t3kProtect(const std::string& plain) {
    if (plain.empty()) return std::string();
    DATA_BLOB in{ (DWORD)plain.size(), (BYTE*)plain.data() };
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"GUITAR ONE TONE3000", nullptr, nullptr, nullptr, 0, &out))
        return std::string();
    std::string s;
    DWORD n = 0;
    if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n)) {
        std::vector<char> b(n + 1, 0);
        if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                 b.data(), &n))
            s.assign(b.data(), n);
    }
    LocalFree(out.pbData);
    return s.empty() ? std::string() : ("enc:" + s);
}
static std::string t3kUnprotect(const std::string& stored) {
    if (stored.empty()) return std::string();
    if (stored.rfind("enc:", 0) != 0) return stored;        // texto plano heredado
    const std::string s = stored.substr(4);
    DWORD n = 0;
    if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64,
                              nullptr, &n, nullptr, nullptr)) return std::string();
    std::vector<BYTE> buf(n, 0);
    if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64,
                              buf.data(), &n, nullptr, nullptr)) return std::string();
    DATA_BLOB in{ n, buf.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return std::string();
    std::string r((const char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
}

static void t3kStoreResult(const std::string& kind, const std::string& json, int status, const std::string& err) {
    std::lock_guard<std::mutex> lk(g_t3kMutex);
    // Cola acotada: si algo se atasca, no crece sin fin (se tira lo mas viejo).
    if (g_t3kQueue.size() > 16) g_t3kQueue.erase(g_t3kQueue.begin());
    g_t3kQueue.push_back(T3kJob{ kind, json, err, status });
}
static std::string t3kTokenSnap() {
    std::lock_guard<std::mutex> lk(g_t3kMutex);
    return g_t3kToken;
}
static void t3kStatusW(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(g_t3kMutex);
    g_t3kStatus = s;
}

// --- Lista (tendencias publicas o busqueda con token) ----------------------
// ---------------------------------------------------------------------------
//  Renovacion silenciosa de la sesion
// ---------------------------------------------------------------------------
//  El access_token dura ~1 hora. La app guardaba el refresh_token (cifrado, en el
//  .ini) y NUNCA lo usaba: al caducar, busquedas y descargas empezaban a fallar y
//  habia que reconectar a mano. Esto lo renueva con NUESTRA credencial (el cliente
//  publicable que la app lleva dentro), sin tocar nada de la app instalada.
static std::atomic<bool> g_t3kDirtySave{ false };   // el hilo de UI guardara el .ini
static bool t3kRenewSession(std::string& tokOut) {
    std::string rf, cid;
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        rf = g_t3kRefresh;
        cid = g_t3kClientId;              // el cliente que obtuvo ESTA sesion
    }
    if (rf.empty()) return false;
    if (cid.empty()) cid = kT3kPubKey;    // sesion nuestra: la clave publicable
    t3k::Resp r; std::string err;
    if (!t3k::refreshSession(cid, rf, r, err)) return false;
    t3k::Jv j;
    if (!t3k::jParse(r.body, j)) return false;
    const std::string at = j.str("access_token");
    if (at.empty()) return false;
    const std::string rt2 = j.str("refresh_token");
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        g_t3kToken = at;
        if (!rt2.empty()) g_t3kRefresh = rt2;
        tokOut = at;
    }
    g_t3kDirtySave.store(true);      // el .ini lo guarda la UI (un solo hilo escribe)
    return true;
}
static void t3kWorkGet(const std::string& url, const std::string& kind, std::string token) {
    t3k::Resp r; std::string err;
    bool sent = t3k::httpRequest(L"GET", url, "", token, r, &err);
    // Caducado: se renueva y se repite UNA vez. Sin esto, a la hora de conectado
    // todo empezaba a dar 401 y parecia que la app se habia roto.
    if (sent && r.status == 401 && t3kRenewSession(token))
        sent = t3k::httpRequest(L"GET", url, "", token, r, &err);
    t3kStoreResult(kind, sent ? r.body : std::string(), sent ? r.status : 0, err);
    t3kPostDone();
}
static void t3kStartTrend() {
    t3kStatusW(L"CARGANDO TENDENCIAS...");
    std::thread(t3kWorkGet, std::string("https://www.tone3000.com/api/v1/tones/trending"),
                std::string("trend"), std::string()).detach();
}
// Listas de la capa gratuita de TONE3000 (su documentacion las permite todas):
// tendencias y nuevos son publicas; favoritos y descargados son TUS cosas y piden
// token. Son las mismas pestanyas que su web.
static const char*  kT3kLists[4]      = { "trending", "latest", "favorited", "downloaded" };
static const WCHAR* kT3kListNames[4]  = { L"TENDENCIAS", L"NUEVOS", L"FAVORITOS", L"DESCARGADOS" };
static int          g_t3kList = 0;
static void t3kStartList(int which) {
    if (which < 0 || which > 3) which = 0;
    g_t3kList  = which;
    const bool needsTok = (which >= 2);
    const std::string tok = needsTok ? t3kTokenSnap() : std::string();
    if (needsTok && tok.empty()) {
        t3kStatusW(L"ESA LISTA NECESITA TU TOKEN DE TONE3000");
        g_t3kConnOpen = true;              // abre la hoja de conexion
        g_t3kBusy = false;
        return;
    }
    g_t3kBusy = true;
    g_t3kPage = 1;
    t3kStatusW(std::wstring(tr(L"CARGANDO ")) + tr(kT3kListNames[which]) + L"...");
    std::thread(t3kWorkGet,
                std::string("https://www.tone3000.com/api/v1/tones/") + kT3kLists[which],
                std::string("trend"), tok).detach();
}
static void t3kStartSearch() {
    std::string tok = t3kTokenSnap();
    if (tok.empty()) {
        t3kStatusW(L"LA BUSQUEDA NECESITA CONECTAR CON TONE3000");
        return;
    }
    std::string q = w8u(g_t3kQuery);
    std::string gears = (g_t3kGear >= 0) ? kT3kGears[g_t3kGear].gear : std::string();
    std::string url = t3k::searchUrl(q, gears, "format=nam", g_t3kPage, t3kPageSize());
    t3kStatusW(L"BUSCANDO...");
    std::thread(t3kWorkGet, url, std::string("search"), tok).detach();
}

// --- Conexion: flujo de dispositivo (RFC 8628) -----------------------------
static void t3kWorkConnect(std::string key) {
    t3k::Device d; std::string err;
    if (!t3k::deviceStart(key, d, err)) { t3kStoreResult("connect", "", 0, err); t3kPostDone(); return; }
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        g_t3kUserCode = d.userCode;
        g_t3kVerifyUri = d.verifyUriComplete.empty() ? d.verifyUri : d.verifyUriComplete;
    }
    g_t3kExpMin = (d.expiresIn > 0 ? d.expiresIn : 600) / 60;
    // Abrir la pagina es justo lo que toca ahora, y el usuario acaba de pedir la
    // conexion pulsando CONECTAR: se abre sola, con el codigo ya metido, en vez de
    // dejarle copiar una URL. El boton ABRIR sigue ahi por si la cierra.
    if (!d.verifyUriComplete.empty()) t3k::openBrowser(d.verifyUriComplete);
    else if (!d.verifyUri.empty())   t3k::openBrowser(d.verifyUri);
    t3kStoreResult("device", "", 200, "");
    t3kPostDone();
    const DWORD t0 = GetTickCount();
    const DWORD life = (DWORD)((d.expiresIn > 0 ? d.expiresIn : 600) * 1000);
    const int step = (d.interval > 0 ? d.interval : 5);
    while (GetTickCount() - t0 < life) {
        Sleep((DWORD)step * 1000);
        t3k::Resp r;
        const int st = t3k::devicePoll(key, d.deviceCode, r, err);
        if (st == 1) { t3kStoreResult("token", r.body, 200, ""); t3kPostDone(); return; }
        if (st < 0) { t3kStoreResult("connect", "", 0, err); t3kPostDone(); return; }
    }
    t3kStoreResult("connect", "", 0, "el codigo ha caducado");
    t3kPostDone();
}
static void t3kWorkTokenCheck(std::string tok);   // definida mas abajo
// ---------------------------------------------------------------------------
//  Biblioteca LOCAL: los .t3kpreset que el usuario ya tiene en su TONE3000
// ---------------------------------------------------------------------------
//  Se leen del DISCO (sin sesion y sin red): nombre del preset, si es estereo y
//  sus bloques con la URL de descarga de cada captura. Solo CARGAR un preset
//  necesitara descargar sus modelos.
static std::vector<t3k::LocalPreset> g_t3kLocal;
static std::wstring t3kLocalPresetsDir() {
    WCHAR ad[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH)) return std::wstring();
    return std::wstring(ad) + L"\\TONE3000\\Presets";
}
static bool t3kReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[16384]; DWORD rd = 0;
    while (ReadFile(hf, buf, sizeof buf, &rd, nullptr) && rd > 0) out.append(buf, rd);
    CloseHandle(hf);
    return !out.empty();
}
// De un ChainBlock saca lo unico que la UI necesita: el titulo del tono, el
// modelo activo y su URL. El titulo y los modelos viven en el `toneJson` del
// bloque; el modelo activo se elige por su id, como hace su propia herramienta.
static void t3kBlockFromTree(const t3k::PresetTree& blk, long long activeId, t3k::PresetBlock& out) {
    const std::string tj = blk.prop("toneJson");
    if (tj.empty()) return;
    t3k::Jv j;
    if (!t3k::jParse(tj, j)) return;
    out.name = j.str("title");
    if (out.name.empty()) out.name = j.str("name");
    if (const t3k::Jv* ms = j.get("models")) {
        for (const t3k::Jv& m : ms->a) {
            if ((long long)m.num("id", -1.0) != activeId) continue;
            out.model = m.str("name");
            out.url   = m.str("model_url");
            break;
        }
        if (out.url.empty() && !ms->a.empty()) {      // sin id valido: el primero
            out.model = ms->a[0].str("name");
            out.url   = ms->a[0].str("model_url");
        }
    }
    if (out.url.empty()) return;
    const std::string t = blk.prop("type");
    out.nam = (t.find("ir") == std::string::npos);
}
// La biblioteca se parsea UNA vez y se cachea: una cuenta normal tiene cientos
// de .t3kpreset (290 ficheros / 7 MB en la de referencia) y leerlos enteros en
// cada apertura del navegador congelaba la interfaz. Se vuelve a leer solo si la
// carpeta cambio de verdad -un fichero mas o menos, o una fecha distinta-, que es
// justo lo que pasa al guardar o borrar un preset en TONE3000.
struct T3kLibStamp {
    unsigned long long n = (unsigned long long)-1, sz = 0, t = 0;
    bool operator==(const T3kLibStamp& o) const { return n == o.n && sz == o.sz && t == o.t; }
};
static T3kLibStamp g_t3kLibStamp;      // la que se parseo la ultima vez
static bool         g_t3kLibLoaded = false;
static T3kLibStamp t3kLocalStampOf() {
    T3kLibStamp s;
    const std::wstring dir = t3kLocalPresetsDir();
    if (dir.empty()) return s;
    WIN32_FIND_DATAW fd{};
    HANDLE hf = FindFirstFileW((dir + L"\\*.t3kpreset").c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return s;
    s.n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ++s.n;
        s.sz += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        const unsigned long long ft = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32)
                                    | fd.ftLastWriteTime.dwLowDateTime;
        if (ft > s.t) s.t = ft;
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
    return s;
}
static void t3kLoadLocalPresets(bool force = false) {
    const T3kLibStamp st = t3kLocalStampOf();
    if (!force && g_t3kLibLoaded && st == g_t3kLibStamp) return;   // la carpeta no cambio
    g_t3kLibStamp = st;
    g_t3kLibLoaded = true;
    g_t3kLocal.clear();
    const std::wstring dir = t3kLocalPresetsDir();
    const std::wstring pat = dir + L"\\*.t3kpreset";
    WIN32_FIND_DATAW fd{};
    HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        t3k::LocalPreset lp;
        lp.file = w8u(std::wstring(fd.cFileName));
        std::string bytes;
        if (!t3kReadWholeFile(dir + L"\\" + fd.cFileName, bytes)) { lp.err = "no se pudo leer"; g_t3kLocal.push_back(lp); continue; }
        if (bytes.size() < 8 || bytes.compare(0, 4, "T3KB") != 0) { lp.err = "no es un .t3kpreset"; g_t3kLocal.push_back(lp); continue; }
        t3k::PresetReader rd(bytes.substr(4));
        const std::vector<std::string> want = { "name", "stereoEnabled", "toneJson", "activeModelId", "type" };
        t3k::PresetTree root;
        if (!rd.readTree(root, want)) { lp.err = rd.error(); g_t3kLocal.push_back(lp); continue; }
        lp.name = root.prop("name");
        const t3k::PresetTree* snap = root.child("ChainSnapshot");
        if (!snap) { lp.err = "sin ChainSnapshot"; g_t3kLocal.push_back(lp); continue; }
        lp.stereo = !snap->prop("stereoEnabled").empty() && snap->prop("stereoEnabled")[0] != 0;
        for (const t3k::PresetTree& lane : snap->kids) {
            std::vector<t3k::PresetBlock>* dst = nullptr;
            if (lane.type == "ChainBlocks")      dst = &lp.left;
            else if (lane.type == "RightChainBlocks") dst = &lp.right;
            if (!dst) continue;
            for (const t3k::PresetTree& blk : lane.kids) {
                if (blk.type != "ChainBlock") continue;
                t3k::PresetBlock pb;
                const std::string raw = blk.prop("activeModelId");
                t3kBlockFromTree(blk, raw.empty() ? -1 : rd.asInt(raw), pb);
                if (!pb.url.empty()) dst->push_back(pb);
            }
        }
        lp.ok = !lp.left.empty() || !lp.right.empty();
        if (!lp.ok && lp.err.empty()) lp.err = "sin bloques con modelo";
        g_t3kLocal.push_back(lp);
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}
// Lleva la biblioteca LOCAL a la rejilla: una tarjeta por preset con su nombre,
// sus bloques y si es estereo. Sin sesion y sin red (solo disco). El pie de la
// tarjeta no aplica aqui (no hay descargas), asi que el dato util son los bloques.
static void t3kShowLocalPresets() {
    t3kLoadLocalPresets();
    g_t3kLocalMode = true;
    g_t3kItems.clear();
    g_t3kPageAll.clear();
    for (size_t i = 0; i < g_t3kLocal.size(); ++i) {
        const t3k::LocalPreset& p = g_t3kLocal[i];
        if (!p.ok) continue;
        T3kItem it;
        it.id        = (long long)i;                  // indice local (no hay id de API)
        it.title     = u8w(p.name);
        it.gear      = L"Preset";
        it.user      = L"en tu TONE3000";
        it.models    = (int)(p.left.size() + p.right.size());
        it.downloads = 0;
        it.verified  = false;
        g_t3kItems.push_back(it);
    }
    g_t3kTotal = (long long)g_t3kItems.size();
    g_t3kPage  = 1;
    g_t3kSel   = -1;
    WCHAR m[96];
    swprintf_s(m, L"TU BIBLIOTECA: %d PRESETS EN DISCO (SIN CONEXION)", (int)g_t3kItems.size());
    t3kStatusW(m);
}
// ---------------------------------------------------------------------------
//  Sesion del TONE3000 INSTALADO (para descargar: la lectura local no la usa)
// ---------------------------------------------------------------------------
//  Su app deja el access_token EN CLARO en %APPDATA%\TONE3000\auth.json (medido
//  en esta maquina: un JWT de 987 caracteres, con client_id y un expires_at EN
//  SEGUNDOS). Se lee para conectar sin pedir clave ni pasar por el navegador:
//  es la sesion que el usuario YA tiene concedida. Es una credencial prestada y
//  corta (su app la renueva sola; la leida aqui caducaba en ~1 minuto), asi que
//  NO se guarda en el .ini: se relee en cada conexion.
// Devuelve la sesion del TONE3000 instalado, RENOVANDOLA si esta caducada.
// ---------------------------------------------------------------------------
// Su app guarda en %APPDATA%\TONE3000\auth.json: access_token, refresh_token,
// client_id y expires_at (en SEGUNDOS). El refresco hay que pedirlo con SU
// client_id, no con nuestra clave publicable: con la nuestra el servidor responde
// invalid_client y todo el camino se caia al navegador (eso era el "me pide
// token"). Medido aqui: POST /oauth/token con su par -> HTTP 200 + token nuevo, y
// con el, /tones/favorited y /tones/search -> 200.
// El refresco ROTA el refresh_token, asi que en principio habria que devolver el
// nuevo a SU fichero... pero medido aqui, la rotacion NO invalida el anterior (un
// segundo refresco con el mismo token del fichero -> HTTP 200), asi que NO se
// escribe nada suyo: se lee y ya. Menos invasivo y no rompe su app.
static std::string t3kInstalledToken(bool& caducado) {
    caducado = false;
    WCHAR ad[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH)) return std::string();
    const std::wstring p = std::wstring(ad) + L"\\TONE3000\\auth.json";
    HANDLE hf = CreateFileW(p.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return std::string();
    std::string txt;
    char b4[4096]; DWORD rd = 0;
    while (ReadFile(hf, b4, sizeof b4, &rd, nullptr) && rd > 0) txt.append(b4, rd);
    CloseHandle(hf);
    if (txt.empty()) return std::string();
    t3k::Jv j;
    if (!t3k::jParse(txt, j)) return std::string();
    const std::string at  = j.str("access_token");
    const std::string rt  = j.str("refresh_token");
    const std::string cid = j.str("client_id");
    const double exp = j.num("expires_at", 0.0);
    const bool hayQueRenovar = at.empty() || (exp > 0.0 && exp <= (double)time(nullptr) + 60.0);
    if (hayQueRenovar) {
        if (rt.empty() || cid.empty()) { caducado = at.empty(); return at; }
        t3k::Resp r; std::string err;
        if (!t3k::refreshSession(cid, rt, r, err)) { caducado = at.empty(); return at; }
        t3k::Jv rj;
        if (!t3k::jParse(r.body, rj)) { caducado = at.empty(); return at; }
        const std::string nat = rj.str("access_token");
        if (nat.empty()) { caducado = at.empty(); return at; }
        const std::string nrt = rj.str("refresh_token");
        const double expIn = rj.num("expires_in", 3600.0);
        (void)expIn;                              // el plazo lo dicta el servidor
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        g_t3kClientId = cid;                       // la renovacion posterior es SUYA
        if (!nrt.empty()) g_t3kRefresh = nrt;
        return nat;
    }
    if (!rt.empty() && !cid.empty()) {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        g_t3kClientId = cid;
        g_t3kRefresh  = rt;
    }
    return at;
}
// El camino comodo, entero y en el hilo de trabajo (la UI no se bloquea):
//   1) leer la sesion que el usuario YA tiene concedida en su TONE3000 y, si esta
//      caducada, RENOVARLA con su propio client_id (ver t3kInstalledToken);
//   2) si no hay sesion que renovar, el flujo de dispositivo con su codigo.
// NO se abre su app: se comprobo que ella no reescribe el token al arrancar (13 h
// con el mismo), asi que abrirla solo molestaba: le abria una ventana al usuario.
static void t3kWorkInstalledFirst() {
    bool cad = false;
    const std::string tok = t3kInstalledToken(cad);   // ya renueva si hace falta
    if (!tok.empty()) {
        {
            std::lock_guard<std::mutex> lk(g_t3kMutex);
            g_t3kPending = tok;
            g_t3kFromInstalled = true;
        }
        t3kStatusW(L"CONECTANDO CON TU TONE3000 INSTALADO...");
        t3kWorkTokenCheck(tok);
        return;
    }
    t3kStatusW(L"TE DOY UN CODIGO PARA CONFIRMAR EN TONE3000.COM/ACTIVATE");
    t3kWorkConnect(std::string(kT3kPubKey));
}
// Pide la conexion con lo que haya en el campo. Lo comparten el boton CONECTAR y
// la tecla Enter del campo: antes Enter solo quitaba el foco y habia que ir al
// boton con el raton.
//
// Campo vacio = cero pasos para el usuario: la app se busca la sesion sola (ver
// t3kWorkInstalledFirst). Pegando algo, se prueba como token/clave.
static void t3kDoConnect(std::string ks) {
    if (ks.empty()) {
        // La app va a leer el auth.json del TONE3000 instalado para reusar
        // la sesion que el usuario ya tiene concedida. Eso es leer una
        // credencial de otra aplicacion, asi que se pide consentimiento la
        // primera vez que se hace (y se recuerda mientras corre la app).
        if (!g_t3kConsentInstalled) {
            const int r = MessageBoxW(g_hwnd,
                L"Voy a leer la sesion de tu TONE3000 instalado\n"
                L"(%APPDATA%\\TONE3000\\auth.json, token corta) para conectarme\n"
                L"sin pedirte codigo. Esa lectura no la guardo en disco.\n\n"
                L"Pulsa Si para permitirlo, o No para ir al flujo de codigo.",
                L"GUITAR ONE - CONSENTIMIENTO", MB_YESNO | MB_ICONQUESTION);
            if (r != IDYES) {
                t3kStatusW(L"CONECTANDO CON CODIGO: SIGUE LOS PASOS EN TONE3000.COM");
                g_t3kExpMin = 0;
                std::thread(t3kWorkConnect,
                            std::string(kT3kPubKey)).detach();
                return;
            }
            g_t3kConsentInstalled = true;
        }
        g_t3kExpMin = 0;
        t3kStatusW(L"BUSCANDO TU SESION DE TONE3000...");
        std::thread(t3kWorkInstalledFirst).detach();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        g_t3kFromInstalled = false;
        g_t3kUserCode.clear(); g_t3kVerifyUri.clear();
        g_t3kPending = ks;
    }
    g_t3kExpMin = 0;
    t3kStatusW(L"HABLANDO CON TONE3000...");
    std::thread(t3kWorkTokenCheck, ks).detach();
}

// Lo que el usuario pega puede ser un token de sesion o la clave publicable. Se
// prueba primero como token contra una lista acotada (capa gratuita) y, si el
// servidor lo rechaza, se sigue como clave con el flujo de dispositivo: una sola
// entrada para las dos cosas, sin adivinar por el formato.
static void t3kWorkTokenCheck(std::string tok) {
    t3k::Resp r; std::string err;
    const bool sent = t3k::httpRequest(L"GET", "https://www.tone3000.com/api/v1/tones/favorited",
                                       "", tok, r, &err);
    if (sent && r.ok()) {
        t3kStoreResult("tokencheck", "ok", r.status, "");
        t3kPostDone();
        return;
    }
    if (!sent) {                       // sin red: no se prueba como clave
        t3kStoreResult("tokencheck", "", 0, err);
        t3kPostDone();
        return;
    }
    // Rechazado. Si la credencial era PRESTADA por la app instalada, no se prueba
    // como clave (no lo es): se sigue con el flujo de dispositivo y la clave
    // publicable, que es la unica que puede pedir un codigo.
    bool prestada = false;
    { std::lock_guard<std::mutex> lk(g_t3kMutex); prestada = g_t3kFromInstalled; }
    if (prestada) { t3kWorkConnect(std::string(kT3kPubKey)); return; }
    t3kWorkConnect(tok);               // rechazado como token -> flujo de dispositivo
}

// --- Descarga de un tono: /models -> model_url -> perfiles\*.nam -----------
static void t3kWorkGrab(long long id, std::wstring title, std::string token) {
    std::string err;
    t3k::Resp r;
    const std::string u = "https://www.tone3000.com/api/v1/models?tone_id=" + std::to_string(id)
                        + "&page_size=100";
    bool sentG = t3k::httpRequest(L"GET", u, "", token, r, &err);
    if (sentG && r.status == 401 && t3kRenewSession(token))
        sentG = t3k::httpRequest(L"GET", u, "", token, r, &err);
    if (!sentG) {
        t3kStoreResult("grab", "", 0, err); t3kPostDone(); return;
    }
    if (!r.ok()) {
        t3kStoreResult("grab", "", r.status, "el servidor devolvio " + std::to_string(r.status));
        t3kPostDone(); return;
    }
    std::vector<std::pair<std::string, std::string>> found;   // (url, arquitectura)
    t3k::Jv j;
    const t3k::Jv* arr = nullptr;
    if (t3k::jParse(r.body, j)) {
        if (j.t == t3k::Jv::ARR) arr = &j;
        else if (j.get("data")) arr = j.get("data");
    }
    static const char* kUrlKeys[5] = { "model_url", "url", "download_url", "file_url", "path" };
    for (size_t i = 0; arr && i < arr->size(); ++i) {
        const t3k::Jv* o = arr->at(i);
        if (!o) continue;
        std::string mu, arch;
        for (const char* k : kUrlKeys) {
            const t3k::Jv* v = o->get(k);
            if (v && v->t == t3k::Jv::STR && !v->s.empty()) { mu = v->s; break; }
        }
        // La API llama architecture_version a la version de arquitectura del
        // modelo ("1"/"2"/"custom"). Se lee esa y, si no esta, architecture (que
        // es el campo de la BUSQUEDA, no del modelo): antes se leia el segundo y
        // la preferencia por A1 no se cumplia nunca.
        const t3k::Jv* a = o->get("architecture_version");
        if (!a) a = o->get("architecture");
        if (a) arch = (a->t == t3k::Jv::STR) ? a->s : std::to_string(a->i64());
        if (!mu.empty()) found.emplace_back(mu, arch);
    }
    if (found.empty()) {   // forma inesperada: escaneo literal del JSON
        static const char* kLits[3] = { "\"model_url\":\"", "\"download_url\":\"", "\"url\":\"" };
        for (const char* kk : kLits) {
            size_t p = 0;
            while ((p = r.body.find(kk, p)) != std::string::npos) {
                const size_t b = p + strlen(kk);
                const size_t e = r.body.find('"', b);
                if (e == std::string::npos) break;
                found.emplace_back(r.body.substr(b, e - b), std::string());
                p = e + 1;
            }
            if (!found.empty()) break;
        }
    }
    const std::string* url = nullptr;
    for (auto& m : found) if (m.second == "1" || m.second == "A1" || m.second == "a1") { url = &m.first; break; }
    if (!url && !found.empty()) url = &found[0].first;
    if (!url) { t3kStoreResult("grab", "", 200, "ese tono no trae ningun modelo .nam"); t3kPostDone(); return; }

    t3k::Resp d;
    if (!t3k::httpRequest(L"GET", *url, "", token, d, &err)) {
        t3kStoreResult("grab", "", 0, err); t3kPostDone(); return;
    }
    if (!d.ok()) {
        t3kStoreResult("grab", "", d.status, "la descarga devolvio " + std::to_string(d.status));
        t3kPostDone(); return;
    }
    t3kProfilesDirInit();
    CreateDirectoryW(g_t3kProfilesDir, nullptr);
    WCHAR path[MAX_PATH];
    swprintf_s(path, L"%s\\%s_%lld.nam", g_t3kProfilesDir, t3kSafeName(title).c_str(), id);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) {
        t3kStoreResult("grab", "", 0, "no se puede escribir en la carpeta perfiles");
        t3kPostDone(); return;
    }
    const size_t wr = fwrite(d.body.data(), 1, d.body.size(), f);
    fclose(f);
    if (wr != d.body.size()) {
        t3kStoreResult("grab", "", 0, "escritura incompleta"); t3kPostDone(); return;
    }
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        g_t3kSavedPath = path;
        g_t3kSavedTitle = title;
    }
    t3kStoreResult("grab", *url, 200, "");
    t3kPostDone();
}

// --- Carga de un preset LOCAL: baja sus capturas y monta la cadena ----------
//  Los bloques ya traen su URL de descarga (leida del .t3kpreset), asi que no hay
//  que resolverla: se baja cada uno a perfiles/ y se monta EN ORDEN. La cadena
//  tiene 4 celdas: si el preset trae mas bloques, se cargan los primeros y se
//  dice cuantos se quedan fuera. El resumen vuelve en el cuerpo del trabajo.
static std::string t3kUrlTail(const std::string& u) {
    const size_t q = u.find_first_of("?#");
    const std::string s = (q == std::string::npos) ? u : u.substr(0, q);
    const size_t b = s.find_last_of('/');
    return (b == std::string::npos) ? std::string() : s.substr(b + 1);
}
static void t3kWorkLoadPreset(int li, std::string token) {
    std::vector<t3k::PresetBlock> blocks;
    std::string pname;
    int derecha = 0;
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        if (li < 0 || li >= (int)g_t3kLocal.size()) {
            t3kStoreResult("preset", "preset no encontrado", 0, "");
            t3kPostDone(); return;
        }
        blocks  = g_t3kLocal[(size_t)li].left;
        pname   = g_t3kLocal[(size_t)li].name;
        derecha = (int)g_t3kLocal[(size_t)li].right.size();
    }
    if (blocks.empty()) {
        t3kStoreResult("preset", "ese preset no trae bloques en la via izquierda", 200, "");
        t3kPostDone(); return;
    }
    t3kProfilesDirInit();
    CreateDirectoryW(g_t3kProfilesDir, nullptr);
    int hechos = 0, fallos = 0, fuera = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        {
            std::lock_guard<std::mutex> lk(g_namMutex);
            if ((int)g_namUi.size() >= namBlkVis()) {   // celdas llenas
                fuera = (int)(blocks.size() - i);
                break;
            }
        }
        const std::string u = blocks[i].url;
        std::wstring base = u8w(t3kUrlTail(u));
        if (base.empty()) base = t3kSafeName(u8w(blocks[i].name));
        const std::wstring path = std::wstring(g_t3kProfilesDir) + L"\\" + base;
        // Si la captura YA esta en perfiles/ no se vuelve a bajar: recargar un
        // preset que ya cargaste no gasta red ni pide token.
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            t3k::Resp d; std::string err;
            bool sent = t3k::httpRequest(L"GET", u, "", token, d, &err);
            if (sent && d.status == 401 && t3kRenewSession(token))
                sent = t3k::httpRequest(L"GET", u, "", token, d, &err);
            if (!sent || !d.ok()) { ++fallos; continue; }
            FILE* f = nullptr;
            if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) { ++fallos; continue; }
            const size_t wr = fwrite(d.body.data(), 1, d.body.size(), f);
            fclose(f);
            if (wr != d.body.size()) { ++fallos; continue; }
        }
        std::wstring msg;
        if (namChainAdd(path.c_str(), msg) >= 0) ++hechos; else ++fallos;
    }
    std::string res = pname + ":  " + std::to_string(hechos) + " de " + std::to_string((int)blocks.size())
                    + " capturas a la cadena";
    if (fallos) res += "  \xB7  " + std::to_string(fallos) + " no se pudieron bajar";
    if (fuera)  res += "  \xB7  " + std::to_string(fuera) + " no caben (4 celdas)";
    if (derecha) res += "  \xB7  via derecha (" + std::to_string(derecha) + ") no se monta";
    t3kStoreResult("preset", res, 200, "");
    t3kPostDone();
}

// --- Fotos de las tarjetas (una detras de otra, sin bloquear nada) ---------
static void t3kWorkImages(std::vector<std::pair<long long, std::string>> list) {
    for (auto& it : list) {
        if (GetFileAttributesW(t3kImgFile(it.first).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        t3k::Resp r; std::string err;
        if (!t3k::httpRequest(L"GET", it.second, "", std::string(), r, &err)) continue;
        if (!r.ok() || r.body.size() < 400) continue;
        const std::wstring f = t3kImgFile(it.first);
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, f.c_str(), L"wb") == 0 && fp) {
            fwrite(r.body.data(), 1, r.body.size(), fp);
            fclose(fp);
            t3kPostDone();
        }
    }
    g_t3kImgRun.store(false);
}
static void t3kStartImages() {
    if (g_t3kImgRun.load()) return;
    std::vector<std::pair<long long, std::string>> list;
    for (const T3kItem& it : g_t3kItems) if (!it.image.empty()) list.emplace_back(it.id, it.image);
    if (list.empty()) return;
    g_t3kImgRun.store(true);
    std::thread(t3kWorkImages, list).detach();
}

// --- Lista JSON -> tarjetas ------------------------------------------------
static int t3kParseList(const std::string& body, std::vector<T3kItem>& out, long long& total) {
    out.clear(); total = 0;
    t3k::Jv j;
    if (!t3k::jParse(body, j)) return -1;
    const t3k::Jv* arr = (j.t == t3k::Jv::ARR) ? &j : j.get("data");
    if (!arr || arr->t != t3k::Jv::ARR) return -1;
    total = j.i64("total", (long long)arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        const t3k::Jv* o = arr->at(i);
        if (!o || o->t != t3k::Jv::OBJ) continue;
        T3kItem it;
        it.id        = o->i64("id");
        it.title     = u8w(o->str("title"));
        it.gear      = t3kGearLabel(o->str("gear"));
        it.downloads = (int)o->i64("downloads_count");
        it.models    = (int)o->i64("models_count");
        it.a1        = (int)o->i64("a1_models_count");
        it.a2        = (int)o->i64("a2_models_count");
        const t3k::Jv* u = o->get("user");
        if (u && u->t == t3k::Jv::OBJ) {
            it.user = u8w(u->str("username"));
            const t3k::Jv* v = u->get("is_verified");
            it.verified = (v && v->t == t3k::Jv::BOOL) ? v->b : false;
        }
        const t3k::Jv* im = o->get("images");
        if (im && im->t == t3k::Jv::ARR && im->size() > 0 && im->at(0) && im->at(0)->t == t3k::Jv::STR)
            it.image = im->at(0)->s;
        const std::string ca = o->str("created_at");          // ISO: YYYY-MM-DD...
        if (ca.size() >= 10) {
            int y = 0, mo = 0, dd = 0;
            if (sscanf_s(ca.c_str(), "%4d-%2d-%2d", &y, &mo, &dd) == 3 && y > 2000) {
                std::tm tm = {};
                tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = dd;
                const time_t t0 = _mkgmtime(&tm);
                if (t0 > 0) it.ageDays = (long long)((difftime(time(nullptr), t0)) / 86400.0);
            }
        }
        if (it.id == 0 && it.title.empty()) continue;
        out.push_back(std::move(it));
    }
    if (total <= 0) total = (long long)out.size();
    return 0;
}

// --- La UI recoge el trabajo terminado (hilo de la ventana) ---------------
static void t3kApplyResult();
static void artBaseDir() {
    if (g_artDir[0]) return;
    WCHAR mod[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    WCHAR* sl = wcsrchr(mod, L'\\');
    if (sl) *sl = 0;
    swprintf_s(g_artDir, L"%s\\amps_frontales", mod);
}
// Logotipo de la cabecera: va incrustado en el .exe como RCDATA (2) y se
// decodifica UNA vez desde memoria. Si el recurso faltara (un .res viejo), la
// cabecera cae al wordmark de texto de siempre.
static G::Bitmap* g_brandLogo  = nullptr;
static bool       g_brandTried = false;
static G::Bitmap* brandLogo() {
    if (g_brandTried) return g_brandLogo;
    g_brandTried = true;
    HRSRC hr = FindResourceA(nullptr, MAKEINTRESOURCEA(2), RT_RCDATA);
    if (!hr) return nullptr;
    const DWORD n = SizeofResource(nullptr, hr);
    HGLOBAL hg = LoadResource(nullptr, hr);
    void* p = hg ? LockResource(hg) : nullptr;
    if (!p || !n) return nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!mem) return nullptr;
    void* mp = GlobalLock(mem);
    if (!mp) { GlobalFree(mem); return nullptr; }
    memcpy(mp, p, n);
    GlobalUnlock(mem);
    IStream* st = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &st)) || !st) { GlobalFree(mem); return nullptr; }
    G::Bitmap* bm = G::Bitmap::FromStream(st, FALSE);
    st->Release();
    if (bm && bm->GetLastStatus() == G::Ok) {
        // Copia propia, ya desacoplada del stream (que se libera al salir).
        G::Bitmap* copy = new G::Bitmap(bm->GetWidth(), bm->GetHeight(), PixelFormat32bppPARGB);
        if (copy && copy->GetLastStatus() == G::Ok) {
            G::Graphics gg(copy);
            gg.SetCompositingMode(G::CompositingModeSourceCopy);
            gg.DrawImage(bm, 0, 0, bm->GetWidth(), bm->GetHeight());
            g_brandLogo = copy;
        } else {
            delete copy;
        }
        delete bm;
    } else {
        delete bm;
    }
    return g_brandLogo;
}

static G::Image* artLoad(const WCHAR* rel) {
    artBaseDir();
    WCHAR p[MAX_PATH];
    swprintf_s(p, L"%s\\%s", g_artDir, rel);
    G::Image* im = G::Image::FromFile(p, false);
    if (!im || im->GetLastStatus() != G::Ok) { delete im; return nullptr; }
    return im;
}
static void loadAmpArt() {   // fondos de los 12 (hero + carrusel)
    for (int i = 0; i < 13; ++i) {
        if (!g_fondoImg[i]) g_fondoImg[i] = artLoad(kAmpArt[i].fondo);
        g_artOk[i] = (g_fondoImg[i] != nullptr);
    }
}
// Caches de arte escalado (definidos mas abajo, junto a roundRect)
static void freeAmpThumbs();
static void freeScaledStrip();
static G::Bitmap* ampThumb(int a, bool dim);
static G::Bitmap* scaledStripFrame(G::Image* strip, int d, int fr);

static void artEnsureStrip(int amp) {   // filmstrip solo del amp activo (RAM)
    if (amp < 0 || amp > 12) return;
    for (int i = 0; i < 13; ++i) {
        if (i == amp) {
            if (!g_stripImg[i]) { g_stripImg[i] = artLoad(kAmpArt[i].strip); freeScaledStrip(); }
        } else if (g_stripImg[i]) {
            delete g_stripImg[i]; g_stripImg[i] = nullptr; freeScaledStrip();
        }
    }
}
static void freeAmpArt() {
    freeAmpThumbs();
    freeScaledStrip();
    for (int i = 0; i < 13; ++i) {
        if (g_fondoImg[i]) { delete g_fondoImg[i]; g_fondoImg[i] = nullptr; }
        if (g_stripImg[i])  { delete g_stripImg[i];  g_stripImg[i]  = nullptr; }
        g_artOk[i] = false;
    }
}
// Perilla del frontal: fotograma del filmstrip ya escalado a la medida del
// knob (ver scaledStripFrame). Escalarlo en cada frame costaba ~5 ms por
// perilla (medido: 29 ms las seis del frontal).
static void drawArtKnob(G::Graphics& g, G::Image* strip, G::REAL cx, G::REAL cy, G::REAL r,
                        float v01, G::Color ac, bool hov, bool drag) {
    if (!strip) return;
    int fr = (int)(v01*100.0f + 0.5f); if (fr < 0) fr = 0; if (fr > 100) fr = 100;
    const int D  = (int)(r*2.0f + 0.5f);
    const int dx = (int)(cx - r + 0.5f), dy = (int)(cy - r + 0.5f);
    G::Bitmap* sc = scaledStripFrame(strip, D, fr);
    if (sc) g.DrawImage(sc, dx, dy, D, D);
    if (hov || drag) {
        G::Pen hp(G::Color(drag ? 255 : 190, ac.GetR(), ac.GetG(), ac.GetB()), drag ? 2.2f : 1.6f);
        g.DrawEllipse(&hp, (G::REAL)dx - 2.0f, (G::REAL)dy - 2.0f, (G::REAL)D + 4.0f, (G::REAL)D + 4.0f);
    }
}

// PKEY_Device_FriendlyName (definido manualmente para evitar dependencias)
static const PROPERTYKEY PKEY_Device_FriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};

// ============================================================================
// --- Sistema de cuadros: un solo velo y un solo radio para TODAS las superficies
// que flotan sobre la app (panel TONE3000, anadir pedal, AJUSTES, afinador,
// renombrar). Antes cada cuadro llevaba su velo (alpha 165..190, dos con tinte
// azul) y su radio (7, 12 y 14): cinco piezas donde tiene que haber una.
static const G::Color kVeil   (178, 0, 0, 0);
static const G::REAL  kRModal = 12.0f;

// Dispositivos de audio: enumeracion y seleccion
// ============================================================================
struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
};

static std::vector<AudioDeviceInfo> g_inputDevices;
static std::vector<AudioDeviceInfo> g_outputDevices;
static int g_selectedInputIdx = 0;
static int g_selectedOutputIdx = 0;

// IDs actuales (protegidos por mutex; leidos por el hilo de audio)
static std::wstring g_currentInputId;
static std::wstring g_currentOutputId;
static std::mutex   g_deviceMutex;

// Flags de control del hilo de audio
static std::atomic<bool> g_audioQuit{false};
static std::atomic<bool> g_deviceChanged{false};
// Boton global AUDIO ON/OFF: arranca apagado; el usuario lo enciende
static std::atomic<bool> g_audioEnabled{false};
// Lo que el motor CONSIGUIO al abrir la sesion (modo, frecuencia y buffer
// reales). La linea de estado ensena esto, no lo que se PIDIO: en modo
// compartido el dispositivo impone su periodo, y exclusivo puede caer a
// compartido si el endpoint no lo acepta.
static std::atomic<int> g_sessRate{0};
static std::atomic<int> g_sessBufFrames{0};
static std::atomic<int> g_sessExclusive{0};

static std::wstring GetDeviceFriendlyName(IMMDevice* dev) {
    if (!dev) return L"Desconocido";
    IPropertyStore* props = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props)) || !props) return L"Desconocido";
    PROPVARIANT var;
    PropVariantInit(&var);
    std::wstring name = L"Desconocido";
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) && var.pwszVal) {
        name = var.pwszVal;
    }
    PropVariantClear(&var);
    props->Release();
    return name;
}

// syncCurrent = false al recargar la lista desde el panel: solo se reconstruye
// la lista, sin tocar los IDs con los que esta corriendo el motor (si no, la
// interfaz diria que suena un dispositivo que el hilo de audio no ha abierto).
static void EnumerateDevices(bool syncCurrent = true) {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)) || !enumerator)
        return;

    g_inputDevices.clear();
    g_outputDevices.clear();

    IMMDeviceCollection* coll = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &coll)) && coll) {
        UINT count = 0;
        coll->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(coll->Item(i, &dev)) || !dev) continue;
            LPWSTR id = nullptr;
            dev->GetId(&id);
            std::wstring name = GetDeviceFriendlyName(dev);
            if (id) {
                g_inputDevices.push_back({id, name});
                CoTaskMemFree(id);
            }
            dev->Release();
        }
        coll->Release();
    }

    coll = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll)) && coll) {
        UINT count = 0;
        coll->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(coll->Item(i, &dev)) || !dev) continue;
            LPWSTR id = nullptr;
            dev->GetId(&id);
            std::wstring name = GetDeviceFriendlyName(dev);
            if (id) {
                g_outputDevices.push_back({id, name});
                CoTaskMemFree(id);
            }
            dev->Release();
        }
        coll->Release();
    }

    enumerator->Release();

    if (g_selectedInputIdx >= (int)g_inputDevices.size()) g_selectedInputIdx = 0;
    if (g_selectedOutputIdx >= (int)g_outputDevices.size()) g_selectedOutputIdx = 0;

    if (!syncCurrent) return;
    std::lock_guard<std::mutex> lock(g_deviceMutex);
    if (!g_inputDevices.empty())
        g_currentInputId = g_inputDevices[g_selectedInputIdx].id;
    if (!g_outputDevices.empty())
        g_currentOutputId = g_outputDevices[g_selectedOutputIdx].id;
}

static void SelectInputDevice(int idx) {
    if (g_inputDevices.empty()) return;
    if (idx < 0) idx = (int)g_inputDevices.size() - 1;
    if (idx >= (int)g_inputDevices.size()) idx = 0;
    g_selectedInputIdx = idx;
    {
        std::lock_guard<std::mutex> lock(g_deviceMutex);
        g_currentInputId = g_inputDevices[idx].id;
    }
    g_deviceChanged.store(true);
}

static void SelectOutputDevice(int idx) {
    if (g_outputDevices.empty()) return;
    if (idx < 0) idx = (int)g_outputDevices.size() - 1;
    if (idx >= (int)g_outputDevices.size()) idx = 0;
    g_selectedOutputIdx = idx;
    {
        std::lock_guard<std::mutex> lock(g_deviceMutex);
        g_currentOutputId = g_outputDevices[idx].id;
    }
    g_deviceChanged.store(true);
}

static IMMDevice* OpenDevice(IMMDeviceEnumerator* enumerator,
                             const std::wstring& id, EDataFlow flow) {
    IMMDevice* dev = nullptr;
    if (id.empty()) {
        enumerator->GetDefaultAudioEndpoint(flow, eConsole, &dev);
    } else {
        enumerator->GetDevice(id.c_str(), &dev);
    }
    return dev;
}

// ============================================================================
//  Ajustes de audio estilo REAPER (panel de configuracion)
// ============================================================================
static const int kRates[]   = { 44100, 48000, 88200, 96000, 176400, 192000 };
static const int kNumRates  = 6;
static const int kBuffers[] = { 128, 256, 512, 1024, 2048, 4096 };
static const int kNumBufs   = 6;

struct AudioSettings {
    int  system;         // 0 compartido, 1 exclusivo
    int  rateIdx;
    int  bufIdx;
    bool enableInput;
    bool enableOutput;
};
static AudioSettings g_settings   = { 0, 1, 2, true, true };  // 48 kHz, 512
static AudioSettings g_appliedSet = { 0, 1, 2, true, true };
static std::mutex     g_settingsMutex;

static int  g_setInIdx  = 0;
static int  g_setOutIdx = 0;
static bool g_settingsOpen = false;

// Rects del panel para hit-testing (se actualizan al pintar)
static const int kSetPanelW = 940, kSetPanelH = 460;   // alto: 6 filas + RECARGAR + estado
static RECT g_panelRows[7] = {};
// REJILLA del panel AJUSTES. Todo lo que va dentro (titulo, rotulos, ayuda y
// botones) comparte estos ejes; antes cada bloque tiraba de su propio margen
// (titulo 30, banda 32, rotulo 44) y las flechas eran dos circulos FUERA de la
// pastilla, asi que cada fila tenia cuatro cajas a cuatro x distintas.
//   X0/X1      margenes de contenido          (relativos a px)
//   LABEL_W    columna del rotulo
//   PILL_L     donde empieza la pastilla       (relativo a px)
//   ROW_Y0/PITCH  ritmo vertical de las filas
static const float kSetX0 = 32.0f, kSetX1 = 908.0f;
static const float kSetLabelW = 150.0f, kSetPillL = 192.0f, kSetPillH = 30.0f;
static const float kSetRowY0 = 70.0f, kSetRowPitch = 36.0f;

// ---------------------------------------------------------------------------
//  IDIOMA DE LA INTERFAZ (espanol / ingles)
// ---------------------------------------------------------------------------
//  La clave de la tabla es el texto TAL Y COMO esta escrito en el codigo (en
//  espanol) y el valor es su version inglesa. tr() devuelve el MISMO puntero si
//  no hay entrada, asi que lo que no es texto de interfaz (nombres de preset
//  del usuario, rutas, valores ya formateados) pasa intacto. Se llama solo al
//  pintar (hilo de la ventana).
static int g_lang = 0;                 // 0 = espanol, 1 = ingles
struct LangEntry { const WCHAR* es; const WCHAR* en; };
static const LangEntry kLangTable[] = {
    { L"SIMULADOR DE AMPLIFICADORES",              L"GUITAR AMP SIMULATOR" },
    { L"←→ presets · ↑↓ lista · ESPACIO audio · ESC salir",
                                                   L"←→ presets · ↑↓ list · SPACE audio · ESC close" },
    { L"ANTES DEL AMPLIFICADOR",                   L"BEFORE THE AMP" },
    { L"CABEZAL",                                  L"AMP HEAD" },
    { L"DESPUÉS DEL AMPLIFICADOR",                 L"AFTER THE AMP" },
    { L"+ AÑADIR",                                 L"+ ADD" },
    { L"perfil .nam",                              L".nam file" },
    { L"SIN PERFIL",                               L"NO PROFILE" },
    { L"PULSA CARGAR PERFIL (.NAM) · %d EN TU LIBRERIA",
                                                   L"PRESS LOAD PROFILE (.NAM) · %d IN YOUR LIBRARY" },
    { L"CABINA",                                   L"CABINET" },
    { L"MEZCLA",                                   L"MIX" },
    { L"DIRECTO",                                  L"DIRECT" },
    { L"SIN AMPLIFICADOR",                         L"NO AMPLIFIER" },
    { L"Buscar amplificador...",                   L"Search amplifier..." },
    { L"PERFIL",                                   L"PROFILE" },
    { L"CAPTURA · NEURONAL",                       L"CAPTURE · NEURAL" },
    { L"VERIFICADOS",                              L"VERIFIED" },
    { L"TODOS",                                    L"ALL" },
    { L"TENDENCIAS (%s): %d de %d",                L"TRENDING (%s): %d of %d" },
    { L"RESULTADOS (%s en la pagina): %d de %d",   L"RESULTS (%s on page): %d of %d" },
    { L"LA BUSQUEDA NECESITA CONECTAR CON TONE3000",
                                                   L"SEARCH NEEDS A TONE3000 CONNECTION" },
    { L"TENDENCIAS DE TONE3000 (publico, sin clave)",
                                                   L"TONE3000 TRENDING (public, no key)" },
    { L"RESULTADOS DE LA BUSQUEDA",                L"SEARCH RESULTS" },
    { L"LA RESPUESTA NO SE PUDO LEER",             L"THE RESPONSE COULD NOT BE READ" },
    { L"LA BUSQUEDA CON FILTROS NECESITA TU TOKEN: PULSA CONECTAR O USA \"VER TONE3000\"",
                                                   L"FILTERED SEARCH NEEDS YOUR TOKEN: PRESS CONNECT OR USE \"SEE TONE3000\"" },
    { L"TU TOKEN DE TONE3000 NO VALE O HA CADUCADO: VUELVE A PEGARLO",
                                                   L"YOUR TONE3000 TOKEN IS INVALID OR EXPIRED: PASTE IT AGAIN" },
    { L"TONES/SEARCH ESTA FUERA DE LA CAPA GRATUITA: USA \"VER TONE3000\"",
                                                   L"TONES/SEARCH IS OUTSIDE THE FREE TIER: USE \"SEE TONE3000\"" },
    { L"SIN CONEXION CON TONE3000",                L"NO CONNECTION TO TONE3000" },
    { L"EL SERVIDOR DEVOLVIO %d",                  L"THE SERVER RETURNED %d" },
    { L"PRESET CARGADO",                           L"PRESET LOADED" },
    { L"CONFIRMA EL CODIGO EN EL NAVEGADOR (YA LO TIENES COPIADO)",
                                                   L"CONFIRM THE CODE IN YOUR BROWSER (ALREADY COPIED)" },
    { L"CONFIRMA EL CODIGO EN EL NAVEGADOR",       L"CONFIRM THE CODE IN YOUR BROWSER" },
    { L"ESA NO ES UNA CLAVE PUBLICABLE (t3k_pub_...). YA HE VACIADO EL CAMPO:",
                                                   L"THAT IS NOT A PUBLISHABLE KEY (t3k_pub_...). I CLEARED THE FIELD:" },
    { L" PULSA CONECTAR OTRA VEZ Y LO HAGO SOLO",  L" PRESS CONNECT AGAIN AND I WILL DO IT" },
    { L"NO SE PUDO CONECTAR: ",                    L"COULD NOT CONNECT: " },
    { L"EL SERVIDOR NO DEVOLVIO TOKEN",            L"THE SERVER RETURNED NO TOKEN" },
    { L"CONECTADO CON TONE3000",                   L"CONNECTED TO TONE3000" },
    { L"CONECTADO CON LA SESION DE TU TONE3000 INSTALADO",
                                                   L"CONNECTED WITH YOUR INSTALLED TONE3000 SESSION" },
    { L"LA SESION INSTALADA YA NO VALE: TE DOY UN CODIGO PARA CONFIRMAR",
                                                   L"THE INSTALLED SESSION IS INVALID: I WILL GIVE YOU A CODE TO CONFIRM" },
    { L"ESA CLAVE O TOKEN NO VALE (",              L"THAT KEY OR TOKEN IS NOT VALID (" },
    { L"). YA HE VACIADO EL CAMPO: PULSA CONECTAR OTRA VEZ Y NECESITARE UN CODIGO",
                                                   L"). I CLEARED THE FIELD: PRESS CONNECT AGAIN AND I WILL NEED A CODE" },
    { L"EN LA CELDA ",                             L"IN SLOT " },
    { L"CAPTURA PUESTA EN LA CELDA",               L"CAPTURE PLACED IN SLOT" },
    { L"EL FICHERO NO VALE: ",                     L"INVALID FILE: " },
    { L"NO SE PUDO BAJAR: ",                       L"DOWNLOAD FAILED: " },
    { L"NO SE PUDO BAJAR (HTTP ",                  L"DOWNLOAD FAILED (HTTP " },
    { L"CONECTAR CON TONE3000",                    L"CONNECT TO TONE3000" },
    { L"Abre la pagina, entra en tu cuenta y confirma este codigo:",
                                                   L"Open the page, sign in to your account and confirm this code:" },
    { L"El codigo caduca en %d minutos",           L"The code expires in %d minutes" },
    { L"ABRIR TONE3000.COM/ACTIVATE",              L"OPEN TONE3000.COM/ACTIVATE" },
    { L"Pulsa CONECTAR y me busco la vida: uso tu TONE3000 instalado, o te doy un codigo.",
                                                   L"Press CONNECT and I will sort it out: I use your installed TONE3000, or give you a code." },
    { L"  Ctrl+V solo si quieres pegar una clave secreta o un token.",
                                                   L"  Ctrl+V only if you want to paste a secret key or a token." },
    { L"clave o token",                            L"key or token" },
    { L"Vacio: cojo la sesion de tu TONE3000 instalado (y si no, te doy",
                                                   L"Empty: I use your installed TONE3000 session (and if not, I give you" },
    { L"un codigo). Con clave secreta o token pegados: entra directo.",
                                                   L"a code). With a secret key or token pasted: it goes straight in." },
    { L"TENDENCIAS",                               L"TRENDING" },
    { L"NUEVOS",                                   L"NEW" },
    { L"FAVORITOS",                                L"FAVORITES" },
    { L"DESCARGADOS",                              L"DOWNLOADED" },
    { L"CONECTAR",                                 L"CONNECT" },
    { L"CANCELAR",                                 L"CANCEL" },
    { L"PERFILES NEURALES · CAPTURAS .NAM",        L"NEURAL PROFILES · .NAM CAPTURES" },
    { L"buscar (fender, jcm800, sd-1...)",         L"search (fender, jcm800, sd-1...)" },
    { L"conectar para buscar",                     L"connect to search" },
    { L"BUSCAR",                                   L"SEARCH" },
    { L"BIBLIOTECA ✓",                             L"LIBRARY ✓" },
    { L"BIBLIOTECA",                               L"LIBRARY" },
    { L"CONECTADO",                                L"CONNECTED" },
    { L"VER TONE3000",                             L"SEE TONE3000" },
    { L"Todos",                                    L"All" },
    { L"CONSULTANDO TONE3000...",                  L"QUERYING TONE3000..." },
    { L"sin resultados: prueba una lista o conecta para buscar",
                                                   L"no results: try a list or connect to search" },
    { L"%d bloque%s  ·  .t3kpreset",               L"%d block%s  ·  .t3kpreset" },
    { L"carga las capturas de su cadena",          L"loads the captures in its chain" },
    { L"%d modelo%s  A1 %d / A2 %d",               L"%d model%s  A1 %d / A2 %d" },
    { L"CLIC: CARGAR EN LA CADENA",                L"CLICK: LOAD INTO THE CHAIN" },
    { L"CLIC: BAJAR Y AÑADIR A LA CADENA",         L"CLICK: DOWNLOAD AND ADD TO THE CHAIN" },
    { L"captura .nam",                             L".nam capture" },
    { L"✓ AUTORES VERIFICADOS",                    L"✓ VERIFIED AUTHORS" },
    { L"%d tonos",                                 L"%d tones" },
    { L"LISTO",                                    L"READY" },
    { L"los perfiles son de sus autores: se descargan, no se incluyen",
                                                   L"profiles belong to their authors: downloaded, not included" },
    { L"CURADO",                                   L"CURATED" },
    { L"AMPLI",                                    L"AMP" },
    { L"RECIENTES",                                L"RECENT" },
    { L"SIN COLOR",                                L"NO COLOR" },
    { L"AMBAR",                                    L"AMBER" },
    { L"ROJO",                                     L"RED" },
    { L"AZUL",                                     L"BLUE" },
    { L"VERDE",                                    L"GREEN" },
    { L"FABRICA",                                  L"FACTORY" },
    { L"MIOS",                                     L"MINE" },
    { L"%d CAPTURA%s",                             L"%d CAPTURE%s" },
    { L"SIN ETIQUETA",                             L"UNTAGGED" },
    { L"LIMPIO",                                   L"CLEAN" },
    { L"AMBIENTE",                                 L"AMBIENT" },
    { L"OTROS",                                    L"OTHER" },
    { L"NAVEGADOR  ·  %d DE %d",                   L"BROWSER  ·  %d OF %d" },
    { L"FAVORITO",                                 L"FAVORITE" },
    { L"ORIGEN",                                   L"SOURCE" },
    { L"CARACTER",                                 L"CHARACTER" },
    { L"AMPLIFICADOR",                             L"AMPLIFIER" },
    { L"Buscar preset...",                         L"Search preset..." },
    { L"sin resultados",                           L"no results" },
    { L" (no disponible)",                         L" (unavailable)" },
    { L"Buscar por nombre, ampli, etiqueta o autor",
                                                   L"Search by name, amp, tag or author" },
    { L"SIN AMPLI",                                L"NO AMP" },
    { L"MIO",                                      L"MINE" },
    { L"NINGUN PRESET CUMPLE LOS FILTROS",         L"NO PRESET MATCHES THE FILTERS" },
    { L"INFORMACION",                              L"INFO" },
    { L"PRESET MIO",                               L"MY PRESET" },
    { L"DE FABRICA",                               L"FACTORY" },
    { L"AUTOR",                                    L"AUTHOR" },
    { L"COMENTARIO",                               L"COMMENT" },
    { L"ETIQUETAS",                                 L"TAGS" },
    { L"CREADO %s   ·   USADO %s",                 L"CREATED %s   ·   USED %s" },
    { L"SEGURO? PULSA OTRA VEZ",                   L"SURE? PRESS AGAIN" },
    { L"BORRAR PRESET",                            L"DELETE PRESET" },
    { L"los presets de fabrica no se editan: guarda una copia para tocar sus metadatos",
                                                   L"factory presets are read-only: save a copy to edit its metadata" },
    { L"ESC CIERRA   ·   UN CLIC CARGA EL PRESET   ·   EL PUNTO DE COLOR ES EL FAVORITO   ·   EN UN PRESET MIO SE ESCRIBEN LOS CAMPOS Y BORRAR PIDE DOS CLICS",
                                                   L"ESC CLOSES   ·   ONE CLICK LOADS THE PRESET   ·   THE COLOR DOT IS THE FAVORITE   ·   MY PRESETS HAVE EDITABLE FIELDS AND DELETE NEEDS TWO CLICKS" },
    { L"METADATOS GUARDADOS",                      L"METADATA SAVED" },
    { L"RENOMBRAR PRESET",                         L"RENAME PRESET" },
    { L"GUARDAR COMO",                             L"SAVE AS" },
    { L"GRABAR",                                   L"RECORD" },
    // Nombres de los 78 presets de fabrica: el nombre es contenido, pero en una
    // interfaz inglesa un catalogo en espanol se lee a medias.
    { L"LIMPIO DIAMANTE",                        L"CLEAN DIAMOND" },
    { L"TWANG DE PUENTE",                        L"BRIDGE TWANG" },
    { L"ROMPE SUAVE",                            L"SOFT BREAKUP" },
    { L"SOLO CREMA",                             L"CREAMY LEAD" },
    { L"NIEBLA PLATEADA",                        L"SILVER HAZE" },
    { L"FUZZ DE GARAJE",                         L"GARAGE FUZZ" },
    { L"CAMPANA PURA",                           L"PURE BELL" },
    { L"JANGLE BRIT",                            L"BRIT JANGLE" },
    { L"EDGE VOX",                               L"VOX EDGE" },
    { L"SOLO CRISTAL",                           L"CRYSTAL LEAD" },
    { L"TREMOLO AZUL",                           L"BLUE TREMOLO" },
    { L"FUZZ BEATLE",                            L"BEATLE FUZZ" },
    { L"TWEED LIMPIO",                           L"CLEAN TWEED" },
    { L"EDGE DE NOCHE",                          L"NIGHT EDGE" },
    { L"CRUNCH 5E3",                             L"5E3 CRUNCH" },
    { L"SOLO CALIENTE",                          L"HOT LEAD" },
    { L"SALA DE MADERA",                         L"WOODEN ROOM" },
    { L"TREMOLO DE TWEED",                       L"TWEED TREMOLO" },
    { L"PRISTINO",                               L"PRISTINE" },
    { L"CORO ANCHO",                             L"WIDE CHORUS" },
    { L"ROTURA SOLIDA",                          L"SOLID BREAKUP" },
    { L"SOLO VIDRIO",                            L"GLASS LEAD" },
    { L"CIELO AZUL",                             L"BLUE SKY" },
    { L"PLEXI LIMPIO",                           L"CLEAN PLEXI" },
    { L"CRUNCH 800",                             L"800 CRUNCH" },
    { L"SOLO 800",                               L"800 LEAD" },
    { L"ECO DE SALA",                            L"ROOM ECHO" },
    { L"FUZZ MARSHALL",                          L"MARSHALL FUZZ" },
    { L"PLEXI AIRE",                             L"AIRY PLEXI" },
    { L"ROMPE PLEXI",                            L"PLEXI BREAKUP" },
    { L"CRUNCH 1959",                            L"1959 CRUNCH" },
    { L"LEAD CLASICO",                           L"CLASSIC LEAD" },
    { L"SALA PLEXI",                             L"PLEXI ROOM" },
    { L"VIENTO PSICODELICO",                     L"PSYCHEDELIC WIND" },
    { L"NARANJA LIMPIA",                         L"CLEAN ORANGE" },
    { L"ROMPE NARANJA",                          L"ORANGE BREAKUP" },
    { L"CRUNCH GORDO",                           L"FAT CRUNCH" },
    { L"SOLO GORDO",                             L"FAT LEAD" },
    { L"NIEBLA NARANJA",                         L"ORANGE HAZE" },
    { L"FUZZ NARANJA",                           L"ORANGE FUZZ" },
    { L"BLUES LIMPIO",                           L"CLEAN BLUES" },
    { L"BLUES MORDIDO",                          L"BITING BLUES" },
    { L"CRUNCH BLUES",                           L"BLUES CRUNCH" },
    { L"SOLO BLUES",                             L"BLUES LEAD" },
    { L"HABITACION BLUES",                       L"BLUES ROOM" },
    { L"WAH BLUES",                              L"BLUES WAH" },
    { L"SESION LIMPIA",                          L"CLEAN SESSION" },
    { L"TERCIOPELO",                             L"VELVET" },
    { L"CRUNCH VOCAL",                           L"VOCAL CRUNCH" },
    { L"SOLO VOCAL",                             L"VOCAL LEAD" },
    { L"PAD VOCAL",                              L"VOCAL PAD" },
    { L"FUZZ CANTADO",                           L"SINGING FUZZ" },
    { L"RECTO LIMPIO",                           L"CLEAN RECTO" },
    { L"RECTO MODERNO",                          L"MODERN RECTO" },
    { L"SOLO RECTO",                             L"RECTO LEAD" },
    { L"RECTO ESPACIO",                          L"RECTO SPACE" },
    { L"MURO DE RECTO",                          L"RECTO WALL" },
    { L"KRAFT LIMPIO",                           L"CLEAN KRAFT" },
    { L"KRAFT BORDE",                            L"KRAFT EDGE" },
    { L"KRAFT APRETADO",                         L"TIGHT KRAFT" },
    { L"SOLO KRAFT",                             L"KRAFT LEAD" },
    { L"KRAFT ETEREO",                           L"ETHEREAL KRAFT" },
    { L"MURO KRAFT",                             L"KRAFT WALL" },
    { L"HAVOC LIMPIO",                           L"CLEAN HAVOC" },
    { L"HAVOC FILO",                             L"HAVOC EDGE" },
    { L"HAVOC SATURADO",                         L"SATURATED HAVOC" },
    { L"SOLO HAVOC",                             L"HAVOC LEAD" },
    { L"HAVOC NEBLINA",                          L"HAVOC MIST" },
    { L"HAVOC CAOS",                             L"HAVOC CHAOS" },
    { L"CAPTURA LIMPIA",                         L"CLEAN CAPTURE" },
    { L"PERFIL BORDE",                           L"PROFILE EDGE" },
    { L"PERFIL CRUNCH",                          L"PROFILE CRUNCH" },
    { L"SOLO NEURONAL",                          L"NEURAL LEAD" },
    { L"SALA NEURONAL",                          L"NEURAL ROOM" },
    { L"PERFIL ESPECIAL",                        L"SPECIAL PROFILE" },
    { L"SILENCIO",                                 L"MUTED" },
    { L"GUARDAR",                                  L"SAVE" },
    { L"ENTRADA",                                  L"INPUT" },
    { L"SALIDA",                                   L"OUTPUT" },
    { L"CARGAR IR",                                L"LOAD IR" },
    { L"AFINADOR",                                 L"TUNER" },
    { L"SIN AMPLIFICADORES CON ESE FILTRO",        L"NO AMPS WITH THAT FILTER" },
    { L"la estrella filtra: solo tus amplis marcados",
                                                   L"the star filters: only your starred amps" },
    { L"borra el filtro de la caja de busqueda",   L"clear the search box filter" },
    { L"AÑADIR",                                   L"ADD" },
    { L"AÑADIR PEDAL",                             L"ADD PEDAL" },
    { L"YA EN LA CADENA",                         L"IN THE CHAIN" },
    { L"CLIC PARA AÑADIR",                         L"CLICK TO ADD" },
    { L"MOTOR",                                    L"ENGINE" },
    { L"FREC",                                     L"RATE" },
    { L"AJUSTES DE AUDIO",                         L"AUDIO SETTINGS" },
    { L"DISPOSITIVOS",                             L"DEVICES" },
    { L"RECARGAR",                                 L"REFRESH" },
    { L"APLICAR",                                  L"APPLY" },
    { L"IDIOMA",                                   L"LANGUAGE" },
    { L"TOCA UNA CUERDA PARA AFINAR",              L"PLAY A STRING TO TUNE" },
    { L"%+.0f centavos",                           L"%+.0f cents" },
    { L"CERRAR",                                   L"CLOSE" },
    { L"PAGINA ABIERTA · CODIGO COPIADO: PEGALO SI TE LO PIDE",
                                                   L"PAGE OPENED · CODE COPIED: PASTE IT IF ASKED" },
    { L"PAGINA ABIERTA EN EL NAVEGADOR",           L"PAGE OPENED IN YOUR BROWSER" },
    { L"NO PUDE ABRIR EL NAVEGADOR: CODIGO COPIADO, PEGALO EN TONE3000.COM/ACTIVATE",
                                                   L"COULD NOT OPEN THE BROWSER: CODE COPIED, PASTE IT AT TONE3000.COM/ACTIVATE" },
    { L"NO PUDE ABRIR EL NAVEGADOR: VE A TONE3000.COM/ACTIVATE Y ESCRIBE EL CODIGO",
                                                   L"COULD NOT OPEN THE BROWSER: GO TO TONE3000.COM/ACTIVATE AND TYPE THE CODE" },
    { L"PARA CARGAR UN PRESET HAY QUE CONECTAR: SUS CAPTURAS SE DESCARGAN",
                                                   L"TO LOAD A PRESET YOU MUST CONNECT: ITS CAPTURES ARE DOWNLOADED" },
    { L"LAS CELDAS ESTAN LLENAS: QUITA UNA CAPTURA ANTES",
                                                   L"THE SLOTS ARE FULL: REMOVE A CAPTURE FIRST" },
    { L"BAJANDO LAS CAPTURAS DEL PRESET...",       L"DOWNLOADING THE PRESET CAPTURES..." },
    { L"PARA BAJAR UN PERFIL HAY QUE CONECTAR CON TONE3000",
                                                   L"TO DOWNLOAD A PROFILE YOU MUST CONNECT TO TONE3000" },
    { L"BAJANDO LA CAPTURA DE ",                   L"DOWNLOADING THE CAPTURE OF " },
    { L"LO QUE ELIJAS IRA A ESA CELDA",            L"WHAT YOU PICK WILL GO INTO THAT SLOT" },
    { L"PULSA OTRA VEZ PARA QUITAR LA CAPTURA",    L"PRESS AGAIN TO REMOVE THE CAPTURE" },
    { L"CAPTURAS INTERCAMBIADAS DE CELDA",         L"CAPTURES SWAPPED BETWEEN SLOTS" },
    { L"CAPTURA AÑADIDA A LA CADENA",              L"CAPTURE ADDED TO THE CHAIN" },
    { L"Anadir captura (.nam) a la cadena del PERFIL",
                                                   L"Add (.nam) capture to the PROFILE chain" },
    { L"Cargar IR de cabina (WAV)",                L"Load cabinet IR (WAV)" },
    { L"No se pudo cargar el WAV como respuesta de impulso.\nFormatos admitidos: PCM 8/16/24/32 bits o float 32/64.",
                                                   L"The WAV could not be loaded as an impulse response.\nSupported formats: PCM 8/16/24/32 bits or float 32/64." },
    { L"LAS CELDAS ESTAN LLENAS: QUITA UNA PARA PONER OTRA",
                                                   L"THE SLOTS ARE FULL: REMOVE ONE TO ADD ANOTHER" },
    { L"no se pudo cargar: ",                      L"could not be loaded: " },
    { L"BLOQUE PUESTO EN LA CELDA",                L"BLOCK PLACED IN SLOT" },
    { L"CARGANDO EL PRESET DESDE TU LIBRERIA",     L"LOADING THE PRESET FROM YOUR LIBRARY" },
    { L"SIN ESPACIO: BORRA UN PRESET",             L"NO SPACE: DELETE A PRESET" },
    { L"PRESET ACTUALIZADO",                       L"PRESET UPDATED" },
    { L"EL PRESET NECESITA UN NOMBRE",             L"THE PRESET NEEDS A NAME" },
    { L"PRESET GUARDADO",                          L"PRESET SAVED" },
    { L"PRESET RENOMBRADO",                        L"PRESET RENAMED" },
    { L"PRESET BORRADO",                           L"PRESET DELETED" },
    { L"CARGANDO TENDENCIAS...",                   L"LOADING TRENDING..." },
    { L"ESA LISTA NECESITA TU TOKEN DE TONE3000",  L"THAT LIST NEEDS YOUR TONE3000 TOKEN" },
    { L"CARGANDO ",                                L"LOADING " },
    { L"BUSCANDO...",                              L"SEARCHING..." },
    { L"CONECTANDO CON TU TONE3000 INSTALADO...",  L"CONNECTING WITH YOUR INSTALLED TONE3000..." },
    { L"TE DOY UN CODIGO PARA CONFIRMAR EN TONE3000.COM/ACTIVATE",
                                                   L"I WILL GIVE YOU A CODE TO CONFIRM AT TONE3000.COM/ACTIVATE" },
    { L"CONECTANDO CON CODIGO: SIGUE LOS PASOS EN TONE3000.COM",
                                                   L"CONNECTING WITH CODE: FOLLOW THE STEPS AT TONE3000.COM" },
    { L"BUSCANDO TU SESION DE TONE3000...",        L"LOOKING FOR YOUR TONE3000 SESSION..." },
    { L"HABLANDO CON TONE3000...",                 L"TALKING TO TONE3000..." },
    { L"hoy",                                      L"today" },
    { L"%lld sem",                                 L"%lld w" },
    { L"%lld mes",                                 L"%lld mo" },
    { L"%lld a",                                   L"%lld y" },
    { L"MOTOR DE AUDIO",                           L"AUDIO ENGINE" },
    { L"PARAR %lu:%02lu",                          L"STOP %lu:%02lu" },
    { L"SIN CAMBIOS",                              L"NO CHANGES" },
    { L"DETECTADO",                                L"DETECTED" },
    { L"SE HA DESCONECTADO ALGO",                  L"SOMETHING WAS DISCONNECTED" },
    { L" ENTRADA(S), ",                            L" INPUT(S), " },
    { L" SALIDA(S)",                               L" OUTPUT(S)" },
    { L"WASAPI EXCLUSIVO",                         L"WASAPI EXCLUSIVE" },
    { L"WASAPI COMPARTIDO",                        L"WASAPI SHARED" },
    { L"(sin dispositivos)",                       L"(no devices)" },
    { L"FRECUENCIA",                               L"SAMPLE RATE" },
    { L"%d muestras (%.1f ms)",                    L"%d samples (%.1f ms)" },
    { L"Latencia estimada (captura + salida): ~%.1f ms",
                                                   L"Estimated latency (capture + output): ~%.1f ms" },
    { L"Anillo corto: %lld bloque(s), peor hueco %lld muestras | tardios: %lld | colchon: %d",
                                                   L"Short ring: %lld block(s), worst gap %lld samples | late: %lld | cushion: %d" },
};
static const WCHAR* tr(const WCHAR* es) {
    if (g_lang == 0 || !es || !es[0]) return es;
    // Ordenada una vez, al primer uso: la tabla de arriba se escribe por bloques
    // y no puede exigir que nadie la mantenga alfabetica a mano.
    static const std::vector<LangEntry> tbl = [] {
        std::vector<LangEntry> v(kLangTable, kLangTable + sizeof(kLangTable)/sizeof(kLangTable[0]));
        std::sort(v.begin(), v.end(), [](const LangEntry& a, const LangEntry& b) {
            return wcscmp(a.es, b.es) < 0;
        });
        return v;
    }();
    auto it = std::lower_bound(tbl.begin(), tbl.end(), es,
        [](const LangEntry& e, const WCHAR* s) { return wcscmp(e.es, s) < 0; });
    return (it != tbl.end() && wcscmp(it->es, es) == 0) ? it->en : es;
}
static RECT g_panelApply = {}, g_panelCancel = {};

// Estado del boton RECARGAR DISPOSITIVOS: lo que se encontro y cuando.
static std::wstring g_devMsg;
static DWORD        g_devMsgAt = 0;

static int cycleIdx(int idx, int delta, int count) {
    if (count <= 0) return 0;
    idx += delta;
    if (idx < 0) idx = count - 1;
    if (idx >= count) idx = 0;
    return idx;
}

// Recoloca un indice despues de recargar una lista: se busca el dispositivo por
// ID, no por posicion (los indices bailan en cuanto entra o sale uno).
static int remapDeviceIdx(const std::vector<AudioDeviceInfo>& list, const std::wstring& keepId)
{
    if (list.empty()) return 0;
    for (int i = 0; i < (int)list.size(); ++i)
        if (!keepId.empty() && list[(size_t)i].id == keepId) return i;
    return 0;
}

// ---------------------------------------------------------------------------
//  RECARGAR DISPOSITIVOS (boton del panel de ajustes)
// ---------------------------------------------------------------------------
//  Windows solo devuelve los endpoints ACTIVOS en el momento de enumerar, asi
//  que una interfaz enchufada despues de arrancar la app no aparece en la lista
//  hasta volver a enumerar. Esto reconstruye las dos listas y conserva la
//  seleccion POR ID. NO cambia lo que esta sonando: eso lo hace APLICAR, igual
//  que el resto de ajustes del panel (y asi RECARGAR + CANCELAR no toca nada).
static void RefreshDeviceLists(bool quiet = false)
{
    std::wstring inId, outId;
    if (g_selectedInputIdx  >= 0 && g_selectedInputIdx  < (int)g_inputDevices.size())
        inId  = g_inputDevices[(size_t)g_selectedInputIdx].id;
    if (g_selectedOutputIdx >= 0 && g_selectedOutputIdx < (int)g_outputDevices.size())
        outId = g_outputDevices[(size_t)g_selectedOutputIdx].id;
    const int nin  = (int)g_inputDevices.size();
    const int nout = (int)g_outputDevices.size();

    EnumerateDevices(false);            // false: NO tocar lo que esta en el aire

    g_selectedInputIdx  = remapDeviceIdx(g_inputDevices,  inId);
    g_selectedOutputIdx = remapDeviceIdx(g_outputDevices, outId);
    // El panel trabaja sobre su propia copia de indices: se sincroniza para que
    // APLICAR caiga en el dispositivo que se esta viendo.
    g_setInIdx  = g_selectedInputIdx;
    g_setOutIdx = g_selectedOutputIdx;

    if (quiet) return;
    const int din  = (int)g_inputDevices.size()  - nin;
    const int dout = (int)g_outputDevices.size() - nout;
    std::wstring m;
    if (din == 0 && dout == 0) m = tr(L"SIN CAMBIOS");
    else if (din > 0 || dout > 0) m = tr(L"DETECTADO");
    else m = tr(L"SE HA DESCONECTADO ALGO");
    m += L" \xB7 " + std::to_wstring(g_inputDevices.size()) + tr(L" ENTRADA(S), ") +
         std::to_wstring(g_outputDevices.size()) + tr(L" SALIDA(S)");
    g_devMsg = m;
    g_devMsgAt = GetTickCount();
}

// ============================================================================
//  Parametros: 3 amps x 5 knobs + 6 FX x 2 knobs + master + medidores
// ============================================================================
// Que control extra lleva un ampli (0 = sin modelar, 1 = resonancia, 2 = nivel).
// Se define mas abajo, junto a la tabla de etiquetas de las chapas.
static int ampExtraKind(int m);
static int ampExtraKindAt(int m, int idx);
static float ampExtraNeutral(int kind);
static float ampExtraVal(int m, int kind);

struct Params {
    std::atomic<int>   model{0};                       // amp activo 0..11
    std::atomic<float> amp[13][8];                     // Gain Bass Mid Treble Vol Presence
                                                       // [6][7] = controles extra del ampli: los
                                                       // que imprime su chapa y el motor no tenia
                                                       // (resonancia, nivel, tremolo, chorus, comp)
    std::atomic<float> fx[18][2];                      // params por efecto (18 tipos)
    std::atomic<bool>  fxOn[18];
    std::atomic<float> master{70.0f};
    std::atomic<float> inTrim{0.0f};                     // trim de entrada (-18..+18 dB, global)
    std::atomic<int>   cabModel{0};                    // 0..5 procedural, 6 = IR de usuario
    std::atomic<int>   cabMic{0};                      // 0 DYN, 1 RIBBON, 2 COND
    std::atomic<int>   cabDist{1};                     // 0 NEAR, 1 MID, 2 FAR
    std::atomic<float> mixCab{100.0f};                 // mezcla del gabinete (100 = todo el cab)
    std::atomic<float> sala{0.0f};                     // envio de habitacion post-cab (diseno: slider SALA)
    std::atomic<bool>  audioActive{false};
    std::atomic<float> meterIn{0.0f}, meterOut{0.0f};
};
static Params g_params;

// --- Estado de audio visible en la UI (hilo de audio -> pintado) ---
static std::atomic<float> g_clipFlag{0.0f};   // 1.0 al saturar la salida (LED CLIP)
static std::atomic<float> g_grGateDb{0.0f};   // reduccion del gate en dB (0 = abierta)
static std::atomic<bool>  g_sceneDirty{false}; // reconstruir escena estatica en el proximo pintado

// --- Afinador cromatico (modal): estado del hilo de audio -> pintado ---
static std::atomic<bool>  g_tunerOpen{false};   // modal abierto (activa el analisis)
static std::atomic<float> g_tunerHz{0.0f};      // 0 = sin nota
static std::atomic<int>   g_tunerNote{0};       // nota MIDI 24..108
static std::atomic<float> g_tunerCents{0.0f};   // desvio -50..+50 centavos
static std::atomic<float> g_tunerLevel{0.0f};   // RMS de la ventana analizada

// --- Cadena de pedales dinamica: dos filas (antes / despues del amp) ---
// Sin duplicados: cada tipo aparece como mucho una vez en toda la cadena.
// La UI publica el orden; el hilo de audio lo copia por bloque y lo procesa
// estrictamente en ese orden. Los presets definen su propia cadena.
static std::mutex       g_chainMutex;
static std::vector<int> g_chainPre;     // tipos en orden ANTES del amp
static std::vector<int> g_chainPost;    // tipos en orden DESPUES del amp

// --- Publicacion de la cadena SIN CERROJO (para el hilo de audio) ------------
// El hilo de audio no puede coger el candado de arriba (es de la interfaz) ni
// reservar memoria: si la interfaz lo tiene cogido y el sistema la desaloja, el
// hilo de audio espera y pierde el buffer; y copiar a vectores locales dentro
// del candado son DOS malloc por bloque en tiempo real. Aqui la interfaz escribe
// una instantanea de tamaÃ±o fijo con un contador de secuencia (impar mientras
// escribe) y el hilo de audio la copia a buffers con la capacidad ya reservada:
// ni espera ni reserva.
static const int kChainMax = 20;    // 18 tipos de pedal posibles, con margen
struct ChainPub {
    std::atomic<unsigned> seq{0};
    int npre = 0, npost = 0;
    int pre[kChainMax] = {};
    int post[kChainMax] = {};
};
static ChainPub g_chainPub;
// Instantaneas del bloque: solo las toca el hilo de audio. La capacidad se
// reserva en AmpChain::prepare, asi que copiar aqui NO reserva memoria (T13).
static std::vector<int> g_preSnap, g_postSnap;

static void publishChain(const std::vector<int>& pre, const std::vector<int>& post) {
    {
        std::lock_guard<std::mutex> lock(g_chainMutex);
        g_chainPre = pre;
        g_chainPost = post;
    }
    // Instantanea para el hilo de audio (seqlock): con el contador impar, quien
    // lee sabe que hay una escritura a medias y vuelve a empezar. La secuencia
    // es par cuando el contenido esta completo.
    const unsigned s = g_chainPub.seq.load(std::memory_order_relaxed);
    g_chainPub.seq.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    const size_t np = pre.size()  > (size_t)kChainMax ? (size_t)kChainMax : pre.size();
    const size_t nq = post.size() > (size_t)kChainMax ? (size_t)kChainMax : post.size();
    for (size_t i = 0; i < np; ++i) g_chainPub.pre[i]  = pre[i];
    for (size_t i = 0; i < nq; ++i) g_chainPub.post[i] = post[i];
    g_chainPub.npre  = (int)np;
    g_chainPub.npost = (int)nq;
    std::atomic_thread_fence(std::memory_order_release);
    g_chainPub.seq.store(s + 2, std::memory_order_release);
}

// Copia la instantanea publicada a los buffers del bloque. La llama el hilo de
// audio al principio de cada bloque (audioBlockBegin). Sin cerrojo y sin
// reservar; solo reintenta si la interfaz estaba escribiendo justo entonces.
//
// La copia va PRIMERO a temporales y solo se publica cuando la lectura cuadra:
// al copiar directamente sobre los buffers del bloque, un reintento fallido
// dejaria dentro una cadena que nadie publico (el PRE de una y el POST de otra,
// que es lo que caza T14). Si no se consigue, se deja intacta la copia anterior,
// que es una cadena valida de hace un bloque.
static std::atomic<long long> g_chainSnapMisses{0};   // lecturas que no cuadraron
static bool chainSnapRead() {
    for (int intento = 0; intento < 16; ++intento) {
        const unsigned s1 = g_chainPub.seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;                         // escribiendo: a reintentar
        const int np = g_chainPub.npre, nq = g_chainPub.npost;
        int p[kChainMax], q[kChainMax];
        for (int i = 0; i < np; ++i) p[i] = g_chainPub.pre[i];
        for (int i = 0; i < nq; ++i) q[i] = g_chainPub.post[i];
        if (g_chainPub.seq.load(std::memory_order_acquire) != s1) continue;
        // Capacidad reservada en prepare: assign no reserva memoria (T13).
        g_preSnap.assign(p, p + np);
        g_postSnap.assign(q, q + nq);
        return true;
    }
    g_chainSnapMisses.fetch_add(1, std::memory_order_relaxed);
    return false;
}
static void rebuildKnobs();   // definida mas abajo (capa UI)
static bool chainHas(int t) {
    for (size_t i = 0; i < g_chainPre.size();  ++i) if (g_chainPre[i]  == t) return true;
    for (size_t i = 0; i < g_chainPost.size(); ++i) if (g_chainPost[i] == t) return true;
    return false;
}

static void selectCab(int model);   // definida mas abajo (cabes procedurales)

// --- Traza de depuracion del sistema de presets (solo con GA_PDBG=1) -------
// Escribe _presetdbg.log con lo que ocurre de verdad: clics en la lista,
// apertura del modal, teclas y aplicacion de presets. Sin la variable de
// entorno no hace absolutamente nada (ni abre el fichero), igual que GA_PROF.
static void pdbg(const char* fmt, ...) {
    static int on = -1;
    if (on < 0) {
        char b[4] = {};
        on = (GetEnvironmentVariableA("GA_PDBG", b, sizeof(b)) > 0 && b[0] == '1') ? 1 : 0;
    }
    if (!on) return;
    FILE* f = nullptr;
    if (fopen_s(&f, "_presetdbg.log", "a") == 0 && f) {
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fputc('\n', f); fclose(f);
    }
}

// Presets de fabrica: cada preset = UN amp real + su cadena + su cabina.
// El banco va por lotes: 6 presets por amplificador, seguidos (amp 0 slots
// 0..5, amp 1 slots 0..5, ...), de modo que el lote de un amp es un tramo
// contiguo. El nombre es ancho porque los presets de usuario lo son.
struct Preset {
    const WCHAR* name;
    int   model;                        // amp 0..11
    float amp[6];                       // GAIN BASS MID TREB VOL PRESENCE
    float fx[18][2];
    bool  fxOn[18];
    float master;
    int   cabModel, cabMic, cabDist;    // cabina del preset
    int   preCount;  int pre[6];
    int   postCount; int post[6];
};
// Banco generado por native/gen_presets.py (no editar a mano)
static const Preset g_presets[] = {
#include "amp_presets_data.h"
};
static const int kPresetsPerAmp = 6;
static const int kNumPresets = (int)(sizeof(g_presets) / sizeof(g_presets[0]));
static_assert(kNumPresets == 13 * kPresetsPerAmp, "el banco debe tener 6 lotes por amp");
static int presetAmp(int p) { return (p >= 0 && p < kNumPresets) ? g_presets[p].model : -1; }
static int   g_presetIdx = 0;


// Mascaras de favoritos de la lista: bit i = ampli i marcado con estrella.
// Se declara aqui porque saveIni/loadIni (muy mas abajo en el fichero) la
// persisten, y el corazon del pie refleja el bit del ampli cargado.
static DWORD g_favMask = 0;
// El favorito de un ampli: lo que marca la estrella de una fila de la lista del
// sidebar. Los PRESETS llevan su propio favorito (ver presetFav), que en el
// navegador es un PUNTO de color: cada lista marca lo suyo con su propio mando.
static bool ampFav(int a) { return (a >= 0 && a < 32) && (((g_favMask >> a) & 1u) != 0); }

// --- Presets de usuario (persistidos en GuitarAmp.ini) ---
// El tope existe porque cada preset ocupa su propia seccion del ini. Al llegar
// arriba no se rota ni se sobrescribe nada en silencio: la app avisa y toca
// borrar alguno.
static const int kMaxUserPresets = 32;
// Cuantos amplificadores hay. El 12 estaba escrito a mano en dos sitios y los dos
// se comian al PERFIL: la lista no lo mostraba y su fila salia sin miniatura
// (ampThumb cortaba en 12). Sube aqui porque el codigo de presets -que es lo
// primero que lo necesita- vive por encima de la tabla de nombres.
static const int kAmpCount = 13;
// Tope de la lista del navegador: TODOS los presets (fabrica + usuario) tienen
// que caber. Con 64 se quedaban fuera los presets de fabrica 65..78, que ni se
// pintaban ni se podian clicar (las flechas si los seleccionaban).
static const int kMaxListEntries = kNumPresets + kMaxUserPresets;
struct UserPreset {
    WCHAR name[64];        // nombre propio del usuario (renombrable)
    float amp[6];          // banco del modelo u.model
    float fx[18][2];
    bool  fxOn[18];
    float master;
    int   model;          // amp activo al guardar (-1 = sin amp)
    int   cabModel, cabMic, cabDist;
    int   preCount;  int pre[6];
    int   postCount; int post[6];
    int   favColor;       // color de favorito: 0 = ninguno, 1..4 (ver kFavColors)
    // --- Metadatos del navegador (paso 18) ---
    WCHAR author[32];     // quien lo hizo ("" = yo)
    WCHAR comment[160];   // la nota libre del preset
    unsigned int tags;    // mascara de caracter (ver kCharNames); 0 = sin etiqueta
    long long created;    // fecha unix de creacion (0 = desconocida)
    long long used;       // fecha unix del ultimo uso (0 = nunca)
};
static std::vector<UserPreset> g_userPresets;

static int presetTotal() { return kNumPresets + (int)g_userPresets.size(); }
static bool isUserPreset(int idx) { return idx >= kNumPresets && idx < presetTotal(); }
// --- Presets de TONE3000 dentro de la MISMA lista ---------------------------
//  La biblioteca LOCAL (.t3kpreset: los presets que el usuario ya tiene en su
//  TONE3000) ocupa el tramo de indices que empieza en presetTotal(). Asi el
//  navegador los trata como una entrada mas -filtros, orden, buscador e Info-
//  sin mantener dos listas en paralelo.
//  Dos reglas que no se pueden saltar:
//   - NO se guardan en el .ini: la biblioteca en disco es la que manda (si
//     borras el preset en TONE3000, desaparece de aqui al reabrir).
//   - NO pasan a ser g_presetIdx: al cargar uno, lo que suena deja de ser un
//     preset del banco, y eso es justo lo que marca el punto de MODIFICADO.
static const int kMaxT3kPresets = 1024;                      // tope de la lista
static const int kMaxListAll    = kMaxListEntries + kMaxT3kPresets;

// ---------------------------------------------------------------------------
//  BUSCADOR DE PRESETS DE LA CABECERA
//  La caja del preset (arriba) se despliega: escribe y la lista se filtra en
//  vivo por nombre, ampli, caracter y autor (lo mismo que busca el navegador).
//  La lista se filtra UNA vez por cambio de texto, no por frame: el despliegue
//  puede tener 370 presets.
// ---------------------------------------------------------------------------
static bool  g_topOpen = false;
static WCHAR g_topQuery[40] = L"";
static int   g_topSel = -1;          // fila resaltada DENTRO de la lista filtrada
static int   g_topScroll = 0;        // primera fila visible
static int   g_topList[kMaxListAll]; // indices de preset ya filtrados
static int   g_topCount = 0;
static int  t3kPresetCount()     { return (int)g_t3kLocal.size(); }
static int  presetTotalAll()     { return presetTotal() + t3kPresetCount(); }
static bool isT3kPreset(int idx) { return idx >= presetTotal() && idx < presetTotalAll(); }
static const t3k::LocalPreset* t3kPresetAt(int idx) {
    return isT3kPreset(idx) ? &g_t3kLocal[(size_t)(idx - presetTotal())] : nullptr;
}
// --- Favoritos de PRESET -----------------------------------------------------
// La estrella de la fila marca EL PRESET que hay en esa fila, no el ampli (esa es
// la de la lista de amplificadores del sidebar). Los 78 de fabrica van en un
// arreglo fijo porque su indice no se mueve nunca; los del usuario lo llevan en su
// propia ficha, porque su lista se recorta al borrar y un bit por indice se
// correria de preset.
// Los COLORES de favorito (paso 18): antes el favorito era un bool y la estrella
// encendida; ahora es un punto con color, como las colecciones de Guitar Rig.
// 0 = sin color; 1..4 = ambar, rojo, azul, verde (ver favColorAt).
static const int kFavColors = 5;
static unsigned char g_favPreset[kNumPresets] = {};
static bool g_favOnly = false;         // el filtro "solo mis amplis favoritos" del sidebar
static int presetFavColor(int idx) {
    if (isT3kPreset(idx)) return 0;               // los de TONE3000 no llevan color
    if (idx < 0 || idx >= presetTotal()) return 0;
    return (idx < kNumPresets) ? (int)g_favPreset[idx]
                               : g_userPresets[idx - kNumPresets].favColor;
}
static bool presetFav(int idx) { return presetFavColor(idx) != 0; }
static void presetFavSet(int idx, int color) {
    if (isT3kPreset(idx)) return;                 // no se guardan: no hay donde
    if (idx < 0 || idx >= presetTotal()) return;
    if (color < 0) color = 0;
    if (color > kFavColors - 1) color = kFavColors - 1;
    if (idx < kNumPresets) g_favPreset[idx] = (unsigned char)color;
    else                   g_userPresets[idx - kNumPresets].favColor = color;
}
// Amp (0..11) bajo el que se agrupa el preset; -1 = sin amp (grupo MIS PRESETS)
static int presetGroup(int idx) {
    if (isT3kPreset(idx)) return 12;              // un preset de TONE3000 es del PERFIL
    if (idx < 0 || idx >= presetTotal()) return -1;
    if (idx < kNumPresets) return g_presets[idx].model;
    const int m = g_userPresets[idx - kNumPresets].model;
    return (m >= 0 && m < 13) ? m : -1;
}
// Numero dentro del lote (1..6); 0 = preset de usuario
static int presetSlot(int idx) {
    if (idx < 0 || idx >= kNumPresets) return 0;
    return (idx % kPresetsPerAmp) + 1;
}
// El numero del preset DENTRO DE SU GRUPO (el ampli): 1..6 los seis lotes de
// fabrica y 7, 8, ... los presets del usuario montados en ese mismo ampli. Es
// el numero que ensenan la lista y la cabecera; antes la lista ensenaba el
// indice global ("31") y la cabecera "PRESET 31", que no son una posicion que
// nadie pueda buscar en ninguna parte. 0 = fuera de rango.
// Los seis de fabrica de un ampli son contiguos y en orden (lo fija el
// generador del banco y lo vigila la prueba Q0), de ahi el (idx % 6) + 1.
static int presetInGroup(int idx) {
    if (idx < 0 || idx >= presetTotalAll()) return 0;
    if (idx < kNumPresets) return (idx % kPresetsPerAmp) + 1;
    const int g = presetGroup(idx);
    int n = kPresetsPerAmp;                       // detras de los seis lotes
    for (int i = kNumPresets; i < idx; ++i)
        if (presetGroup(i) == g) ++n;
    return n + 1;
}

// --- Etiquetas y colores del navegador (paso 18) ----------------------------
// El CARACTER de un preset: el vocabulario fijo que filtran las etiquetas de la
// ventana, como los "Filter tags" de Guitar Rig. Los 78 de fabrica lo llevan por
// el lote que ocupan -el banco esta generado como seis caracteres por ampli:
// limpio, limpio, crunch, lead, ambiente y el cajon de sastre-; los del usuario
// lo eligen en el Info pane.
static const int kCharCount = 6;
static const WCHAR* kCharNames[kCharCount] = {
    L"LIMPIO", L"CRUNCH", L"LEAD", L"AMBIENTE", L"FUZZ", L"OTROS"
};
static unsigned char presetChar(int idx) {
    if (idx < 0 || idx >= kNumPresets) return 0;
    const int slot = idx % kPresetsPerAmp;
    if (slot == 5) {
        // El lote 6 no es uniforme: en unos amplis es un fuzz y en otros un
        // tremolo, un wah o un muro. 4 = FUZZ, 5 = OTROS, un caracter por ampli.
        static const unsigned char s5[kAmpCount] = { 4,4,5,5,4,5,4,5,4,5,5,5,5 };
        const int a = idx / kPresetsPerAmp;
        return (a >= 0 && a < kAmpCount) ? s5[a] : 5;
    }
    static const unsigned char bySlot[kPresetsPerAmp] = { 0,0,1,2,3,5 };
    return bySlot[slot];
}
// La mascara de caracter de un preset: un bit por caracter. De fabrica sale de
// su lote; el del usuario la lleva en su ficha (0 = sin etiqueta).
static unsigned int presetTags(int idx) {
    if (isT3kPreset(idx)) return 0;               // sin etiqueta de caracter
    if (idx < 0 || idx >= presetTotal()) return 0;
    if (idx < kNumPresets) return 1u << presetChar(idx);
    return g_userPresets[idx - kNumPresets].tags;
}
// El color del punto de favorito. kAcc para el ambar: el unico color "vivo" del
// lenguaje plano sigue siendo el mismo ambar de lo que suena.
static G::Color favColorAt(int c) {
    switch (c) {
        case 1: return kAcc;
        case 2: return G::Color(255, 0xff, 0x6b, 0x6b);   // rojo
        case 3: return G::Color(255, 0x7a, 0xd0, 0xff);   // azul
        case 4: return G::Color(255, 0x7a, 0xd9, 0x8a);   // verde
        default: return kFTxt3;
    }
}

// --- El ESTADO de un preset, ni mas ni menos ---------------------------------
// Lo que un preset guarda: el ampli, sus seis perillas, los 18 pedales (con su
// encendido), el master, la cabina y la cadena. Lo leen DOS cosas: la marca de
// MODIFICADO (se compara el rig de ahora con el que habia al aplicar el preset)
// y GUARDAR (se vuelca a un preset de usuario). Al salir de la misma lista, un
// sitio no puede contar cosas que el otro no guarde: el punto de MODIFICADO no
// puede quedarse encendido despues de guardar.
// NO entra la MEZCLA ni los dos controles extra del ampli: no son parte de
// ningun preset (no se guardan con el), asi que tocarlos no lo modifica.
struct RigState {
    int model;
    float amp[6];
    float fx[18][2];
    unsigned char fxOn[18];
    float master;
    int cabModel, cabMic, cabDist;
    int preCount,  pre[6];
    int postCount, post[6];
};
static RigState g_rigAtApply;      // el rig que habia al aplicar el preset
static void rigCapture(RigState& s) {
    memset(&s, 0, sizeof(s));      // tambien el relleno: la comparacion es un memcmp
    s.model = g_params.model.load();
    if (s.model < 0 || s.model >= 13) s.model = -1;
    if (s.model >= 0)
        for (int k = 0; k < 6; ++k) s.amp[k] = g_params.amp[s.model][k].load();
    for (int f = 0; f < 18; ++f) {
        s.fx[f][0] = g_params.fx[f][0].load();
        s.fx[f][1] = g_params.fx[f][1].load();
        s.fxOn[f]  = g_params.fxOn[f].load() ? 1 : 0;
    }
    s.master   = g_params.master.load();
    s.cabModel = g_params.cabModel.load();
    s.cabMic   = g_params.cabMic.load();
    s.cabDist  = g_params.cabDist.load();
    {
        std::lock_guard<std::mutex> lk(g_chainMutex);
        s.preCount = (int)g_chainPre.size();  if (s.preCount > 6)  s.preCount = 6;
        for (int i = 0; i < s.preCount; ++i)  s.pre[i]  = g_chainPre[i];
        s.postCount = (int)g_chainPost.size(); if (s.postCount > 6) s.postCount = 6;
        for (int i = 0; i < s.postCount; ++i) s.post[i] = g_chainPost[i];
    }
}
// Marca el rig actual como "el que era": al aplicar un preset y al guardarlo.
static void rigMark() { rigCapture(g_rigAtApply); }
// MODIFICADO: el rig ya no es el del preset que suena.
static bool presetDirty() {
    RigState now;
    rigCapture(now);
    return memcmp(&now, &g_rigAtApply, sizeof(RigState)) != 0;
}

// --- Fechas y metadatos de un preset (paso 18) ------------------------------
// Los de fabrica no se fechan (0): el orden RECIENTES deja los del usuario
// delante y los de fabrica detras, en su orden curado.
static long long nowUnix() { return (long long)time(nullptr); }
static long long presetUsed(int idx) {
    if (idx < 0 || idx >= presetTotal() || idx < kNumPresets) return 0;
    return g_userPresets[idx - kNumPresets].used;
}
static long long presetCreated(int idx) {
    if (idx < 0 || idx >= presetTotal() || idx < kNumPresets) return 0;
    return g_userPresets[idx - kNumPresets].created;
}
static const WCHAR* presetAuthor(int idx) {
    if (isT3kPreset(idx)) return L"TONE3000";
    if (idx < 0 || idx >= presetTotal() || idx < kNumPresets) return L"";
    return g_userPresets[idx - kNumPresets].author;
}
static const WCHAR* presetComment(int idx) {
    if (isT3kPreset(idx)) {
        // Lo util de un preset de TONE3000 es lo que trae: cuantas capturas y de
        // que tipo. Se compone aqui para que el Info pane lo ensene sin tocar
        // nada mas (el buffer es estatico: hay un solo panel).
        static WCHAR buf[160];
        const t3k::LocalPreset* p = t3kPresetAt(idx);
        const int nb = p ? (int)(p->left.size() + p->right.size()) : 0;
        swprintf_s(buf, L"%d captura%s \xB7 preset de TONE3000%s",
                   nb, nb == 1 ? L"" : L"s",
                   (p && p->stereo) ? L" \xB7 estereo" : L"");
        return buf;
    }
    if (idx < 0 || idx >= presetTotal() || idx < kNumPresets) return L"";
    return g_userPresets[idx - kNumPresets].comment;
}

static void applyPreset(int idx) {
    if (idx < 0 || idx >= presetTotal()) return;
    if (idx < kNumPresets) {
        pdbg("APPLY fabrica idx=%d amp=%d slot=%d master=%.0f cab=%d/%d/%d pre=%d post=%d",
             idx, g_presets[idx].model, presetSlot(idx), (double)g_presets[idx].master,
             g_presets[idx].cabModel, g_presets[idx].cabMic, g_presets[idx].cabDist,
             g_presets[idx].preCount, g_presets[idx].postCount);
        const Preset& p = g_presets[idx];
        if (p.model >= 0 && p.model < 13) {
            g_params.model.store(p.model);
            for (int k = 0; k < 6; ++k)
                g_params.amp[p.model][k].store(p.amp[k]);
            for (int i = 0; i < 2; ++i)   // controles extra: neutros de fabrica
                g_params.amp[p.model][6 + i].store(ampExtraNeutral(ampExtraKindAt(p.model, i)));
        }
        for (int f = 0; f < 18; ++f) {
            g_params.fx[f][0].store(p.fx[f][0]);
            g_params.fx[f][1].store(p.fx[f][1]);
            g_params.fxOn[f].store(p.fxOn[f]);
        }
        g_params.master.store(p.master);
        g_params.cabModel.store(p.cabModel);
        g_params.cabMic.store(p.cabMic);
        g_params.cabDist.store(p.cabDist);
        selectCab(p.cabModel);
        // Cadena del preset (bajo candado: el hilo de audio la lee por bloques)
        publishChain(std::vector<int>(p.pre, p.pre + p.preCount),
                     std::vector<int>(p.post, p.post + p.postCount));
        g_sceneDirty.store(true);   // hero/rack/knobs dependen del modelo
        rebuildKnobs();
        rigMark();                  // este es el rig del preset: no hay nada modificado
        return;
    }
    // Preset de usuario guardado por GUARDAR
    const UserPreset& u = g_userPresets[idx - kNumPresets];
    pdbg("APPLY usuario idx=%d amp=%d pre=%d post=%d", idx, u.model, u.preCount, u.postCount);
    if (u.model >= 0 && u.model < 13) {
        g_params.model.store(u.model);
        for (int k = 0; k < 6; ++k)
            g_params.amp[u.model][k].store(u.amp[k]);
    }
    for (int f = 0; f < 18; ++f) {
        g_params.fx[f][0].store(u.fx[f][0]);
        g_params.fx[f][1].store(u.fx[f][1]);
        g_params.fxOn[f].store(u.fxOn[f]);
    }
    g_params.master.store(u.master);
    g_params.cabModel.store(u.cabModel);
    g_params.cabMic.store(u.cabMic);
    g_params.cabDist.store(u.cabDist);
    selectCab(u.cabModel);
    publishChain(std::vector<int>(u.pre, u.pre + u.preCount),
                 std::vector<int>(u.post, u.post + u.postCount));
    g_sceneDirty.store(true);   // hero/rack/knobs dependen del modelo
    rebuildKnobs();
    g_userPresets[idx - kNumPresets].used = nowUnix();   // para el orden RECIENTES
    rigMark();                  // este es el rig del preset: no hay nada modificado
}

// --- Cargar en el PERFIL un preset de TONE3000 ------------------------------
//  Baja (o REUTILIZA, ver t3kWorkLoadPreset) las capturas del preset y las monta
//  en la cadena del cabezal PERFIL, en orden. No toca g_presetIdx a proposito:
//  lo que suena ya no es un preset del banco, y el punto de MODIFICADO lo dice.
static void applyT3kPreset(int idx)
{
    const int li = idx - presetTotal();
    if (li < 0 || li >= (int)g_t3kLocal.size()) return;
    const t3k::LocalPreset& lp = g_t3kLocal[(size_t)li];
    // OJO con el orden: primero se comprueba TODO y solo despues se toca el
    // cabezal. Cambiarlo a PERFIL sin capturas dejaba la app en silencio, que es
    // el "no me suena" de siempre.
    int libres = 0;
    { std::lock_guard<std::mutex> lk(g_namMutex); libres = namBlkVis() - (int)g_namUi.size(); }
    if (libres <= 0) {
        toast(L"LAS CELDAS ESTAN LLENAS: QUITA UNA CAPTURA ANTES");
        return;
    }
    // Si las capturas ya estan en perfiles/, no hace falta ni red ni sesion.
    int faltan = 0;
    {
        t3kProfilesDirInit();
        for (const t3k::PresetBlock& b : lp.left) {
            std::wstring base = u8w(t3kUrlTail(b.url));
            if (base.empty()) base = t3kSafeName(u8w(b.name));
            if (GetFileAttributesW((std::wstring(g_t3kProfilesDir) + L"\\" + base).c_str()) == INVALID_FILE_ATTRIBUTES)
                ++faltan;
        }
    }
    if (faltan > 0 && !g_t3kConnected) {
        g_t3kConnOpen = true;
        t3kStatusW(L"PARA CARGAR UN PRESET HAY QUE CONECTAR: SUS CAPTURAS SE DESCARGAN");
        return;
    }
    // Ya esta todo comprobado: ahora si, el cabezal pasa al PERFIL. Si no, las
    // capturas entrarian en la cadena y no sonarian.
    if (g_params.model.load() != 12) {
        g_params.model.store(12);
        rebuildKnobs();
    }
    toast(faltan == 0 ? L"CARGANDO EL PRESET DESDE TU LIBRERIA"
                      : L"BAJANDO LAS CAPTURAS DEL PRESET...");
    std::thread(t3kWorkLoadPreset, li, faltan == 0 ? std::string() : t3kTokenSnap()).detach();
    g_sceneDirty.store(true);
}

// "Aplicar lo que hay en esta fila del navegador": un preset del banco (fabrica o
// mio) o uno de TONE3000, que monta sus capturas en el PERFIL.
static void presetApplyFromBrowser(int idx) {
    if (isT3kPreset(idx)) { applyT3kPreset(idx); return; }
    applyPreset(idx);
}

static void presetNameW(int idx, WCHAR* out, int n) {
    if (!out || n <= 0) return;
    if (isT3kPreset(idx)) {
        const t3k::LocalPreset* p = t3kPresetAt(idx);
        const std::wstring wn = p ? u8w(p->name) : std::wstring();
        if (!wn.empty()) wcsncpy_s(out, (size_t)n, wn.c_str(), _TRUNCATE);
        else             swprintf_s(out, (size_t)n, L"PRESET TONE3000 %d", idx - presetTotal() + 1);
    } else if (idx >= 0 && idx < kNumPresets) {
        wcsncpy_s(out, (size_t)n, tr(g_presets[idx].name), _TRUNCATE);
    } else if (idx >= 0 && idx < presetTotal()) {
        wcsncpy_s(out, (size_t)n, g_userPresets[idx - kNumPresets].name, _TRUNCATE);
    } else {
        out[0] = 0;
    }
}

// ============================================================================
//  DSP: cadena completa del amplificador + efectos
// ============================================================================
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0, z1=0, z2=0;
    void reset() { z1=z2=0; }
    void set(float _b0,float _b1,float _b2,float _a1,float _a2) {
        b0=_b0; b1=_b1; b2=_b2; a1=_a1; a2=_a2;
    }
    inline float process(float x) {
        float y = b0*x + z1;
        z1 = b1*x - a1*y + z2;
        z2 = b2*x - a2*y;
        return y;
    }
};

static void normBq(Biquad& f, float b0,float b1,float b2,float a0,float a1,float a2) {
    f.set(b0/a0, b1/a0, b2/a0, a1/a0, a2/a0);
}

static void lowShelf(Biquad& f, float fs, float fc, float gainDb, float S=1.0f) {
    float A  = std::pow(10.0f, gainDb/40.0f);
    float w0 = 2.0f*(float)M_PI*fc/fs;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/2.0f * std::sqrt((A + 1.0f/A)*(1.0f/S - 1.0f) + 2.0f);
    float tA = 2.0f*std::sqrt(A)*alpha;
    normBq(f, A*((A+1)-(A-1)*cw + tA),  2*A*((A-1)-(A+1)*cw),      A*((A+1)-(A-1)*cw - tA),
               (A+1)+(A-1)*cw + tA,     -2*((A-1)+(A+1)*cw),        (A+1)+(A-1)*cw - tA);
}

static void highShelf(Biquad& f, float fs, float fc, float gainDb, float S=1.0f) {
    float A  = std::pow(10.0f, gainDb/40.0f);
    float w0 = 2.0f*(float)M_PI*fc/fs;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/2.0f * std::sqrt((A + 1.0f/A)*(1.0f/S - 1.0f) + 2.0f);
    float tA = 2.0f*std::sqrt(A)*alpha;
    normBq(f, A*((A+1)+(A-1)*cw + tA), -2*A*((A-1)+(A+1)*cw),      A*((A+1)+(A-1)*cw - tA),
               (A+1)-(A-1)*cw + tA,      2*((A-1)-(A+1)*cw),        (A+1)-(A-1)*cw - tA);
}

static void peak(Biquad& f, float fs, float fc, float gainDb, float Q) {
    float A  = std::pow(10.0f, gainDb/40.0f);
    float w0 = 2.0f*(float)M_PI*fc/fs;
    float alpha = std::sin(w0)/(2.0f*Q);
    float cw = std::cos(w0);
    normBq(f, 1+alpha*A, -2*cw, 1-alpha*A, 1+alpha/A, -2*cw, 1-alpha/A);
}

static void lowpass(Biquad& f, float fs, float fc, float Q = 0.707f) {
    float w0 = 2.0f*(float)M_PI*fc/fs;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/(2.0f*Q);
    normBq(f, (1-cw)/2, 1-cw, (1-cw)/2, 1+alpha, -2*cw, 1-alpha);
}

static void highpass(Biquad& f, float fs, float fc, float Q = 0.707f) {
    float w0 = 2.0f*(float)M_PI*fc/fs;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/(2.0f*Q);
    normBq(f, (1+cw)/2, -(1+cw), (1+cw)/2, 1+alpha, -2*cw, 1-alpha);
}
static void bandpass(Biquad& f, float fs, float fc, float Q) {
    float w0 = 2.0f*(float)M_PI*fc/fs;
    float cw = std::cos(w0), sw = std::sin(w0);
    float alpha = sw/(2.0f*Q);
    normBq(f, alpha, 0.0f, -alpha, 1.0f+alpha, -2*cw, 1.0f-alpha);
}

// Bloqueo de DC de 1er orden (8 Hz). Imprescindible tras una etapa asimetrica:
// el offset que genera el bias entra en la etapa siguiente y produce distorsion
// estatica de 2o orden incluso a niveles bajisimos (sonaba "siempre roto").
struct DcBlock {
    float x1 = 0.0f, y1 = 0.0f, R = 0.998f;
    void prepare(float fs) { R = 1.0f - 2.0f*3.14159265358979f*8.0f/fs; x1 = y1 = 0.0f; }
    void reset() { x1 = y1 = 0.0f; }
    inline float process(float x) { const float y = x - x1 + R*y1; x1 = x; y1 = y; return y; }
};

// Sobremuestreo 4x: dos etapas de 2x (relleno de ceros + LP Butterworth 4o) y
// decimacion con filtro real. Antes el "2x" era un promedio de dos muestras
// (interpolacion lineal), que deja pasar las imagenes y mete aliasing al recortar.
struct Os4 {
    Biquad u1a, u1b, u2a, u2b, d1a, d1b, d2a, d2b;
    void prepare(float fs) {
        // El corte va a 0,46 del Nyquist de ENTRADA de cada etapa, o sea 0,23
        // del de SALIDA. Antes estaba a 0,46 del Nyquist de salida (= 0,92 del
        // de entrada), pegado al borde del plegado: un Butterworth de 4o orden
        // solo atenuaba -4,7 dB donde empieza el plegado, asi que el 4x casi no
        // filtraba. Medido con el autotest (rechazo a 30 kHz): -18,5 -> -67 dB.
        // La banda audible no se mueve: preLp y el IR de cabina ya caen a 5-8 kHz.
        const float f2 = fs*2.0f, f4 = fs*4.0f, c2 = fs*0.23f, c4 = fs*0.46f;
        lowpass(u1a, f2, c2, 0.5412f); lowpass(u1b, f2, c2, 1.3066f);
        lowpass(u2a, f4, c4, 0.5412f); lowpass(u2b, f4, c4, 1.3066f);
        lowpass(d1a, f4, c4, 0.5412f); lowpass(d1b, f4, c4, 1.3066f);
        lowpass(d2a, f2, c2, 0.5412f); lowpass(d2b, f2, c2, 1.3066f);
    }
    void reset() { u1a.reset(); u1b.reset(); u2a.reset(); u2b.reset();
                   d1a.reset(); d1b.reset(); d2a.reset(); d2b.reset(); }
    inline void up(float x, float* o) {
        const float a = u1b.process(u1a.process(x*2.0f));
        const float b = u1b.process(u1a.process(0.0f));
        o[0] = u2b.process(u2a.process(a*2.0f));
        o[1] = u2b.process(u2a.process(0.0f));
        o[2] = u2b.process(u2a.process(b*2.0f));
        o[3] = u2b.process(u2a.process(0.0f));
    }
    inline float down(const float* o) {
        const float p0 = d1b.process(d1a.process(o[0]));
        const float p1 = d1b.process(d1a.process(o[1]));
        const float p2 = d1b.process(d1a.process(o[2]));
        const float p3 = d1b.process(d1a.process(o[3]));
        (void)p1;
        d2b.process(d2a.process(p0));          // primer par: se descarta al diezmar
        return d2b.process(d2a.process(p2));   // segundo par: muestra valida
        (void)p3;
    }
};

// Preamp de dos etapas de tubo en cascada (como un amplificador real):
//  - Ganancia unitaria a senal pequena: el ampli TOCA LIMPIO a bajo nivel y
//    rompe de forma progresiva al subir (antes el bias solo desplazaba la
//    salida, no asimetrizaba el recorte).
//  - Bias real por etapa -> armonicos pares; DC bloqueada tras cada etapa.
//  - La ganancia de cada etapa se conserva igual que antes (h/tanh(h)) para no
//    re-voicar: el cambio es de headroom y de dinamica, no de timbre base.
struct TubeStage {
    float g1 = 1.0f, g2 = 1.0f, h1 = 1.0f, h2 = 1.0f;
    float b1 = 0.0f, b2 = 0.0f, k1 = 1.0f, k2 = 1.0f;
    float trim = 1.0f;
    Biquad couplerHP;
    DcBlock dc1, dc2;

    void configure(float d, float b, float h, float fs, int model, float coupF) {
        // Escalado por voz IDENTICO al diseÃ±o anterior (d = drive de la voz),
        // corregido por h/tanh(h): la pendiente a seÃ±al pequeÃ±a de la etapa
        // nueva es 1, la del tanh(x*h)/n antiguo era h/tanh(h). AsÃ­ el gain
        // staging de cada voz se conserva exactamente.
        const float gs = h/(std::tanh(h) + 1.0e-9f);
        g1 = (1.0f + d*d*6.0f)*gs;
        g2 = (1.0f + d*d*14.0f)*gs;
        h1 = h; h2 = h*0.98f;
        b1 = b*1.6f; b2 = b*2.6f;              // 2a etapa mas desviada (mas pares)
        // normalizacion: pendiente 1 en el punto de reposo (x=0)
        k1 = 1.0f/(h1*(1.0f - std::tanh(b1*h1)*std::tanh(b1*h1)));
        k2 = 1.0f/(h2*(1.0f - std::tanh(b2*h2)*std::tanh(b2*h2)));
        highpass(couplerHP, fs, coupF, 0.707f);   // acoplamiento entre etapas (por voz)
        dc1.prepare(fs); dc2.prepare(fs);
        if (model == 0)      trim = 1.00f;   // clean
        else if (model == 1) trim = 0.80f;   // crunch
        else                 trim = 0.62f;   // dist
    }
    inline float stg(float x, float g, float h, float b, float k) const {
        const float t = x*g*h + b*h;
        return (std::tanh(t) - std::tanh(b*h))*k;
    }
    inline float process(float x) {
        float s1 = stg(x, g1, h1, b1, k1);
        s1 = dc1.process(s1);
        s1 = couplerHP.process(s1);
        float s2 = stg(s1, g2, h2, b2, k2);
        s2 = dc2.process(s2);
        return s2*trim;
    }
};

// Puerta de ruido
struct NoiseGate {
    float env = 0.0f;
    float atkCoef = 0.0f, relCoef = 0.0f;
    void prepare(float fs) {
        atkCoef = std::exp(-1.0f/(0.002f*fs));
        relCoef = std::exp(-1.0f/(0.080f*fs));
        env = 0.0f;     // sin esto, un NaN queda en la envolvente para siempre
    }
    inline float process(float x, float thrDb, float ratio, bool enabled) {
        float ax = std::fabs(x);
        env = (ax > env) ? atkCoef*env + (1.0f-atkCoef)*ax
                         : relCoef*env + (1.0f-relCoef)*ax;
        if (!enabled) { g_grGateDb.store(0.0f); return x; }
        float envDb = 20.0f*std::log10(env + 1.0e-9f);
        if (envDb >= thrDb) { g_grGateDb.store(0.0f); return x; }
        float over    = thrDb - envDb;
        float attenDb = -over*(ratio - 1.0f);
        if (attenDb < -80.0f) attenDb = -80.0f;
        g_grGateDb.store(attenDb);   // visible en la UI como medidor GATE
        return x * std::pow(10.0f, attenDb/20.0f);
    }
};

// Compresor feed-forward con envelope
struct Compressor {
    float env = 0.0f, fs = 48000.0f;
    void prepare(float f) { fs = f; env = 0.0f; }
    inline float process(float x, float sustain, float level) {
        float ax = std::fabs(x);
        float aA = std::exp(-1.0f/(0.003f*fs));
        float aR = std::exp(-1.0f/(0.080f*fs));
        env = (ax > env) ? aA*env + (1.0f-aA)*ax : aR*env + (1.0f-aR)*ax;
        float envDb  = 20.0f*std::log10(env + 1.0e-9f);
        float thrDb  = -24.0f + sustain*0.12f;        // -24..-12 dB
        float ratio  = 2.0f + sustain*0.06f;          // 2:1 .. 8:1
        float overDb = envDb - thrDb;
        float grDb   = (overDb > 0.0f) ? overDb*(1.0f - 1.0f/ratio) : 0.0f;
        float makeup = level*0.08f;                   // 0..+8 dB
        return x * std::pow(10.0f, (makeup - grDb)*0.05f);
    }
};

// Drive: waveshaper + filtro de tono
struct DriveStage {
    float lpState = 0.0f, fs = 48000.0f;
    void prepare(float f) { fs = f; lpState = 0.0f; }
    inline float process(float x, float drive, float tone) {
        float g = 1.0f + drive*0.35f;
        float y = std::tanh(x*g);
        float fc = 700.0f + tone*5300.0f;
        float a  = 1.0f - std::exp(-2.0f*(float)M_PI*fc/fs);
        lpState += a*(y - lpState);
        return lpState*1.2f;
    }
};

// Modulation (chorus): delay modulado por LFO
struct Chorus {
    std::vector<float> buf;
    size_t w = 0;
    float phase = 0.0f, fs = 48000.0f;
    void prepare(float f) {
        fs = f;
        buf.assign((size_t)(fs*0.05f) + 8, 0.0f);
        w = 0; phase = 0.0f;
    }
    inline float process(float x, float rate, float depth) {
        if (buf.empty()) return x;
        const size_t N = buf.size();
        buf[w] = x;
        float hz = 0.15f + rate*0.0485f;              // 0.15..5 Hz
        phase += hz/fs;
        if (phase >= 1.0f) phase -= 1.0f;
        float depth01 = depth*0.01f;                  // knob 0..100 -> 0..1
        float base = 0.012f*fs;                       // 12 ms
        float mod  = depth01*0.0025f*fs*std::sin(2.0f*(float)M_PI*phase); // +-2.5 ms
        float rp   = (float)w - (base + mod);
        // Normalizar el puntero de lectura al buffer (siempre 0..N-1)
        rp = std::fmod(rp, (float)N);
        if (rp < 0.0f) rp += (float)N;
        size_t i0 = (size_t)rp;
        if (i0 >= N) i0 = N - 1;
        size_t i1 = (i0 + 1) % N;
        float fr  = rp - (float)i0;                   // parte fraccionaria 0..1
        float d   = buf[i0]*(1.0f - fr) + buf[i1]*fr;
        w = (w + 1) % N;
        return x*0.65f + d*0.55f;
    }
};

// Delay con feedback
struct DelayFX {
    std::vector<float> buf;
    size_t w = 0;
    float fs = 48000.0f;
    void prepare(float f) {
        fs = f;
        buf.assign((size_t)(fs*1.5f) + 16, 0.0f);
        w = 0;
    }
    inline float process(float x, float timeMs, float fb) {
        if (buf.empty()) return x;
        const size_t N = buf.size();
        float sp = timeMs*0.001f*fs;
        if (sp < 1.0f) sp = 1.0f;
        if (sp > (float)(N - 2)) sp = (float)(N - 2);
        size_t rp = (w + N - (size_t)sp) % N;
        float d = buf[rp];
        float f = fb; if (f > 0.78f) f = 0.78f;
        buf[w] = x + d*f;
        w = (w + 1) % N;
        return x*0.55f + d*0.55f;
    }
};

// Reverb Schroeder (4 comb + 2 allpass)
struct Reverb {
    static const int NC = 4, NA = 2;
    std::vector<float> cbuf[NC]; size_t cw[NC] = {};
    std::vector<float> abuf[NA]; size_t aw[NA] = {};
    float fs = 48000.0f;
    void prepare(float f) {
        fs = f;
        const float cm[NC] = { 29.7f, 37.1f, 41.1f, 43.7f };
        for (int i = 0; i < NC; ++i) {
            cbuf[i].assign((size_t)(fs*cm[i]*0.001f) + 4, 0.0f);
            cw[i] = 0;
        }
        const float am[NA] = { 5.0f, 1.7f };
        for (int i = 0; i < NA; ++i) {
            abuf[i].assign((size_t)(fs*am[i]*0.001f) + 4, 0.0f);
            aw[i] = 0;
        }
    }
    inline float process(float x, float size, float mix01) {
        float fb = 0.70f + size*0.0026f;              // 0.70..0.96
        float acc = 0.0f;
        for (int i = 0; i < NC; ++i) {
            size_t n = cbuf[i].size(); if (n < 4) continue;
            float y = cbuf[i][cw[i]];
            cbuf[i][cw[i]] = x + y*fb;
            cw[i] = (cw[i] + 1) % n;
            acc += y;
        }
        acc *= 0.25f;
        for (int i = 0; i < NA; ++i) {
            size_t n = abuf[i].size(); if (n < 4) continue;
            float y  = abuf[i][aw[i]];
            float gp = 0.5f;
            float xn = acc + y*gp;
            abuf[i][aw[i]] = xn;
            aw[i] = (aw[i] + 1) % n;
            acc = y - gp*xn;
        }
        float wet = mix01*0.8f;
        return x*(1.0f - wet*0.6f) + acc*wet;
    }
};

// ============================================================================
//  CAB CON IR (.wav): convolucion overlap-save + FFT propia (sin dependencias)
// ============================================================================
static const float kPi = 3.14159265358979f;

// Filtro de 1er orden todo-paso (phaser); fc en Hz
struct Allpass1 {
    float a = 0.0f, z = 0.0f;
    void  setFc(float fc, float sr) {
        const float th = std::tan(kPi*fc/sr);
        a = (th - 1.0f)/(th + 1.0f);
    }
    inline float process(float x) {
        const float y = -a*x + z;
        z = x + a*y;
        return y;
    }
};

// FFT radix-2 in-place (n potencia de 2). inverse=true -> senal * 1/n.
static void fftRadix2(std::vector<float>& re, std::vector<float>& im, bool inverse) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? 2.0f : -2.0f)*kPi/(float)len;
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (size_t k = 0; k < len/2; ++k) {
                const float ur = re[i+k],      ui = im[i+k];
                const float vr = re[i+k+len/2]*cr - im[i+k+len/2]*ci;
                const float vi = re[i+k+len/2]*ci + im[i+k+len/2]*cr;
                re[i+k] = ur + vr; im[i+k] = ui + vi;
                re[i+k+len/2] = ur - vr; im[i+k+len/2] = ui - vi;
                const float nr = cr*wr - ci*wi;
                ci = cr*wi + ci*wr; cr = nr;
            }
        }
    }
    if (inverse) {
        const float s = 1.0f/(float)n;
        for (size_t i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
    }
}

// Impulso normalizado listo para convolucionar (N=1024, BLOCK=128)
struct CabIR {
    static const int N = 1024;
    static const int BLOCK = 128;
    std::vector<float> fr, fi;    // espectro del IR
    std::vector<float> hist;      // N-BLOCK = 896 muestras historicas
    char nameW[160] = "";         // nombre del archivo (para la UI)
    bool owned = false;           // true solo si lo reservo loadIRFromWav con new
};

// Convolucion overlap-save en streaming sobre el IR activo (latencia BLOCK ~2.7 ms)
struct CabConv {
    CabIR*             irCur = nullptr;  // con quien se reseteo por ultima vez
    std::vector<float> hist;             // N-BLOCK
    std::vector<float> irRe, irIm;       // espectro del IR (constante por sesion)
    std::vector<float> fRe, fIm, oRe, oIm;  // scratch FFT
    std::vector<float> inF, outF;        // entrada (BLOCK) + 2 bloques de salida
    int inN = 0;                          // entradas acumuladas del bloque
    int outIdx = 0, blk = 0;              // lectura del bloque en curso
    void ensureFor(CabIR* ir) {
        if (!ir || irCur == ir) return;     // tamanyos constantes: reset perezoso
        irCur = ir;
        const int NB = CabIR::BLOCK, N = CabIR::N;
        hist.assign(N - NB, 0.0f);
        irRe = ir->fr; irIm = ir->fi;
        fRe.assign(N, 0.0f); fIm.assign(N, 0.0f);
        oRe.assign(N, 0.0f); oIm.assign(N, 0.0f);
        inF.assign(NB, 0.0f); outF.assign(NB*2, 0.0f);
        inN = 0; outIdx = 0; blk = 0;
    }
    // Reserva los buffers UNA vez (los tamaÃ±os son fijos: N y BLOCK). A partir de
    // aqui ensureFor() solo copia y limpia: cambiar de cabina o cargar otro IR no
    // reserva memoria en el hilo de audio (T16), que es cuando mas duele, porque
    // el cambio lo pide la interfaz mientras se esta tocando.
    void prepare() {
        const int NB = CabIR::BLOCK, N = CabIR::N;
        hist.assign((size_t)(N - NB), 0.0f);
        irRe.assign((size_t)N, 0.0f); irIm.assign((size_t)N, 0.0f);
        fRe.assign((size_t)N, 0.0f);  fIm.assign((size_t)N, 0.0f);
        oRe.assign((size_t)N, 0.0f);  oIm.assign((size_t)N, 0.0f);
        inF.assign((size_t)NB, 0.0f); outF.assign((size_t)(NB*2), 0.0f);
        irCur = nullptr;
        inN = 0; outIdx = 0; blk = 0;
    }
    // Overlap-save: escribe el bloque valido (NB muestras) en dst
    inline void processBlockTo(float* dst) {
        const int NB = CabIR::BLOCK, N = CabIR::N, tail = N - NB;
        std::fill(fRe.begin(), fRe.end(), 0.0f);
        std::fill(fIm.begin(), fIm.end(), 0.0f);
        for (int i = 0; i < tail; ++i) fRe[i] = hist[i];
        for (int i = 0; i < NB;   ++i) fRe[tail + i] = inF[i];
        fftRadix2(fRe, fIm, false);
        for (int i = 0; i < N; ++i) {
            oRe[i] = fRe[i]*irRe[i] - fIm[i]*irIm[i];
            oIm[i] = fRe[i]*irIm[i] + fIm[i]*irRe[i];
        }
        fftRadix2(oRe, oIm, true);
        // historial <- la ENTRADA del bloque
        std::copy(hist.begin() + NB, hist.end(), hist.begin());
        for (int i = 0; i < NB; ++i) hist[tail - NB + i] = inF[i];
        // salida valida (overlap-save)
        for (int i = 0; i < NB; ++i) dst[i] = oRe[tail + i];
    }
    // Latencia = 1 bloque (2.7 ms). Doble buffer: mientras se drena el bloque
    // en curso, el recien calculado se escribe en el otro. El FIFO anterior
    // soltaba muestras de entrada al drenar salida (consumia 1 de cada 128
    // muestras): eso era el "timbre roto" que se atribuia a los IRs.
    inline float process(float x) {
        if (!irCur || irRe.empty()) return x;
        const int NB = CabIR::BLOCK;
        inF[inN++] = x;
        if (inN >= NB) { processBlockTo(outF.data() + (1 - blk)*NB); inN = 0; }
        const float y = outF[blk*NB + outIdx];
        if (++outIdx >= NB) { outIdx = 0; blk = 1 - blk; }
        return y;
    }
};

// IR activo: el hilo UI lo sustituye; el audio solo lee (swap atomico)
static std::atomic<CabIR*> g_cabIRPtr{nullptr};
static std::vector<CabIR*> g_cabRetirados;   // borrado diferido en el hilo UI
static std::wstring        g_cabIRPath;      // ruta del IR (persistencia + UI)
static CabConv             g_cabConv;
static CabIR g_procIR[6];                 // 6 cabes procedurales (generados al arrancar el audio)
static bool  g_procIRBuilt = false;
static float g_procIRFs = 0.0f;
static CabIR* g_userIRPtr = nullptr;      // ultimo IR cargado por el usuario
// Tasa a la que esta construido el IR de usuario. La convolucion supone esa
// frecuencia: si la sesion cambia (44,1 -> 48) hay que REHACERLO o el gabinete
// queda desafinado un 8,8%.
static std::atomic<int> g_irBuiltRate{0};
// g_procIR / g_procIR2 viven en tablas estaticas: NUNCA se les puede hacer
// delete. La marca esta en CabIR::owned, que solo pone loadIRFromWav.

// Carga un IR desde un WAV (PCM 8/16/24/32 o float 32/64, mono o estereo) y lo publica.
static bool loadIRFromWav(const wchar_t* path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return false;
    auto rd = [&](void* b, size_t n) { return fread(b, 1, n, f) == n; };
    char id[4] = {};
    uint32_t sz = 0;
    if (!rd(id, 4) || memcmp(id, "RIFF", 4) != 0 || !rd(&sz, 4) ||
        !rd(id, 4) || memcmp(id, "WAVE", 4) != 0) { fclose(f); return false; }
    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<char> data;
    bool gotFmt = false;
    while (fread(id, 1, 4, f) == 4) {
        uint32_t csz = 0; if (!rd(&csz, 4)) break;
        if (memcmp(id, "fmt ", 4) == 0 && csz >= 16) {
            uint16_t blockAlign = 0;
            uint32_t byteRate = 0;
            // Orden EXACTO del chunk fmt: format, channels, sampleRate,
            // avgBytesPerSec, blockAlign, bitsPerSample (16 bytes en PCM).
            // Antes no se leia avgBytesPerSec, asi que blockAlign y bits
            // salian desplazados 4 bytes y TODO WAV estandar se rechazaba.
            if (!rd(&fmt, 2) || !rd(&ch, 2) || !rd(&rate, 4) ||
                !rd(&byteRate, 4) || !rd(&blockAlign, 2) || !rd(&bits, 2)) { fclose(f); return false; }
            if (csz > 16) fseek(f, (long)(csz - 16), SEEK_CUR);
            gotFmt = true;
            (void)byteRate; (void)blockAlign;
        } else if (memcmp(id, "data", 4) == 0) {
            if (csz > 64u*1024u*1024u) { fclose(f); return false; }   // tope de prudencia
            data.resize(csz);
            if (csz && !rd(data.data(), csz)) { fclose(f); return false; }
            break;
        } else {
            fseek(f, (long)csz, SEEK_CUR);
        }
    }
    fclose(f);
    if (!gotFmt || data.empty() || ch == 0 || rate == 0) return false;
    if (fmt == 0xFFFE) fmt = (bits == 32) ? 3 : 1;   // EXTENSIBLE -> PCM/float segun bits
    if (fmt != 1 && fmt != 3) return false;
    const size_t bytesPer = (bits == 24) ? 3 : (size_t)(bits / 8);
    if (bytesPer == 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32 && bits != 64)) return false;
    const size_t frames = data.size() / (bytesPer * ch);
    if (frames < 64) return false;
    // decodizar a float mono
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (int c = 0; c < ch; ++c) {
            const unsigned char* q = (const unsigned char*)data.data() + (i*ch + c)*bytesPer;
            float v = 0.0f;
            if (fmt == 3 && bits == 32) { float fv;  memcpy(&fv, q, 4); v = fv; }
            else if (fmt == 3 && bits == 64) { double dv; memcpy(&dv, q, 8); v = (float)dv; }
            else if (bits == 32) { int32_t iv; memcpy(&iv, q, 4); v = (float)iv / 2147483648.0f; }
            else if (bits == 16) { int16_t iv; memcpy(&iv, q, 2); v = (float)iv / 32768.0f; }
            else if (bits == 24) {
                const int32_t iv = (int32_t)(((uint32_t)q[2] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[0] << 8));
                v = (float)(iv >> 8) / 8388608.0f;
            }
            else if (bits == 8)  { v = ((float)q[0] - 128.0f) / 128.0f; }
            acc += v;
        }
        mono[i] = acc / (float)ch;
    }
    // El IR declara su frecuencia en el WAV. Si no es la de la sesion se
    // re-muestrea (lineal: es un impulso de 512 tomas), porque convolucionarlo
    // tal cual desplazaba todo el gabinete por la relacion de tasas (48/44,1 =
    // 8,8%: el corte de 5 kHz caia en 4,6 kHz).
    int sessRate = 48000;
    {
        { std::lock_guard<std::mutex> lk(g_settingsMutex); sessRate = kRates[g_appliedSet.rateIdx]; }
        if (rate > 0 && (int)rate != sessRate) {
            const double k = (double)rate/(double)sessRate;
            const size_t n2 = (size_t)((double)frames/k) + 1;
            std::vector<float> rs(n2, 0.0f);
            for (size_t i = 0; i < n2; ++i) {
                const double sp = (double)i*k;
                const size_t i0 = (size_t)sp;
                const double fr = sp - (double)i0;
                const float  a = (i0 < frames)     ? mono[i0]     : 0.0f;
                const float  b = (i0 + 1 < frames) ? mono[i0 + 1] : 0.0f;
                rs[i] = (float)((double)a + ((double)b - (double)a)*fr);
            }
            mono.swap(rs);
        }
    }
    // truncar a 512 muestras con fade-out, normalizar a ~-1 dBFS, rellenar a 1024
    float peak = 1e-6f;
    for (size_t i = 0; i < mono.size(); ++i) peak = std::max(peak, std::fabs(mono[i]));
    const float gain = 1.0f / peak;
    CabIR* ir = new CabIR();
    ir->owned = true;                       // reservado aqui: solo esto se puede delete
    ir->fr.assign(CabIR::N, 0.0f); ir->fi.assign(CabIR::N, 0.0f);
    ir->hist.assign(CabIR::BLOCK*(CabIR::N/CabIR::BLOCK - 1), 0.0f);
    const size_t keep = std::min<size_t>(mono.size(), 512);
    for (size_t i = 0; i < (size_t)CabIR::N; ++i) {
        float s = 0.0f;
        if (i < keep) {
            s = mono[i]*gain;
            if (i >= keep - 64) {                       // fade-out final anti-clic
                const float ft = (float)(keep - i)/64.0f;
                s *= ft*ft;
            }
        }
        ir->fr[i] = s;
    }
    // Normalizacion por ENERGIA (no por pico): la convolucion debe conservar el
    // nivel RMS de la senal (el pico a 0.891 hacia el limitador final con
    // clipping constante y aliasing). Tope de prudencia anti-pico.
    {
        double e = 1.0e-12;
        for (int i = 0; i < CabIR::N; ++i) e += (double)ir->fr[i]*(double)ir->fr[i];
        float g = 0.85f/(float)std::sqrt(e);
        float mx = 0.0f;
        for (int i = 0; i < CabIR::N; ++i) {
            ir->fr[i] *= g;
            const float a2 = std::fabs(ir->fr[i]);
            if (a2 > mx) mx = a2;
        }
        if (mx > 0.95f) {
            const float g2 = 0.95f/mx;
            for (int i = 0; i < CabIR::N; ++i) ir->fr[i] *= g2;
        }
    }
    fftRadix2(ir->fr, ir->fi, false);
    {   // nombre corto del archivo (para la UI)
        const wchar_t* bs = wcsrchr(path, L'\\');
        const wchar_t* nm = bs ? bs + 1 : path;
        WideCharToMultiByte(CP_ACP, 0, nm, -1, ir->nameW, (int)sizeof(ir->nameW), nullptr, nullptr);
    }
    g_irBuiltRate.store(sessRate);           // tasa a la que se ha remuestreado
CabIR* old = g_cabIRPtr.exchange(ir);   // el audio pasara a leerlo en su proximo bloque
    if (old && old->owned) g_cabRetirados.push_back(old);   // retira solo IRs reservados aqui
    g_userIRPtr = ir;
    g_cabIRPath = path;
    g_params.cabModel.store(6);             // el cargador selecciona el modo USER IR
    return true;
}

// ============================================================================
//  Motor de amplificador parametrizado: 12 voces sobre el mismo esqueleto.
//  Cada voz = tube stages + tone stack + presencia + bright cap + sag +
//  etapa de potencia + resonancia de la carga del altavoz.
// ============================================================================
struct AmpVoice {
    const char* name;
    float tD, tB, tH; int tTrim;       // TubeStage::configure(d, bias, hardness, fs, trim-model)
    float inCut;                        // HP de entrada
    float lp2;                          // LP interetapa (2a etapa)
    float stage2;                       // asimetria de la 2a etapa (0 = sin etapa extra)
    float bassF, bassSc;                // tono stack: low shelf
    float midF, midQ, midSc, midBias;   // tono stack: peak parametrico
    float trebF, trebSc;                // tono stack: high shelf
    float bright;                       // bright cap 0..1
    float sag;                          // sag de potencia 0..1
    float pDrive;                       // etapa de potencia
    float resoF, resoQ, resoDb, lpCab;  // resonancia de la carga (fallback biquad)
    float d[6];                         // GAIN BASS MID TREB VOL PRESENCE (fabrica)
    int cab, mic, dist;                 // cabina sugerida (0..5, 0..2, 0..2)
};
// Nivel percibido igualado entre voces (dB). Medido con el harness offline
// (_dsp_test.exe wav) sobre un DI sintetico: sin esto, cambiar de amplificador
// saltaba hasta 22 dB de volumen (CHORUS 120 vs LEAD 1959). Los competidores
// nivelan sus modelos; aqui igual se mantiene A/B honesto.
// RE-MEDIDO: con la tabla anterior DUO RECTO y KRAFT V4 quedaban 10,5 dB por
// debajo del resto (rms 0,071 y 0,069 contra 0,19-0,26 de los demas: cambiar de
// HAVOC 100 a KRAFT V4 era un bache de nivel). Estos valores dejan los doce
// dentro de +-0,3 dB y los picos por debajo del limitador de master.
static const float kVoicesTrimDb[13] = {
     6.8f, -0.1f, -3.4f, 12.4f, -6.7f, -7.7f, -6.1f, -3.4f, -5.3f, 5.5f, 4.8f, -5.1f,
     0.0f
};   // (medido con _dsp_test.exe wav: los doce quedan en rms 0,194-0,200, o sea
     // dentro de +-0,15 dB; el ultimo, PERFIL, no se toca: su nivel sale de los
     // metadatos del .nam)
// Acoplamiento entre etapas (Hz). Es el paso alto que en un ampli de verdad
// pone el condensador de acoplamiento, y su valor NO es el mismo en todos:
//   - en los limpios/bajos de ganancia va bajo (20-25 Hz): ahi el condensador
//     solo tiene que quitar la continua, y con 130 Hz -lo que habia- el ampli
//     se quedaba sin graves (medido en SPARKLE TWIN: -42 dB a 40 Hz respecto a
//     1 kHz, cuando un Twin real esta casi plano a partir de 80 Hz);
//   - en los de alta ganancia va alto (100-130 Hz) A PROPOSITO: es el truco de
//     siempre para que el pastoso no se coma el headroom del recorte. Ahi se
//     queda como estaba.
static const float kVoicesCoupling[13] = {
     20.0f, 22.0f, 24.0f, 20.0f, 70.0f, 95.0f, 55.0f, 24.0f, 40.0f, 120.0f, 110.0f, 130.0f,
     20.0f
};

static const AmpVoice kVoices[13] = {
    { "SPARKLE TWIN",    0.15f,0.012f,0.62f,0, 80.0f,7000.0f,0.00f,
      120.0f,0.24f, 800.0f,0.9f,0.16f, 0.0f, 3200.0f,0.24f, 0.70f,0.10f,1.6f,
      110.0f,1.2f,2.0f,7500.0f, { 45,55,50,55,70,50 }, 1,2,1 },     // Twin (knobs aprobados)
    { "CHIME 30",        0.32f,0.030f,0.78f,1, 75.0f,6500.0f,0.18f,
      150.0f,0.20f, 1100.0f,1.2f,0.18f,-2.5f, 4500.0f,0.26f, 0.90f,0.22f,2.2f,
      130.0f,1.5f,3.0f,6800.0f, { 60,60,65,55,65,50 }, 0,0,0 },     // AC30 (knobs aprobados)
    { "AMBER TWEED",     0.42f,0.050f,0.85f,1, 70.0f,6000.0f,0.22f,
      120.0f,0.26f, 650.0f,0.8f,0.20f, 1.5f, 3000.0f,0.20f, 0.40f,0.38f,2.6f,
      95.0f,1.8f,4.0f,6000.0f, { 75,50,40,60,60,50 }, 0,0,0 },      // 5E3 (knobs aprobados)
    { "CHORUS 120",      0.05f,0.005f,0.50f,0, 60.0f,9000.0f,0.00f,
      120.0f,0.14f, 900.0f,0.7f,0.10f, 0.0f, 3600.0f,0.16f, 0.20f,0.04f,1.2f,
      150.0f,1.0f,1.0f,9000.0f, { 25,55,50,58,78,45 }, 1,2,2 },     // JC-120
    { "MARAUDER 800",    0.62f,0.055f,0.92f,1, 90.0f,5200.0f,0.10f,
      100.0f,0.26f, 620.0f,1.1f,0.22f, 2.5f, 2800.0f,0.24f, 0.50f,0.24f,2.4f,
      100.0f,1.6f,3.5f,5500.0f, { 62,58,65,55,66,62 }, 2,0,1 },     // JCM800
    { "LEAD 1959",       0.72f,0.065f,0.95f,1, 85.0f,5000.0f,0.14f,
      100.0f,0.26f, 580.0f,1.0f,0.22f, 2.0f, 2800.0f,0.24f, 0.60f,0.32f,2.8f,
      90.0f,1.5f,3.0f,5200.0f, { 70,60,62,58,64,55 }, 2,1,0 },      // 1959
    { "TANGERINE 120",   0.55f,0.060f,0.90f,1, 80.0f,5400.0f,0.10f,
      100.0f,0.28f, 520.0f,0.9f,0.24f, 3.5f, 2600.0f,0.20f, 0.25f,0.30f,2.5f,
      85.0f,1.7f,4.0f,5400.0f, { 58,65,70,45,64,42 }, 2,0,0 },      // OR120
    { "LONDON BLUES",    0.40f,0.040f,0.82f,1, 80.0f,6000.0f,0.08f,
      100.0f,0.24f, 700.0f,0.9f,0.20f, 1.0f, 3000.0f,0.22f, 0.50f,0.28f,2.1f,
      105.0f,1.4f,2.5f,6000.0f, { 48,56,62,52,68,50 }, 1,1,1 },     // Bluesbreaker
    { "SESSION HOT",     0.48f,0.050f,0.88f,1, 85.0f,5600.0f,0.12f,
      140.0f,0.22f, 720.0f,0.8f,0.19f, 2.0f, 3000.0f,0.20f, 0.45f,0.30f,2.3f,
      100.0f,1.5f,3.0f,5600.0f, { 55,54,60,50,66,52 }, 5,1,1 },     // ODS
    { "DUO RECTO",       0.85f,0.075f,1.15f,2,100.0f,4600.0f,0.20f,
      90.0f,0.30f, 450.0f,1.4f,0.20f,-4.5f, 3500.0f,0.24f, 0.35f,0.26f,3.2f,
      80.0f,1.8f,4.5f,4800.0f, { 72,68,38,58,62,55 }, 3,0,1 },      // Rectifier
    { "KRAFT V4",        0.78f,0.070f,1.05f,2, 95.0f,5000.0f,0.16f,
      100.0f,0.26f, 680.0f,1.3f,0.20f, 1.5f, 3200.0f,0.24f, 0.55f,0.18f,2.9f,
      95.0f,2.0f,4.0f,5200.0f, { 68,60,55,62,64,60 }, 3,0,1 },      // VH4
    { "HAVOC 100",       0.95f,0.085f,1.25f,2,100.0f,4400.0f,0.22f,
      90.0f,0.28f, 500.0f,1.2f,0.20f,-3.0f, 3400.0f,0.22f, 0.40f,0.22f,3.4f,
      85.0f,1.7f,4.5f,4600.0f, { 78,62,48,60,62,58 }, 3,2,0 },      // 5150
    { "PERFIL",          0.20f,0.020f,0.70f,0, 80.0f,7000.0f,0.00f,
      120.0f,0.24f, 800.0f,0.9f,0.16f, 0.0f, 3200.0f,0.22f, 0.60f,0.10f,1.4f,
      110.0f,1.2f,2.0f,7500.0f, { 50,50,50,50,50,50 }, 0,0,0 },     // captura NAM
};
// Orden: kVoices[i] ES la voz del ampli que la lista llama kAmpNames[i] y cuyo
// frontal trae kAmpArt[i]. Estaban los dos ultimos intercambiados: el ampli
// HAVOC 100 de la lista sonaba con la voz plana del PERFIL (y con su cabina) y
// el cabezal PERFIL montaba la voz del 5150. El autotest lo comprueba ahora.

// --- Cabes procedurales: 6 IRs analiticos (modos resonantes amortiguados) ---
struct CabModeDef {
    const char* name;
    float modes[5][3];   // f (Hz), Q, amplitud
    float hpF, lpF, thru;
};
static const CabModeDef kCabModes[6] = {
    { "1x12 COMBO",   { { 82,3.0f,0.9f},{190,4.0f,0.7f},{430,6.0f,0.5f},{1150,8.0f,0.35f},{3400,10.0f,0.22f} },  70.0f,7200.0f,0.55f },
    { "2x12 OPEN",    { { 75,3.5f,0.95f},{165,5.0f,0.75f},{400,7.0f,0.5f},{1000,9.0f,0.35f},{3000,11.0f,0.2f} },65.0f,6800.0f,0.50f },
    { "4x12 CLOSED",  { { 70,5.0f,1.0f},{150,6.0f,0.8f},{380,8.0f,0.55f},{980,10.0f,0.38f},{2900,12.0f,0.22f} }, 60.0f,6200.0f,0.45f },
    { "4x12 MODERN",  { { 65,5.5f,1.0f},{140,7.0f,0.85f},{360,9.0f,0.5f},{900,11.0f,0.4f},{3200,13.0f,0.26f} },  55.0f,7000.0f,0.40f },
    { "1x15 BASS",    { { 48,4.0f,1.0f},{110,5.0f,0.8f},{260,6.0f,0.5f},{700,8.0f,0.3f},{1800,9.0f,0.15f} },    40.0f,4500.0f,0.60f },
    { "BOUTIQUE 2x10",{ { 88,3.2f,0.9f},{210,4.5f,0.7f},{520,7.0f,0.55f},{1300,9.0f,0.4f},{3600,11.0f,0.24f} }, 75.0f,7600.0f,0.50f },
};

// --- Cabes procedurales por BICHAID: el camino por defecto aprobado ---
// Cada modelo define su propio filtrado de gabinete (HP + LP + 2 realces).
// La convolucion FFT con IR sintetico queda SOLO para el IR de usuario (WAV):
// era la causa del timbre horrible/robotico del camino nuevo.
struct CabBqDef { float hpF, lpF, pk1F, pk1Q, pk1Db, pk2F, pk2Q, pk2Db; };
static const CabBqDef kCabBiquads[6] = {
    {  70.0f, 7200.0f,  190.0f, 2.0f, 2.0f, 1150.0f, 1.2f, 1.5f },   // 1x12 COMBO
    {  65.0f, 6800.0f,  165.0f, 2.0f, 2.0f, 1000.0f, 1.2f, 1.2f },   // 2x12 OPEN
    {  60.0f, 6200.0f,  150.0f, 2.5f, 2.5f,  980.0f, 1.2f, 1.5f },   // 4x12 CLOSED
    {  55.0f, 7000.0f,  140.0f, 2.5f, 2.8f, 3200.0f, 1.5f, 2.0f },   // 4x12 MODERN
    {  40.0f, 4500.0f,  110.0f, 2.0f, 2.5f,  700.0f, 1.2f, 1.0f },   // 1x15 BASS
    {  75.0f, 7600.0f,  210.0f, 2.0f, 2.0f, 1300.0f, 1.2f, 1.2f },   // BOUTIQUE 2x10
};

static void genProcCabs(float fs) {
    if (g_procIRBuilt && g_procIRFs == fs) return;
    g_procIRFs = fs; g_procIRBuilt = true;
    for (int c = 0; c < 6; ++c) {
        const CabModeDef& D = kCabModes[c];
        CabIR& ir = g_procIR[c];
        ir.fr.assign(CabIR::N, 0.0f);
        ir.fi.assign(CabIR::N, 0.0f);
        ir.hist.assign(CabIR::BLOCK*(CabIR::N/CabIR::BLOCK - 1), 0.0f);
        std::vector<float> tt(CabIR::N, 0.0f);
        tt[0] += D.thru;                                   // impulso directo
        // PRNG determinista por cab (reproducible entre sesiones)
        uint32_t rs = 0x9E3779B9u ^ (uint32_t)(c*2654435761u + 1013904223u);
        auto rnd = [&rs]() { rs = rs*1664525u + 1013904223u;
                             return (float)((rs >> 8) & 0xFFFFFF) / 16777216.0f; };
        // Ataque menos "tick": realce directo con dispersiÃ³n de 2 taps
        tt[1] += D.thru*0.35f*rnd();
        tt[2] += D.thru*0.18f*rnd();
        // Banco modal DENSO: 8 parciales incoherentes por ancla (40 por cab).
        // Un IR de 5 sinusoides puras tenÃ­a espectro de peine: resonaba en 5
        // frecuencias exactas y sonaba a "zumbido"/robotico al convolucionar.
        // Jitter de frecuencia, amplitud, decaimiento y fase => densidad
        // modal realista (textura de gabinete, sin tonos puros).
        for (int k = 0; k < 5; ++k) {
            const float fa = D.modes[k][0], Qa = D.modes[k][1], aa = D.modes[k][2];
            for (int p = 0; p < 8; ++p) {
                const float jit = -0.22f + 0.50f*rnd();          // log-uniformo +-~25%
                const float f  = fa * std::exp(jit);
                const float Q  = Qa * (0.70f + 0.60f*rnd());
                const float A  = aa * 0.30f * (0.45f + 0.55f*rnd()) / (1.0f + 0.30f*(float)p);
                const float dec = kPi*f/Q;
                const float w  = 2.0f*kPi*f;
                const float ph = 6.2831853f*rnd();
                for (int i = 0; i < 512; ++i) {
                    const float s = (float)i/fs;
                    tt[i] += A*std::exp(-dec*s)*std::sin(w*s + ph);
                }
            }
        }
        // Textura de ruido amortiguado (papel/panno del gabinete): llena los
        // huecos espectrales y elimina el caracter "puro" de las sinusoides.
        for (int i = 0; i < 512; ++i) {
            const float s = (float)i/fs;
            tt[i] += 0.045f*std::exp(-285.0f*s)*(rnd()*2.0f - 1.0f);
        }
        float hpA = 0.0f; const float hpC = 1.0f - std::exp(-2.0f*kPi*D.hpF/fs);
        float lpS = 0.0f; const float lpC = 1.0f - std::exp(-2.0f*kPi*D.lpF/fs);
        for (int i = 0; i < CabIR::N; ++i) {
            float s = tt[i];
            hpA += hpC*(s - hpA); s = s - hpA;            // HP 1er orden
            lpS += lpC*(s - lpS); s = lpS;                // LP 1er orden
            if (i >= 448) { const float ft = (float)(512 - i)/64.0f; if (ft > 0.0f) s *= ft*ft; }
            ir.fr[i] = s;
        }
        // Normalizacion por ENERGIA (no por pico): la convolucion debe conservar
        // el nivel RMS de la senal. Un IR rico en modos normalizado por pico
        // amplificaba la cadena ~+15 dB y saturaba el limitador final con
        // clipping constante y aliasing (zumbido estridente en todo el sonido).
        double e = 1.0e-12;
        for (int i = 0; i < CabIR::N; ++i) e += (double)ir.fr[i]*(double)ir.fr[i];
        const float g = 0.85f/(float)std::sqrt(e);
        for (int i = 0; i < CabIR::N; ++i) ir.fr[i] *= g;
        sprintf_s(ir.nameW, "%s", D.name);
        fftRadix2(ir.fr, ir.fi, false);
    }
}
// ----------------------------------------------------------------------------
//  Cab v2: IR fisica por (modelo, microfono, distancia)
//  El intento anterior de IR procedural fallo por tres motivos concretos:
//   - un pico directo en t=0 (~0.5) que dominaba el IR,
//   - un banco de 40 sinusoides puras => peine y timbre "robotico",
//   - normalizacion por energia que no casaba el nivel con el camino anterior.
//  Aqui el IR se sintetiza con un modelo de altavoz/caja FILTRADO (respuesta
//  suave, sin picos sueltos), micro y distancia aplicados con filtros reales,
//  reflexiones difusas (nada de peine duro) y normalizacion por magnitud a
//  1 kHz, que es lo que mantiene el nivel igual al del camino clasico.
// ----------------------------------------------------------------------------
struct CabV2Def {
    const char* name;
    float fsq, fq, fsG;                     // resonancia del altavoz (Hz, Q, dB)
    float boxF, boxQ, boxG;                 // modo de la caja
    float honkF, honkQ, honkG;              // honk de medios (cono/papel)
    float b1F, b1Q, b1G, b2F, b2Q, b2G;     // rotura de cono (2 parciales)
    float lp1F, lp2F;                       // rolloff (inductancia + cono)
    float baffleDb;                         // escalon de baffle (dB)
};
static const CabV2Def kCabV2[6] = {
    // 1x12 COMBO (abierto, cono brillante)
    { "1x12 COMBO",   92.0f,1.15f,4.5f,  185.0f,1.6f,2.6f,  540.0f,1.4f,2.4f,  2100.0f,2.6f,3.6f, 3600.0f,3.2f,2.6f, 4600.0f,5600.0f,-3.0f },
    // 2x12 OPEN (alnico, medios ricos)
    { "2x12 OPEN",    84.0f,1.05f,5.0f,  170.0f,1.5f,2.2f,  620.0f,1.3f,3.2f,  1900.0f,2.4f,3.0f, 3300.0f,3.0f,2.4f, 4200.0f,5000.0f,-2.6f },
    // 4x12 CLOSED (graves firmes, medios de chasis)
    { "4x12 CLOSED",  76.0f,1.35f,6.0f,  150.0f,2.0f,3.4f,  420.0f,1.6f,3.6f,  1750.0f,3.0f,2.8f, 3000.0f,3.6f,2.0f, 3800.0f,4800.0f,-3.6f },
    // 4x12 MODERN (medios agresivos, extremo duro)
    { "4x12 MODERN",  70.0f,1.50f,6.5f,  145.0f,2.2f,3.8f,  900.0f,1.5f,4.2f,  2400.0f,2.8f,4.2f, 4000.0f,3.4f,2.6f, 4400.0f,5200.0f,-4.0f },
    // 1x15 BASS (mucho grave, agudo corto)
    { "1x15 BASS",    52.0f,1.20f,6.0f,  110.0f,1.8f,3.0f,  330.0f,1.4f,2.0f,  1200.0f,2.2f,2.2f, 2200.0f,2.8f,1.6f, 3200.0f,3800.0f,-2.4f },
    // BOUTIQUE 2x10 (tweedy, medios presentes y brillo)
    { "BOUTIQUE 2x10",100.0f,1.10f,4.0f,  205.0f,1.4f,2.4f,  700.0f,1.3f,3.0f,  2600.0f,2.4f,3.4f, 4200.0f,3.0f,2.8f, 5000.0f,6200.0f,-3.2f },
};
static const double kCabIrRef = 1.28;      // magnitud objetivo a 1 kHz (calibrada
                                           // contra el camino de biquads: Â±0.2 dB)
static CabIR g_procIR2[54];                // 6 modelos x 3 mics x 3 distancias
static bool  g_procIR2Built = false;
static float g_procIR2Fs = 0.0f;
static std::atomic<int> g_cabEngine{1};    // 0 = biquads clasicos, 1 = IR fisica

static void runBuf(std::vector<float>& h, Biquad& f) {
    f.reset();
    for (size_t i = 0; i < h.size(); ++i) h[i] = f.process(h[i]);
}

static void genCabIRv2(int c, int mic, int dist, float fs, CabIR& ir) {
    const int N = CabIR::N;
    const CabV2Def& D = kCabV2[c];
    std::vector<float> h(N, 0.0f);
    h[0] = 1.0f;
    Biquad f;

    // 1) Altavoz + caja: baffle, resonancia, modo de caja, honk, rotura y rolloff
    lowShelf(f, fs, 480.0f, D.baffleDb);  runBuf(h, f);
    peak(f, fs, D.fsq, D.fq, D.fsG);      runBuf(h, f);
    highpass(f, fs, D.fsq*0.60f, 0.82f);  runBuf(h, f);
    peak(f, fs, D.boxF, D.boxQ, D.boxG);  runBuf(h, f);
    peak(f, fs, D.honkF, D.honkQ, D.honkG); runBuf(h, f);
    peak(f, fs, D.b1F, D.b1Q, D.b1G);     runBuf(h, f);
    peak(f, fs, D.b2F, D.b2Q, D.b2G);     runBuf(h, f);
    lowpass(f, fs, D.lp1F, 0.90f);        runBuf(h, f);
    lowpass(f, fs, D.lp2F, 0.68f);        runBuf(h, f);

    // 2) Microfono
    if (mic == 0) {           // DINAMICO: presencia marcada, extremos cortados
        highpass(f, fs, 120.0f, 0.75f);   runBuf(h, f);
        lowShelf(f, fs, 400.0f, -2.0f);   runBuf(h, f);
        peak(f, fs, 5500.0f, 1.5f, 5.0f); runBuf(h, f);
        highShelf(f, fs, 9000.0f, -6.0f); runBuf(h, f);
    } else if (mic == 1) {    // RIBBON: calido, agudo dulce
        highpass(f, fs, 60.0f, 0.70f);    runBuf(h, f);
        peak(f, fs, 2200.0f, 0.90f, 2.0f);runBuf(h, f);
        highShelf(f, fs, 4000.0f, -4.5f); runBuf(h, f);
    } else {                  // CONDENSADOR: extendido, aire
        highpass(f, fs, 40.0f, 0.70f);    runBuf(h, f);
        peak(f, fs, 10500.0f, 1.2f, 3.0f);runBuf(h, f);
        highShelf(f, fs, 7000.0f, 2.5f);  runBuf(h, f);
    }

    // 3) Distancia
    if (dist == 0) {          // NEAR: proximidad
        lowShelf(f, fs, 150.0f, 3.0f);    runBuf(h, f);
    } else if (dist == 2) {   // FAR: aire + reflexiones difusas de sala
        lowShelf(f, fs, 200.0f, -3.5f);   runBuf(h, f);
        lowpass(f, fs, 5200.0f, 0.80f);   runBuf(h, f);
        std::vector<float> ref(h);
        lowpass(f, fs, 6500.0f, 0.75f);   runBuf(ref, f);
        const float tp[5] = { 0.0037f, 0.0055f, 0.0073f, 0.0096f, 0.0125f };
        const float am[5] = { 0.30f,   0.24f,   0.18f,   0.13f,   0.09f };
        for (int t = 0; t < 5; ++t) {
            const int d0 = (int)(tp[t]*fs);
            const float sgn = (t & 1) ? -1.0f : 1.0f;
            for (int i = d0; i < N; ++i) {
                const float env = std::exp(-(float)(i - d0)/(0.0035f*fs));
                h[i] += sgn*am[t]*env*ref[i - d0];
            }
        }
    }

    // 4) Desvanecido final corto (evita el corte seco del IR) y normalizacion
    //    por magnitud a 1 kHz: nivel constante entre cab/mic/dist y frente al
    //    camino clasico de biquads.
    for (int i = N - 24; i < N; ++i) h[i] *= (float)(N - i)/24.0f;
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i) {
            const double ph = 2.0*3.14159265358979*1000.0*(double)i/(double)fs;
            re += (double)h[i]*std::cos(ph);
            im -= (double)h[i]*std::sin(ph);
        }
        const double mag = std::sqrt(re*re + im*im);
        const float g = (float)(kCabIrRef/(mag + 1.0e-9));
        for (int i = 0; i < N; ++i) h[i] *= g;
    }

    // 5) Al espectro (el overlap-save lee fr/fi)
    ir.fr.assign(h.begin(), h.end());
    ir.fi.assign(N, 0.0f);
    ir.hist.assign(CabIR::BLOCK*(N/CabIR::BLOCK - 1), 0.0f);
    sprintf_s(ir.nameW, "%s", D.name);
    fftRadix2(ir.fr, ir.fi, false);
}

static void genProcCabsV2(float fs) {
    if (g_procIR2Built && g_procIR2Fs == fs) return;
    g_procIR2Fs = fs; g_procIR2Built = true;
    for (int c = 0; c < 6; ++c)
        for (int mi = 0; mi < 3; ++mi)
            for (int d = 0; d < 3; ++d)
                genCabIRv2(c, mi, d, fs, g_procIR2[c*9 + mi*3 + d]);
}

// Selecciona el IR activo (modelo + micro + distancia) segun el motor elegido
static void selectCabEx(int model, int mic, int dist) {
    if (model >= 0 && model < 6 && g_cabEngine.load() == 1) {
        const int mi = (mic  < 0 || mic  > 2) ? 0 : mic;
        const int ds = (dist < 0 || dist > 2) ? 1 : dist;
        g_cabIRPtr.store(&g_procIR2[model*9 + mi*3 + ds]);
    } else if (model == 6 && g_userIRPtr) {
        g_cabIRPtr.store(g_userIRPtr);
    } else if (model >= 0 && model < 6) {
        g_cabIRPtr.store(&g_procIR[model]);
    } else {
        g_cabIRPtr.store(&g_procIR[0]);
    }
}
static void selectCab(int model) { selectCabEx(model, g_params.cabMic.load(), g_params.cabDist.load()); }
static void initDspDefaults() {
    for (int m = 0; m < 13; ++m)
        for (int k = 0; k < 6; ++k)
            g_params.amp[m][k].store(kVoices[m].d[k]);
    static const float dfx[18][2] = {
        { -55, 25 }, { 40, 50 }, { 30, 50 }, { 35, 45 }, { 40, 35 }, { 45, 40 },
        { 40, 60 }, { 30, 55 }, { 25, 45 }, { 50, 50 },
        { 45, 50 }, { 50, 40 }, { 55, 45 }, { 35, 40 }, { 38, 35 }, { 45, 40 }, { 60, 50 }, { 45, 50 } };
    for (int f = 0; f < 18; ++f) {
        g_params.fx[f][0].store(dfx[f][0]);
        g_params.fx[f][1].store(dfx[f][1]);
        g_params.fxOn[f].store(f == 0 || f == 1 || f == 4 || f == 5);
    }
    g_params.model.store(0);
    g_params.master.store(70.0f);
    g_params.mixCab.store(100.0f);
    g_params.cabModel.store(kVoices[0].cab);
    g_params.cabMic.store(kVoices[0].mic);
    g_params.cabDist.store(kVoices[0].dist);
    publishChain(std::vector<int>{ 0, 1, 2 }, std::vector<int>{ 3, 4, 5 });
}

// Cadena completa
// Compensacion de nivel por pedal (dB). Medida con el harness (modo "fx") sobre
// el DI sintetico: encender un pedal no debe dar un salto de volumen.
static const float kFxTrimDb[18] = {
     0.0f,  0.0f,  -6.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,
     0.0f, -5.0f,  -5.3f, -5.6f,  0.0f,  0.0f,  0.0f, -3.5f,  0.0f
};   // 2 DRIVE, 10 OD, 11 FUZZ, 12 DIST y 16 BOOST (parcial: sigue empujando)
static float kFxTrimLin[18] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };

struct AmpChain {
    // Instantanea del perfil NAM del BLOQUE en curso. La deja audioBlockBegin()
    // (una carga atomica por bloque, no por muestra) y NO la toca nadie mas.
    const NamChain* namBlk = nullptr;
    // Conversion de frecuencia por bloque de la cadena (ver NamRateConv). El
    // modelo corre a SU tasa; esto lleva la senal y la devuelve.
    NamRateConv namConv[4];
    TubeStage  tube;
    NoiseGate  gate;
    Compressor comp;
    DriveStage drive;
    Chorus     chorus;
    DelayFX    delay;
    Reverb     reverb;
    Biquad     fBass, fMid, fTreble;
    DcBlock    ampDc;                                // continua a la salida del cabezal
    Biquad     inHp, cabHp, cabLp, cabPk1, cabPk2;   // entrada + gabinete
    // --- Nuevos pedales (6..9) + cab con IR: estado por tipo ---
    float tremPhase = 0.0f;                            // 6 TREMOLO
    float phPhase = 0.0f; Allpass1 ph[4];              // 7 PHASER
    float phLastFc = -1.0f;
    float flPhase = 0.0f;                              // 8 FLANGER
    std::vector<float> flBuf; int flW = 0;
    float flLastOut = 0.0f;
    Biquad eqLo, eqHi;                                 // 9 EQ
    float eqLastLo = -999.0f, eqLastHi = -999.0f;
    Biquad micLo, micHi; int micLast = -1;             // (legado) micro por tilt
    int   curModel = -1;
    float lastBass=-1, lastMid=-1, lastTreble=-1, lastPres=-1, lastBright=-1e9f;
    float lastExtra=-1;
    // Ganancia con la que esta configurado el pico extra. Se compara la GANANCIA
    // y no el valor del mando: el filtro tiene que volver a 0 dB cuando el ampli
    // nuevo no tiene ese control, y reset() solo limpia el estado, no los
    // coeficientes (el realce se quedaba sonando en las demas voces).
    float lastExtraDb = -1e9f;
    float fs = 48000.0f;
    // --- Motor de amp parametrizado ---
    Biquad fPres, brightCap, preLp, powPk;             // presencia / bright cap / 2a etapa / carga
    Biquad fExtra;                                     // control extra (resonancia de graves)
    float sagEnv = 0.0f;
    // --- Sobremuestreo 4x del tramo no lineal + nivel igualado por voz ---
    Os4   os;
    float u4[4] = {};
    float sagLp = 0.0f, sagLpA = 0.0f, sagAtk = 0.0f, sagRel = 0.0f;
    float outTrim = 1.0f;
    // --- Gabinete: micro por tipo + distancia ---
    Biquad micShelfLo, micShelfHi, micPeak, distShelf;
    float micKey = -1.0f;
    int lastSelCab = -1;
    std::vector<float> distComb; int distW = 0;
    // --- Pedales nuevos 10..17 ---
    Biquad odMid, odTone, odHp;    float odLastDr = -1.0f, odLastTn = -1.0f;   // 10 OD
    Biquad fzLp;                   float fzLastTn = -1.0f;                     // 11 FUZZ
    Biquad dsLp;                   float dsLastFl = -1.0f;                     // 12 DIST
    // Recortadores de pedal: DC bloqueada (igual que en los tubos) y carga de
    // entrada del fuzz; sin esto, el offset del recorte asimetrico se propaga
    // al amp y engorda el sonido con distorsion estatica.
    DcBlock odDc, fzDc, dsDc;
    Biquad  fzHp;
    Chorus chorusB, chorusC;                                               // 13 CHORUS
    // Controles extra del ampli: chorus (RATE/DEPTH), tremolo (INTENSITY/SPEED)
    // y compresion (COMP) propios, independientes de los pedales.
    Chorus ampChorusB, ampChorusC;
    Compressor ampComp;
    float ampTremPhase = 0.0f;
    std::vector<float> tapeBuf; int tapeW = 0;                             // 14 TAPE
    float tapeP1 = 0.0f, tapeP2 = 0.0f, tapeLp = 0.0f, tapeLpA = 0.0f;
    std::vector<float> sprBuf[3]; int sprW[3] = {};                        // 15 SPRING
    float sprDamp = 0.0f, sprDampA = 0.0f; int sprReady = -1;
    Biquad bsHp, bsTone;             float bsLastTn = -1.0f;               // 16 BOOST
    float wahEnv = 0.0f, wahLastFc = -1.0f; Biquad wahBp;                  // 17 AUTO-WAH

    void prepare(float sampleRate, bool rebuildCabs = true) {
        fs = sampleRate;
        // Instantanea de la cadena de pedales: la capacidad se reserva UNA vez
        // (el hilo de audio copia aqui en cada bloque y no puede reservar: T13).
        if (g_preSnap.capacity()  < (size_t)kChainMax) g_preSnap.reserve((size_t)kChainMax);
        if (g_postSnap.capacity() < (size_t)kChainMax) g_postSnap.reserve((size_t)kChainMax);
        // Cabina con IR: sus buffers son de tamaÃ±o fijo, se reservan aqui y
        // cambiar de cabina o de IR ya no reserva en el hilo de audio (T16).
        g_cabConv.prepare();
        os.prepare(fs); os.reset();
        ampDc.prepare(fs);
        for (int i = 0; i < 4; ++i) u4[i] = 0.0f;
        // El sag ahora se evalua a 4x: las constantes van a esa tasa
        {
            const float f4 = fs*4.0f;
            sagLpA = 1.0f - std::exp(-2.0f*kPi*180.0f/f4);   // envolvente pesada en graves
            sagAtk = 1.0f - std::exp(-1.0f/(0.006f*f4));
            sagRel = 1.0f - std::exp(-1.0f/(0.120f*f4));
            sagLp = 0.0f;
        }
        gate.prepare(fs); comp.prepare(fs); drive.prepare(fs);
        chorus.prepare(fs); delay.prepare(fs); reverb.prepare(fs);
        chorusB.prepare(fs); chorusC.prepare(fs);
        ampChorusB.prepare(fs); ampChorusC.prepare(fs); ampComp.prepare(fs);
        ampTremPhase = 0.0f;
        curModel = -1;
        // La conversion de frecuencia de las capturas depende de fs: se invalida
        // para que el primer bloque la reconfigure (namConfigure).
        for (int i = 0; i < 4; ++i) { namConv[i].ready = false; namConv[i].reset(); }
        // La tabla de IR procedurales se regenera EN LA MISMA DIRECCION al cambiar
        // de frecuencia de muestreo (genProcCabsV2) y ensureFor solo compara
        // punteros: sin invalidar aqui, la convolucion seguia usando el espectro
        // calculado a la tasa anterior y todo el gabinete quedaba desplazado.
        g_cabConv.irCur = nullptr;
        lastBass = lastMid = lastTreble = -1;
        lastPres = -1; lastBright = -1e9f;
        tremPhase = phPhase = flPhase = 0.0f;
        for (int i = 0; i < 4; ++i) ph[i] = Allpass1();
        phLastFc = -1.0f;
        flBuf.assign(2048, 0.0f); flW = 0; flLastOut = 0.0f;
        eqLastLo = eqLastHi = -999.0f; eqLo.reset(); eqHi.reset();
        micLast = -1; micLo.reset(); micHi.reset();
        sagEnv = 0.0f; micKey = -1.0f;
        distComb.clear(); distW = 0;
        highpass(odHp, fs, 48.0f, 0.707f);
        highpass(bsHp, fs, 60.0f, 0.707f);
        highpass(fzHp, fs, 90.0f, 0.700f);
        odDc.prepare(fs); fzDc.prepare(fs); dsDc.prepare(fs);
        for (int i = 0; i < 18; ++i) kFxTrimLin[i] = std::pow(10.0f, kFxTrimDb[i]/20.0f);
        tapeLpA = 1.0f - std::exp(-2.0f*kPi*3200.0f/fs);
        tapeBuf.clear(); tapeW = 0; tapeP1 = tapeP2 = tapeLp = 0.0f;
        sprReady = -1; sprDamp = 0.0f;
        wahEnv = 0.0f; wahLastFc = -1.0f;
        // Las 6 cabes procedurales y las 54 IRs fisicas solo cambian con la frecuencia
        // de muestreo. La recuperacion por NaN (que la pide el hilo de audio) las
        // reutiliza en vez de rehacerlas: generarlas son 54 convoluciones con FFT
        // de 1024 puntos y en el hilo de tiempo real son decenas de ms. Los
        // coeficientes ya estan a fs; lo que hace falta es limpiar el estado.
        if (rebuildCabs) {
            genProcCabs(fs);                       // 6 cabes procedurales (una vez por fs)
            genProcCabsV2(fs);                     // 54 IRs fisicas cab x mic x distancia
        }
        selectCabEx(g_params.cabModel.load(), g_params.cabMic.load(), g_params.cabDist.load());
        if (!g_cabIRPtr.load() && g_params.cabModel.load() == 6) {  // USER IR no disponible -> procedural
            g_params.cabModel.store(0);
            selectCab(0);
        }
    }

    void update() {
        const int m = g_params.model.load();
        if (m != curModel) {
            curModel = m;
            const AmpVoice& V = kVoices[m];
            const float fso = fs*4.0f;                        // tramo no lineal a 4x
            tube.configure(V.tD, V.tB, V.tH, fso, V.tTrim, kVoicesCoupling[m]);
            highpass(inHp, fs, V.inCut, 0.707f);              // HP de entrada: tasa base
            lowpass(preLp, fso, V.lp2, 0.80f);
            peak(powPk, fso, V.resoF, V.resoQ, V.resoDb);     // resonancia de la carga
            os.reset();
            outTrim = std::pow(10.0f, kVoicesTrimDb[m]/20.0f);
            // El cab por biquad se configura por MODELO de cabina (bloque siguiente)
            lastSelCab = -1;
            inHp.reset(); preLp.reset(); powPk.reset();
            cabHp.reset(); cabLp.reset(); cabPk1.reset(); cabPk2.reset();
            fBass.reset(); fMid.reset(); fTreble.reset(); fPres.reset(); brightCap.reset();
            // El pico extra (RESONANCE/DEEP) tambien se resetea: si no, el realce
            // de +10 dB a 95 Hz del ampli anterior se quedaba sonando en el nuevo
            // (el bloque de abajo solo recalcula fExtra si el modelo TIENE ese
            // control, asi que al cambiar a uno que no lo tiene nadie lo tocaba).
            fExtra.reset();
            lastExtra = -1.0f;
            lastExtraDb = -1e9f;
            lastBass = lastMid = lastTreble = lastPres = -1;
            lastBright = -1e9f;
            sagEnv = 0.0f;
            ampDc.reset();
        }
        // Cab por biquad (camino por defecto aprobado): filtrado por MODELO de cabina
        {
            int sel = g_params.cabModel.load();
            if (sel < 0 || sel > 5) sel = 0;         // 6 = USER IR: el filtrado lo hace el convolver
            if (sel != lastSelCab) {
                lastSelCab = sel;
                const CabBqDef& C = kCabBiquads[sel];
                highpass(cabHp, fs, C.hpF, 0.70f);
                lowpass(cabLp, fs, C.lpF, 0.80f);
                peak(cabPk1, fs, C.pk1F, C.pk1Q, C.pk1Db);
                peak(cabPk2, fs, C.pk2F, C.pk2Q, C.pk2Db);
                cabHp.reset(); cabLp.reset(); cabPk1.reset(); cabPk2.reset();
            }
        }
        const AmpVoice& V = kVoices[m];
        const float bass = g_params.amp[m][1].load();
        const float mid  = g_params.amp[m][2].load();
        const float treb = g_params.amp[m][3].load();
        const float pres = g_params.amp[m][5].load();
        const float vol  = g_params.amp[m][4].load();
        // Tone stack + presencia + bright cap: lineales, pero viven dentro del
        // tramo 4x (entre el preamp y la potencia), asi que se calculan a fso.
        // En el PERFIL (captura) van DESPUES del modelo y a la frecuencia base.
        const float fso = (m == 12) ? fs : fs*4.0f;
        if (bass != lastBass) { lastBass = bass; lowShelf(fBass, fso, V.bassF, (bass - 50.0f)*V.bassSc); }
        if (mid != lastMid)   { lastMid = mid;   peak(fMid, fso, V.midF, (mid - 50.0f)*V.midSc + V.midBias, V.midQ); }
        if (treb != lastTreble) { lastTreble = treb; highShelf(fTreble, fso, V.trebF, (treb - 50.0f)*V.trebSc); }
        if (pres != lastPres) { lastPres = pres; highShelf(fPres, fso, 3400.0f, (pres - 50.0f)*0.22f); }
        // Control extra del ampli: si su chapa imprime RESONANCE, es un pico de
        // graves (el empuje de la carga del altavoz); neutro en 50 (0 dB), asi
        // que los presets de fabrica suenan exactamente igual que antes.
        const float extra = ampExtraVal(m, 1);
        const bool hasReso = (ampExtraKindAt(m, 0) == 1 || ampExtraKindAt(m, 1) == 1);
        const float exDb = hasReso ? (extra - 50.0f)*0.20f : 0.0f;
        if (exDb != lastExtraDb) { lastExtraDb = exDb; peak(fExtra, fso, 95.0f, exDb, 0.85f); }
        const float bcap = V.bright*(1.0f - vol*0.01f)*6.0f;   // bright cap: mas al bajar el volumen
        if (std::fabs(bcap - lastBright) > 0.15f) { lastBright = bcap; highShelf(brightCap, fso, 4200.0f, bcap); }
    }

    // Aplica un tipo de pedal con sus parametros exactos (bypass si esta off)
    inline float applyFx(int t, float x) {
        switch (t) {
        case 0: {
            if (!g_params.fxOn[0].load()) return x;
            // THRESHOLD en dB (-80..-20); DECAY 0..100 -> ratio 1:1..16:1
            float thr = g_params.fx[0][0].load();
            float rat = 1.0f + g_params.fx[0][1].load()*0.15f;
            if (thr < -80.0f) thr = -80.0f;
            if (thr > -20.0f) thr = -20.0f;
            return gate.process(x, thr, rat, true);
        }
        case 1:
            if (!g_params.fxOn[1].load()) return x;
            return comp.process(x, g_params.fx[1][0].load(), g_params.fx[1][1].load());
        case 2:
            if (!g_params.fxOn[2].load()) return x;
            return drive.process(x, g_params.fx[2][0].load(), g_params.fx[2][1].load())*kFxTrimLin[2];
        case 3:
            if (!g_params.fxOn[3].load()) return x;
            return chorus.process(x, g_params.fx[3][0].load(), g_params.fx[3][1].load());
        case 4:
            if (!g_params.fxOn[4].load()) return x;
            return delay.process(x, 40.0f + g_params.fx[4][0].load()*5.6f,
                                      g_params.fx[4][1].load()*0.0075f);
        case 5:
            if (!g_params.fxOn[5].load()) return x;
            return reverb.process(x, g_params.fx[5][0].load(), g_params.fx[5][1].load()/100.0f);
        case 6: {   // TREMOLO: amplitud modulada por LFO
            if (!g_params.fxOn[6].load()) return x;
            const float rate = 0.4f + g_params.fx[6][0].load()*0.116f;    // 0.4..12 Hz
            const float dep  = g_params.fx[6][1].load()/100.0f;
            tremPhase += 2.0f*kPi*rate/fs;
            if (tremPhase > 2.0f*kPi) tremPhase -= 2.0f*kPi;
            const float lfo = 0.5f + 0.5f*std::sin(tremPhase);
            return x*(1.0f - dep + dep*lfo);
        }
        case 7: {   // PHASER: 4 all-pass en cascada con barrido
            if (!g_params.fxOn[7].load()) return x;
            const float rate = 0.05f + g_params.fx[7][0].load()*0.055f;   // 0.05..6 Hz
            const float dep  = g_params.fx[7][1].load()/100.0f;
            phPhase += 2.0f*kPi*rate/fs;
            if (phPhase > 2.0f*kPi) phPhase -= 2.0f*kPi;
            const float sweep = 0.5f + 0.5f*std::sin(phPhase);
            const float fc = 180.0f + sweep*dep*2400.0f;                  // 180..2580 Hz
            if (std::fabs(fc - phLastFc) > 4.0f) {
                phLastFc = fc;
                for (int i = 0; i < 4; ++i) ph[i].setFc(fc, fs);
            }
            float y = x;
            for (int i = 0; i < 4; ++i) y = ph[i].process(y);
            return x*0.55f + y*0.45f;
        }
        case 8: {   // FLANGER: delay modulado con realimentacion
            if (!g_params.fxOn[8].load()) return x;
            if ((int)flBuf.size() != 2048) { flBuf.assign(2048, 0.0f); flW = 0; }
            const float rate = 0.1f + g_params.fx[8][0].load()*0.049f;    // 0.1..5 Hz
            const float dep  = g_params.fx[8][1].load()/100.0f;
            flPhase += 2.0f*kPi*rate/fs;
            if (flPhase > 2.0f*kPi) flPhase -= 2.0f*kPi;
            const float dMs = 1.5f + dep*8.5f*(0.5f + 0.5f*std::sin(flPhase));  // 1.5..10 ms
            float dSamp = dMs*fs*0.001f;
            if (dSamp < 2.0f) dSamp = 2.0f;
            if (dSamp > 2040.0f) dSamp = 2040.0f;
            const float fb = 0.25f + dep*0.45f;
            float rp = (float)flW - dSamp; while (rp < 0.0f) rp += 2048.0f;
            const int   i0 = (int)rp;
            const int   i1 = (i0 + 1) & 2047;
            const float frac = rp - (float)i0;
            const float d  = flBuf[i0]*(1.0f - frac) + flBuf[i1]*frac;
            flLastOut = d;
            flBuf[flW] = x + d*fb;
            flW = (flW + 1) & 2047;
            return x*0.55f + d*0.45f;
        }
        case 9: {   // EQ: shelf grave + agudo, +-12 dB (50 = plano)
            if (!g_params.fxOn[9].load()) return x;
            const float lo = g_params.fx[9][0].load();
            const float hi = g_params.fx[9][1].load();
            if (lo != eqLastLo) { eqLastLo = lo; lowShelf(eqLo, fs, 220.0f,   (lo - 50.0f)*0.24f); }
            if (hi != eqLastHi) { eqLastHi = hi; highShelf(eqHi, fs, 3200.0f, (hi - 50.0f)*0.24f); }
            return eqHi.process(eqLo.process(x));
        }
        case 10: {  // OVERDRIVE estilo TS: pre-Ã©nfasis de medios + recorte asimÃ©trico
            if (!g_params.fxOn[10].load()) return x;
            const float dr = g_params.fx[10][0].load(), tn = g_params.fx[10][1].load();
            if (dr != odLastDr) { odLastDr = dr; peak(odMid, fs, 720.0f, 2.0f + dr*0.07f, 1.0f); }
            if (tn != odLastTn) { odLastTn = tn; highShelf(odTone, fs, 3000.0f, (tn - 50.0f)*0.20f); }
            float y = odMid.process(x);
            y = odHp.process(y);
            // Ganancia unitaria a seÃ±al pequeÃ±a + bias real + DC fuera: el pedal
            // limpia al bajar el pote de la guitarra y rompe al subirlo.
            const float g = 1.0f + dr*0.05f;
            const float b = 0.09f;
            const float k = 1.0f/(std::tanh(b)*std::tanh(b));
            (void)k;
            y = std::tanh(y*g + b) - std::tanh(b);
            y = odDc.process(y);
            return odTone.process(y)*kFxTrimLin[10];
        }
        case 11: {  // FUZZ de germanio: carga de entrada, bias fuerte y sustain
            if (!g_params.fxOn[11].load()) return x;
            const float fz = g_params.fx[11][0].load(), tn = g_params.fx[11][1].load();
            if (tn != fzLastTn) { fzLastTn = tn; lowpass(fzLp, fs, 700.0f + tn*33.0f, 0.707f); }
            float y = fzHp.process(x)*0.8f;                  // carga de entrada del fuzz
            const float g = 2.0f + fz*0.10f;
            const float b = 0.30f;
            y = std::tanh(y*g*1.2f + b) - std::tanh(b);      // bias marcado -> "spitty"
            y = fzDc.process(y);
            return fzLp.process(y)*kFxTrimLin[11];
        }
        case 12: {  // DISTORTION estilo RAT: recorte duro + filtro de agudo
            if (!g_params.fxOn[12].load()) return x;
            const float dv = g_params.fx[12][0].load(), fl = g_params.fx[12][1].load();
            if (fl != dsLastFl) { dsLastFl = fl; lowpass(dsLp, fs, 2500.0f + fl*55.0f, 1.1f); }
            float y = x*(1.2f + dv*0.10f);
            const float th = 1.0f - dv*0.006f;               // umbral cae con el drive
            if (y >  th) y =  th + (y -  th)*0.12f;
            if (y < -th*0.85f) y = -th*0.85f + (y + th*0.85f)*0.12f;
            y = dsDc.process(y);                             // fuera el offset del recorte
            return dsLp.process(y)*kFxTrimLin[12];
        }
        case 13: {  // CHORUS BBD: dos voces con LFO desfasado
            if (!g_params.fxOn[13].load()) return x;
            const float rate = g_params.fx[13][0].load(), dep = g_params.fx[13][1].load();
            const float y1 = chorusB.process(x, rate, dep);
            const float y2 = chorusC.process(x, rate*0.61f + 9.0f, dep*0.8f);
            return x*0.52f + y1*0.34f + y2*0.34f;
        }
        case 14: {  // TAPE ECHO: saturacion + wow/flutter + tono de repeticiones
            if (!g_params.fxOn[14].load()) return x;
            if ((int)tapeBuf.size() != (int)(fs*0.6f) + 8) { tapeBuf.assign((size_t)(fs*0.6f) + 8, 0.0f); tapeW = 0; }
            const float tm = 80.0f + g_params.fx[14][0].load()*4.2f;        // 80..500 ms
            const float fb = 0.25f + g_params.fx[14][1].load()*0.0052f;     // hasta ~0.77
            tapeP1 += 2.0f*kPi*0.6f/fs;  if (tapeP1 > 2.0f*kPi) tapeP1 -= 2.0f*kPi;   // wow
            tapeP2 += 2.0f*kPi*5.7f/fs;  if (tapeP2 > 2.0f*kPi) tapeP2 -= 2.0f*kPi;   // flutter
            const float mod = std::sin(tapeP1)*2.6f + std::sin(tapeP2)*0.55f;         // ms
            float sp = (tm + mod)*0.001f*fs;
            const float Nn = (float)tapeBuf.size();
            if (sp < 2.0f) sp = 2.0f;
            if (sp > Nn - 4.0f) sp = Nn - 4.0f;
            float rp = (float)tapeW - sp; while (rp < 0.0f) rp += Nn;
            const int i0 = (int)rp, i1 = (i0 + 1) % (int)tapeBuf.size();
            float d = tapeBuf[i0]*(1.0f - (rp - (float)i0)) + tapeBuf[i1]*(rp - (float)i0);
            d = std::tanh(d*1.6f)*0.8f;                      // saturacion de cinta
            tapeLp += tapeLpA*(d - tapeLp); d = tapeLp;      // tono (envejecimiento)
            tapeBuf[tapeW] = x + d*fb;
            tapeW = (tapeW + 1) % (int)tapeBuf.size();
            return x*0.6f + d*0.6f;
        }
        case 15: {  // SPRING REVERB: tanque de muelles (3 combs + damping)
            if (!g_params.fxOn[15].load()) return x;
            const float dwell = g_params.fx[15][0].load(), mixv = g_params.fx[15][1].load();
            if (sprReady < 0) {
                const float cm[3] = { 29.7f, 33.1f, 37.3f };
                for (int i = 0; i < 3; ++i) { sprBuf[i].assign((size_t)(fs*cm[i]*0.001f) + 4, 0.0f); sprW[i] = 0; }
                sprDampA = 1.0f - std::exp(-2.0f*kPi*5200.0f/fs);
                sprDamp = 0.0f;
                sprReady = 1;
            }
            const float fb = 0.80f + dwell*0.0017f;
            float acc = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const int n = (int)sprBuf[i].size(); if (n < 4) continue;
                float y = sprBuf[i][sprW[i]];
                sprDamp += sprDampA*(y - sprDamp);           // damping en el loop
                sprBuf[i][sprW[i]] = x + sprDamp*fb;
                sprW[i] = (sprW[i] + 1) % n;
                acc += y;
            }
            acc *= 0.33f;
            const float wet = mixv*0.0085f;
            return x*(1.0f - wet*0.5f) + acc*wet;
        }
        case 16: {  // BOOST limpio: nivel + inclinacion (tilt) de tono
            if (!g_params.fxOn[16].load()) return x;
            const float lv = g_params.fx[16][0].load(), tn = g_params.fx[16][1].load();
            if (tn != bsLastTn) { bsLastTn = tn;
                highShelf(bsTone, fs, 2800.0f, (tn - 50.0f)*0.20f);
                lowShelf(bsHp,  fs, 180.0f,  (50.0f - tn)*0.14f); }   // tilt real
            return bsTone.process(bsHp.process(x))*(1.0f + lv*0.14f)*kFxTrimLin[16];
        }
        case 17: {  // AUTO-WAH: barrido resonante por envolvente
            if (!g_params.fxOn[17].load()) return x;
            const float sens = g_params.fx[17][0].load(), rng = g_params.fx[17][1].load();
            const float ax = std::fabs(x);
            const float aA = std::exp(-1.0f/(0.004f*fs)), aR = std::exp(-1.0f/(0.090f*fs));
            wahEnv = (ax > wahEnv) ? aA*wahEnv + (1.0f - aA)*ax
                                   : aR*wahEnv + (1.0f - aR)*ax;
            float e = wahEnv*3.5f*(0.4f + sens*0.012f);
            if (e > 1.0f) e = 1.0f;
            const float fc = 300.0f + e*(200.0f + rng*20.0f);   // 300..~2500 Hz
            if (std::fabs(fc - wahLastFc) > 18.0f) { wahLastFc = fc; bandpass(wahBp, fs, fc, 4.5f); }
            return x*0.45f + wahBp.process(x)*0.9f;
        }
        }
        return x;
    }

    // Ajusta la conversion de frecuencia de cada bloque a la tasa de SU modelo.
    // Se llama una vez por bloque (audioBlockBegin); configure() no hace nada si
    // las tasas no han cambiado, asi que el coste es comparar cuatro dobles.
    void namConfigure() {
        if (!namBlk) return;
        int i = 0;
        for (const auto& sl : *namBlk) {
            if (i >= 4) break;
            const double mr = (sl && sl->model) ? sl->model->sampleRate : (double)fs;
            namConv[i].configure(mr, (double)fs);
            ++i;
        }
    }

    inline float process(float x, const std::vector<int>& pre, const std::vector<int>& post) {
        update();

        // Sensibilidad de entrada: guitarra -> nivel de trabajo
        x *= 1.8f;
        x = inHp.process(x);
        // COMP del ampli (solo en los amps cuya chapa lo imprime): neutro en 50.

        // --- Pedales ANTES del amp, en el orden del tablero ---
        for (size_t i = 0; i < pre.size(); ++i) x = applyFx(pre[i], x);

        // --- Amplificador: preamp por voz + tone stack + presencia + potencia ---
        // Todo el tramo no lineal (tubos, 2a etapa y recorte de potencia) se
        // evalua a 4x y se decima con filtro real: ahi estaba el aliasing de los
        // modelos con mas ganancia.
        const int       m = curModel;
        // --- Captura NAM: el perfil SUSTITUYE al nucleo no lineal -----------
        //  El resto del motor se queda: pedales antes, tono posterior, cabina
        //  despues. El mando GAIN es un trim de entrada (+-12 dB) sobre el nivel
        //  que pide el fichero; VOLUME es la salida. Sin perfil: silencio.
        const bool namAmp = (m == 12);
        // Instantanea del BLOQUE (audioBlockBegin): antes era un
        // std::atomic_load(&g_namLive) por muestra, que en MSVC es un spin-lock y
        // ponia al hilo de audio a competir con la interfaz 48.000 veces por
        // segundo. Aqui solo se lee el puntero que dejo el bloque (T12).
        const NamChain* namCh = namAmp ? namBlk : nullptr;
        if (namAmp && namCh && !namCh->empty()) {
            // GAIN del cabezal = trim de entrada a la cadena (+-12 dB)
            const float trimDb = (g_params.amp[12][0].load() - 50.0f)*0.24f;
            float v = x*std::pow(10.0f, trimDb/20.0f);
            // Bloques EN SERIE, en el orden que marque la cadena. Cada bloque pasa
            // por su conversion de frecuencia (el modelo corre a la SUYA).
            int ci = 0;
            for (const auto& sl : *namCh) {
                NamRateConv& rc = namConv[ci < 4 ? ci : 3];
                ++ci;
                if (!sl || !sl->on.load(std::memory_order_relaxed)) continue;
                nam::Model* mm = sl->model.get();
                if (!mm) continue;
                const float inT = sl->inTrim.load(std::memory_order_relaxed);
                v = rc.tick([mm](float s) { return mm->tick(s); }, v*inT)
                    * sl->outTrim.load(std::memory_order_relaxed)
                    * sl->level.load(std::memory_order_relaxed);
            }
            v = fBass.process(v);
            v = fMid.process(v);
            v = fTreble.process(v);
            x = v*std::pow(g_params.amp[12][4].load()/100.0f, 1.2f)*2.0f;
        } else if (namAmp) {
            x = 0.0f;                       // sin bloques: silencio
        }
        if (!namAmp) {
            const AmpVoice& V = kVoices[m];
            const float gain = 1.0f + (g_params.amp[m][0].load()/100.0f)*3.0f;
            const float vol  = std::pow(g_params.amp[m][4].load()/100.0f, 1.4f)*2.0f;
            const float pwG  = 0.9f + V.pDrive*0.45f;
            const float pwN  = 1.0f/std::tanh(pwG*0.75f);
            const float gv   = gain*vol*0.35f;
            os.up(x, u4);
            for (int k = 0; k < 4; ++k) {
                float v = tube.process(u4[k]);
                if (V.stage2 > 0.01f) {                  // 2a etapa asimetrica (amps HD)
                    v = v - V.stage2*0.22f*v*v + V.stage2*0.15f*v*v*v;
                    v = preLp.process(v);
                }
                v = fBass.process(v);
                v = fMid.process(v);
                v = fTreble.process(v);
                v = fPres.process(v);
                v = brightCap.process(v);
                v *= gv;
                // SAG de potencia: envolvente pesada en graves (los graves hunden mas)
                const float ax = (v > 0.0f) ? v : -v;
                sagLp += sagLpA*(ax - sagLp);
                sagEnv += ((sagLp > sagEnv) ? sagAtk : sagRel)*(sagLp - sagEnv);
                float envN = sagEnv*3.5f; if (envN > 1.0f) envN = 1.0f;
                v *= (1.0f - V.sag*0.45f*(1.0f - envN));
                // Etapa de potencia: recorte suave + resonancia de la carga del altavoz
                v = std::tanh(v*pwG)*pwN;
                v = powPk.process(v);
                // RESONANCIA: va DESPUES de la potencia (es la carga del altavoz, no
                // una EQ de entrada). Antes entraba antes de saturar y el propio
                // recorte se comia el empuje: medido, +0,6 dB arriba frente a -3,2
                // abajo. Aqui el mando empuja de verdad en los dos sentidos.
                v = fExtra.process(v);
                u4[k] = v;
            }
            x = os.down(u4)*outTrim;
            // Sin continua a la salida del cabezal: la 2a etapa asimetrica y el
            // recorte del tanh de potencia dejan un desvio que, con la MEZCLA por
            // debajo del 100 %, llega tal cual al conector (medido: 2,9 % del rms
            // en TANGERINE 120) y que ademas hace "clac" al cambiar de ampli o al
            // soltar una nota, porque es un escalon. Aqui se quita una sola vez,
            // a la tasa base y a 8 Hz: por encima de eso es transparente.
            x = ampDc.process(x);
        }
        // Control extra tipo NIVEL (OUTPUT/VOLUME/MASTER): recorte de +/-10 dB
        // sobre la salida del ampli, neutro en 50.
        {
            const float lv = ampExtraVal(m, 2);
            if (lv != 50.0f) x *= std::pow(10.0f, (lv - 50.0f)/100.0f);
        }
        // COMP del ampli (solo amps cuya chapa lo imprime): a la SALIDA del
        // cabezal. Delante se la comia la saturacion del ampli y no se oia.
        {
            const float cv = ampExtraVal(m, 7);
            const float amt = (cv - 50.0f)*2.0f;                   // -100..100
            if (amt > 0.0f) x = ampComp.process(x, 50.0f + amt*0.5f, amt*0.08f);
        }

        // TREMOLO del ampli: profundidad (INTENSITY) y velocidad (SPEED). Neutro
        // en 50: por debajo no modula, asi que los presets de fabrica no cambian.
        {
            const float dep = ampExtraVal(m, 3);
            const float spd = ampExtraVal(m, 4);
            if (dep > 0.0f || spd != 50.0f) {
                const float hz = 0.6f + (spd/100.0f)*7.0f;      // 0.6..7.6 Hz
                ampTremPhase += hz/fs;
                if (ampTremPhase >= 1.0f) ampTremPhase -= 1.0f;
                float d01 = dep/100.0f;
                if (d01 < 0.12f && spd != 50.0f) d01 = 0.12f;   // mover la velocidad ya modula
                const float lfo = 0.5f + 0.5f*std::sin(2.0f*(float)M_PI*ampTremPhase);
                x *= 1.0f - d01*(1.0f - lfo);
            }
        }

        // CHORUS del ampli (solo amps con RATE/DEPTH impresos): neutro en 50.
        {
            const float cr = ampExtraVal(m, 5);
            const float cd = ampExtraVal(m, 6);
            if (cd > 0.0f || cr != 50.0f) {
                float d01 = cd/100.0f;
                if (d01 < 0.42f && cr != 50.0f) d01 = 0.42f;    // mover la velocidad ya chorusea
                const float cdEff = d01*100.0f;      // la profundidad que MUEVE el delay
                const float y1 = ampChorusB.process(x, cr, cdEff);
                const float y2 = ampChorusC.process(x, cr*0.61f + 9.0f, cdEff*0.8f);
                const float wet = 0.34f*d01;
                x = x*(1.0f - wet*0.55f) + (y1 + y2)*wet*0.5f;
            }
        }

        // --- Gabinete: IR fisica (cab x mic x distancia) o biquads clasicos ---
        //  engine 1 (por defecto): convolucion overlap-save con el IR del cab
        //  seleccionado; micro y distancia ya viven dentro del IR, asi que no se
        //  vuelven a aplicar aqui (hacerlo doblaba el filtrado).
        //  engine 0: camino clasico de biquads (queda para comparar A/B).
        {
            const float dry = x;
            float wet;
            const int sel = g_params.cabModel.load();
            const bool irEng = (sel != 6) && (g_cabEngine.load() == 1);
            if (irEng) {
                CabIR* ir = g_cabIRPtr.load();       // IR objetivo del motor v2
                if (ir) {
                    g_cabConv.ensureFor(ir);
                    wet = g_cabConv.process(x);      // convolucion overlap-save
                } else {
                    wet = cabHp.process(x);
                    wet = cabLp.process(wet);
                    wet = cabPk1.process(wet);
                    wet = cabPk2.process(wet);
                }
            } else if (sel == 6) {                   // USER IR: convolucion FFT
                CabIR* ir = g_cabIRPtr.load();
                if (ir) {
                    g_cabConv.ensureFor(ir);
                    wet = g_cabConv.process(x);      // convolucion overlap-save
                } else {
                    wet = cabHp.process(x);
                    wet = cabLp.process(wet);
                    wet = cabPk1.process(wet);
                    wet = cabPk2.process(wet);
                }
            } else {
                // Cabes procedurales: biquads por MODELO (camino clasico)
                wet = cabHp.process(x);
                wet = cabLp.process(wet);
                wet = cabPk1.process(wet);
                wet = cabPk2.process(wet);
            }
            if (!irEng) {
                // Micro por tipo + distancia (proximidad / plano / habitacion)
                const int mic = g_params.cabMic.load(), dst = g_params.cabDist.load();
                const float key = (float)(mic*10 + dst);
                if (key != micKey) {
                    micKey = key;
                    if (mic == 0)      { lowShelf(micShelfLo, fs, 180.0f, 0.8f);  highShelf(micShelfHi, fs, 5000.0f, -2.0f); peak(micPeak, fs, 3200.0f, 1.0f, 0.6f); }
                    else if (mic == 1) { lowShelf(micShelfLo, fs, 200.0f, 0.5f);  highShelf(micShelfHi, fs, 4000.0f, -3.0f); peak(micPeak, fs, 2400.0f, 0.5f, 0.3f); }
                    else               { lowShelf(micShelfLo, fs, 160.0f, -0.5f); highShelf(micShelfHi, fs, 8000.0f, 1.2f);  peak(micPeak, fs, 5000.0f, 0.8f, 0.5f); }
                    if (dst == 0)      lowShelf(distShelf, fs, 120.0f, 1.2f);    // NEAR: proximidad
                    else if (dst == 1) lowShelf(distShelf, fs, 120.0f, 0.0f);    // MID: plano
                    else               lowShelf(distShelf, fs, 120.0f, -1.0f);   // FAR: aire
                    if (dst == 2) {
                        const size_t n = (size_t)(0.011f*fs) + 4;
                        if (distComb.size() != n) { distComb.assign(n, 0.0f); distW = 0; }
                    }
                }
                wet = micShelfLo.process(wet); wet = micShelfHi.process(wet); wet = micPeak.process(wet);
                wet = distShelf.process(wet);
                if (dst == 2 && !distComb.empty()) {     // reflexion corta (habitacion)
                    const int n = (int)distComb.size();
                    float d = distComb[distW];
                    distComb[distW] = wet + d*0.22f;
                    distW = (distW + 1) % n;
                    wet = wet*0.70f + d*0.20f;
                }
            }
            // Mezcla directa / micro (knob MIX del cluster CAB)
            const float mix = g_params.mixCab.load()*0.01f;
            x = dry*(1.0f - mix) + wet*mix;
        }

        // --- Pedales DESPUES del amp, en el orden del tablero ---
        for (size_t i = 0; i < post.size(); ++i) x = applyFx(post[i], x);

        // --- Master: tanh solo como limitador de seguridad ---
        //  El codo se desplaza al techo (0.6x + compensacion): la seÃ±al normal
        //  pasa LINEAL y solo se limita el pico. Antes comprimia siempre y eso
        //  generaba armonicos por encima de Nyquist que aliasaban (se media como
        //  "energia no armonica" en las voces limpias, que son las mas picosas).
        x *= std::pow(g_params.master.load()/100.0f, 1.3f);
        const float xr = x;
        {
            // Transparente hasta 0.8 y techo duro en 1.0: protege la salida y el
            // WAV sin meter armonicos en la seÃ±al normal (el tanh anterior entraba
            // siempre y su codo generaba aliasing medible en las voces limpias).
            const float a = (xr < 0.0f) ? -xr : xr;
            if (a > 0.80f) {
                const float s = (xr < 0.0f) ? -1.0f : 1.0f;
                x = s*(0.80f + 0.20f*std::tanh((a - 0.80f)/0.20f));
            } else {
                x = xr;
            }
        }
        // LED de clip: se enciende cuando el limitador esta recortando de verdad
        // (con el codo suave, 1.0 de seÃ±al cruda = salida practicamente en el techo)
        if (xr > 1.0f || xr < -1.0f) g_clipFlag.store(1.0f);
        return x;
    }
};
static AmpChain g_amp;

// Ring buffer lock-free
struct RingBuffer {
    std::vector<float> data;
    std::atomic<size_t> w{0}, r{0};
    void resize(size_t n) { data.assign(n, 0.0f); w = r = 0; }
    // Reservar capacidad sin tocar la ventana logica. Se usa donde el redimensionar
    // puede coincidir con otro hilo leyendo o escribiendo: assign() puede liberar
    // el vector anterior, y quien este dentro se queda escribiendo memoria liberada.
    // ensure() SOLO crece, y quien lo llama tiene que garantizar que no hace falta.
    void ensure(size_t n) { if (n > data.size()) data.assign(n, 0.0f); }
    // Reiniciar los indices sin reasignar. w y r son atomicos, asi que esto es
    // seguro aunque otro hilo este empujando o sacando: lo que puede truncarse es
    // alguna muestra del borde, no la memoria.
    void clear() { w = r = 0; }
    void push(const float* p, size_t n) {
        if (data.empty() || n == 0) return;
        size_t ww = w.load(std::memory_order_relaxed);
        size_t rr = r.load(std::memory_order_acquire);
        size_t avail = (ww + data.size() - rr) % data.size();
        // Proteccion contra desbordamiento (deriva de reloj entre dispositivos):
        // si se llena, se descarta lo mas antiguo en vez de corromper el buffer
        if (avail + n >= data.size()) {
            size_t drop = avail + n - data.size() + 1;
            r.store((rr + drop) % data.size(), std::memory_order_release);
        }
        for (size_t i = 0; i < n; ++i)
            data[(ww + i) % data.size()] = p[i];
        w.store((ww + n) % data.size(), std::memory_order_release);
    }
    size_t pop(float* out, size_t n) {
        // Sin anillo dimensionado no hay nada que sacar. Esta guarda es
        // OBLIGATORIA: si data esta vacio, el % data.size() de abajo es una
        // division por cero y tumba el proceso. No es teorico: el primer
        // repintado de la ventana puede llegar antes que el hilo de audio, que
        // es quien dimensiona el anillo (g_scopeRing.resize en prepare), asi que
        // el arranque dependia de quien ganase la carrera. push ya lo miraba.
        if (data.empty() || n == 0) return 0;
        size_t rr = r.load(std::memory_order_relaxed);
        size_t ww = w.load(std::memory_order_acquire);
        size_t avail = (ww + data.size() - rr) % data.size();
        size_t take = (avail < n) ? avail : n;
        for (size_t i = 0; i < take; ++i) out[i] = data[(rr + i) % data.size()];
        r.store((rr + take) % data.size(), std::memory_order_release);
        return take;
    }
};
static RingBuffer g_ring;
static RingBuffer g_scopeRing;         // visualizador: senal procesada (hilo UI lo drena)
static std::atomic<float> g_scopeHash{0.0f}; // RMS del ultimo bloque empujado (dirty-flag)
static std::vector<float> g_scopeHist; // historial local del pintado (solo hilo UI)

// ============================================================================
//  Persistencia (GuitarAmp.ini junto al exe): audio + DSP + presets usuario
// ============================================================================
static std::wstring g_iniPath;
static std::wstring exeDirW() {
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    size_t bs = p.find_last_of(L"\\/");
    return (bs == std::wstring::npos) ? L"" : p.substr(0, bs);
}
static void iniBuildPath() {
    if (!g_iniPath.empty()) return;
    g_iniPath = exeDirW() + L"\\GuitarAmp.ini";
}

// --- Codificacion del .ini ---------------------------------------------------
// La API de perfiles escribe en la pagina de codigos del sistema, no en Unicode,
// salvo que el fichero lleve BOM UTF-16. Con la config de este equipo eso
// funciona: ACP=1252 y el nombre del microfono se guarda como un unico byte 0xF3
// y vuelve intacto. Los dos fallos aparecen en el borde:
//   - Al mover el .ini a otra maquina, el byte se relee con SU pagina de
//     codigos. Lo que era un microfono vuelve como caracteres raros, y el
//     guardado siguiente los fija para siempre.
//   - Un caracter que la pagina no tiene (japones, cirilico, emoji) no se
//     guarda: sale '?'. En una RUTA eso es perdida de datos silenciosa --el
//     .nam deja de existir para la app y no hay forma de saber cual era--.
// Con BOM UTF-16LE las dos funciones W trabajan en Unicode puro y no hay
// conversion en ningun sentido. El fichero son 4 KB: duplicarlo no cuesta nada.
// Esta rutina migra el ANSI que hubiera, una sola vez.
static void iniEnsureUtf16() {
    HANDLE h = CreateFileW(g_iniPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring uni;
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz; sz.QuadPart = 0;
        GetFileSizeEx(h, &sz);
        if (sz.QuadPart > 0 && sz.QuadPart <= (LONGLONG)(1 << 20)) {
            std::vector<unsigned char> raw((size_t)sz.QuadPart);
            DWORD got = 0;
            ReadFile(h, raw.data(), (DWORD)raw.size(), &got, nullptr);
            raw.resize(got);
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            // Ya es UTF-16: no hay nada que hacer.
            if (got >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) return;
            // UTF-8 con BOM si alguien lo exporto asi; si no, la pagina del
            // sistema, que es justo lo que hacia la API al leerlo.
            const bool utf8 = (got >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF);
            std::string bytes((const char*)raw.data(), got);
            const int n = MultiByteToWideChar(utf8 ? CP_UTF8 : CP_ACP, 0,
                                              bytes.c_str(), (int)bytes.size(),
                                              nullptr, 0);
            if (n > 0) {
                uni.resize((size_t)n);
                MultiByteToWideChar(utf8 ? CP_UTF8 : CP_ACP, 0,
                                    bytes.c_str(), (int)bytes.size(), &uni[0], n);
            }
        } else {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
    }
    std::vector<unsigned char> out;
    out.reserve(2 + uni.size() * 2);
    out.push_back(0xFF); out.push_back(0xFE);          // BOM UTF-16LE
    for (size_t i = 0; i < uni.size(); ++i) {
        out.push_back((unsigned char)( uni[i]        & 0xFF));
        out.push_back((unsigned char)((uni[i] >> 8)  & 0xFF));
    }
    // Se abre para escribir siempre, tambien cuando no habia fichero: asi la
    // primera clave que escriba la API de perfiles sale ya en Unicode.
    HANDLE c = CreateFileW(g_iniPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (c == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(c, out.data(), (DWORD)out.size(), &wr, nullptr);
    CloseHandle(c);
}

// --- Rutas portables --------------------------------------------------------
// En el .ini las rutas se guardan RELATIVAS a la carpeta del ejecutable cuando
// el fichero esta dentro de ella: asi el estado sobrevive a que la app se mueva
// de carpeta o de equipo. Las de fuera --un .nam bajadode Descargas, una IR de
// otra unidad-- se guardan absolutas, porque alli una ruta relativa no
// significaria nada.
static std::wstring iniPathStore(const std::wstring& abs) {
    const std::wstring base = exeDirW() + L"\\";
    if (abs.size() > base.size() && _wcsnicmp(abs.c_str(), base.c_str(), base.size()) == 0)
        return abs.substr(base.size());
    return abs;
}
static std::wstring iniPathLoad(const std::wstring& stored) {
    // Absoluta ya (X:\..., \..., \\servidor, /...) -> tal cual. El .ini
    // anterior guardaba todo asi, asi que esto tambien es la ruta de Upgrade.
    if (stored.empty() || stored[0] == L'\\' || stored[0] == L'/' ||
        (stored.size() > 1 && stored[1] == L':'))
        return stored;
    return exeDirW() + L"\\" + stored;
}

static void iniGetFlt(const wchar_t* sec, const wchar_t* key, float* out, int n) {
    wchar_t b[256]; GetPrivateProfileStringW(sec, key, L"", b, 256, g_iniPath.c_str());
    if (!b[0]) return;
    int i = 0; wchar_t* ctx = nullptr;
    for (wchar_t* tok = wcstok_s(b, L",", &ctx); tok && i < n; tok = wcstok_s(nullptr, L",", &ctx))
        out[i++] = (float)_wtof(tok);
}
static void iniGetIntL(const wchar_t* sec, const wchar_t* key, int* out, int n) {
    wchar_t b[128]; GetPrivateProfileStringW(sec, key, L"", b, 128, g_iniPath.c_str());
    if (!b[0]) return;
    int i = 0; wchar_t* ctx = nullptr;
    for (wchar_t* tok = wcstok_s(b, L",", &ctx); tok && i < n; tok = wcstok_s(nullptr, L",", &ctx))
        out[i++] = _wtoi(tok);
}
static int iniGetInt(const wchar_t* sec, const wchar_t* key, int def) {
    wchar_t b[64]; GetPrivateProfileStringW(sec, key, L"", b, 64, g_iniPath.c_str());
    return b[0] ? _wtoi(b) : def;
}
// Un entero de 64 bits (las fechas unix). _wtoi corta a 32 bits y en 2038 se
// daria la vuelta, asi que se lee a mano.
static long long iniGetI64(const wchar_t* sec, const wchar_t* key, long long def) {
    wchar_t b[64]; GetPrivateProfileStringW(sec, key, L"", b, 64, g_iniPath.c_str());
    if (!b[0]) return def;
    long long v = 0; int i = 0; bool neg = false;
    if (b[0] == L'-') { neg = true; i = 1; }
    for (; b[i] >= L'0' && b[i] <= L'9'; ++i) v = v*10 + (b[i] - L'0');
    return neg ? -v : v;
}

static void saveIni() {
    iniBuildPath();
    iniEnsureUtf16();   // antes de la primera escritura: de aqui en adelante la API ve Unicode
    AudioSettings st;
    { std::lock_guard<std::mutex> lk(g_settingsMutex); st = g_appliedSet; }
    wchar_t buf[512];
    swprintf_s(buf, L"%d", st.system);                  WritePrivateProfileStringW(L"audio", L"system", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", st.rateIdx);                 WritePrivateProfileStringW(L"audio", L"rate", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", st.bufIdx);                  WritePrivateProfileStringW(L"audio", L"buf", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", st.enableInput ? 1 : 0);     WritePrivateProfileStringW(L"audio", L"in", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", st.enableOutput ? 1 : 0);    WritePrivateProfileStringW(L"audio", L"out", buf, g_iniPath.c_str());
    {
        // Nombres de dispositivo: estables aunque Windows reordene la lista
        std::lock_guard<std::mutex> lk(g_deviceMutex);
        if (g_selectedInputIdx >= 0 && g_selectedInputIdx < (int)g_inputDevices.size())
            WritePrivateProfileStringW(L"audio", L"inName",
                g_inputDevices[g_selectedInputIdx].name.c_str(), g_iniPath.c_str());
        if (g_selectedOutputIdx >= 0 && g_selectedOutputIdx < (int)g_outputDevices.size())
            WritePrivateProfileStringW(L"audio", L"outName",
                g_outputDevices[g_selectedOutputIdx].name.c_str(), g_iniPath.c_str());
    }
    swprintf_s(buf, L"%d", g_params.model.load());      WritePrivateProfileStringW(L"state", L"model", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", g_lang);                     WritePrivateProfileStringW(L"state", L"lang", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%.4f", (double)g_params.master.load()); WritePrivateProfileStringW(L"state", L"master", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%.4f", (double)g_params.inTrim.load()); WritePrivateProfileStringW(L"state", L"inTrim", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", g_params.cabModel.load()); WritePrivateProfileStringW(L"state", L"cabModel", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", g_params.cabMic.load());   WritePrivateProfileStringW(L"state", L"cabMic", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", g_params.cabDist.load());  WritePrivateProfileStringW(L"state", L"cabDist", buf, g_iniPath.c_str());
    swprintf_s(buf, L"%d", g_cabEngine.load());       WritePrivateProfileStringW(L"state", L"cabEngine", buf, g_iniPath.c_str());
    if (g_t3kKey[0]) WritePrivateProfileStringW(L"state", L"t3kKey", g_t3kKey, g_iniPath.c_str());
    if (!g_t3kToken.empty())   WritePrivateProfileStringW(L"state", L"t3kToken",   wstr(t3kProtect(g_t3kToken)).c_str(),   g_iniPath.c_str());
    if (!g_t3kRefresh.empty()) WritePrivateProfileStringW(L"state", L"t3kRefresh", wstr(t3kProtect(g_t3kRefresh)).c_str(), g_iniPath.c_str());
    // Cadena de capturas del cabezal PERFIL: orden, ruta, encendido y nivel
    {
        NamChain snap;
        { std::lock_guard<std::mutex> lk(g_namMutex); snap = g_namUi; }
        WCHAR buf2[64];
        swprintf_s(buf2, L"%d", (int)snap.size());
        WritePrivateProfileStringW(L"chain", L"count", buf2, g_iniPath.c_str());
        for (size_t i = 0; i < snap.size(); ++i) {
            WCHAR key[32], val[64];
            const std::wstring rel = iniPathStore(snap[i]->path);
            swprintf_s(key, L"p%d", (int)i);
            WritePrivateProfileStringW(L"chain", key, rel.c_str(), g_iniPath.c_str());
            swprintf_s(key, L"on%d", (int)i);
            WritePrivateProfileStringW(L"chain", key, snap[i]->on.load() ? L"1" : L"0", g_iniPath.c_str());
            swprintf_s(key, L"lv%d", (int)i);
            swprintf_s(val, L"%.2f", (double)snap[i]->level.load());
            WritePrivateProfileStringW(L"chain", key, val, g_iniPath.c_str());
        }
        // Las claves de los bloques que se han quedado fuera NO se reescriben:
        // la API solo pisa las que se le pasan. Se acumulan para siempre y el
        // .ini acaba guardando rutas de sesiones antiguas (medido en el estado
        // anterior: count=1 con p0, p1 y p2 de tres montajes distintos).
        // Para borrar una clave la API quiere el valor a NULL, no vacio: "" deja
        // la linea puesta y sin contenido. El barrido va de snap.size() al tope
        // que puede leer el cargador, no al count ANTERIOR: con el count ya
        // rebajado, las claves viejas de indices mayores no entraban en el rango
        // y el .ini se las quedaba para siempre.
        for (int i = (int)snap.size(); i < 64; ++i) {
            for (int f = 0; f < 3; ++f) {
                WCHAR key[32];
                swprintf_s(key, (f == 0) ? L"p%d" : (f == 1) ? L"on%d" : L"lv%d", i);
                WritePrivateProfileStringW(L"chain", key, nullptr, g_iniPath.c_str());
            }
        }
    }
    swprintf_s(buf, L"%.4f", (double)g_params.mixCab.load()); WritePrivateProfileStringW(L"state", L"mixCab", buf, g_iniPath.c_str());
    WritePrivateProfileStringW(L"cab", L"irPath", iniPathStore(g_cabIRPath).c_str(), g_iniPath.c_str());
    swprintf_s(buf, L"%08X", (unsigned)g_favMask);     WritePrivateProfileStringW(L"state", L"favMask", buf, g_iniPath.c_str());
    {
        // Favoritos de preset de fabrica: una cadena de kNumPresets digitos, el
        // COLOR de cada uno ('0' = sin color). Antes era '0'/'1' y un .ini viejo
        // se lee igual: '1' es el color 1 (ambar).
        WCHAR pf[kNumPresets + 8];
        for (int i = 0; i < kNumPresets; ++i)
            pf[i] = (WCHAR)(L'0' + (g_favPreset[i] > 4 ? 4 : (int)g_favPreset[i]));
        pf[kNumPresets] = 0;
        WritePrivateProfileStringW(L"state", L"presetFav", pf, g_iniPath.c_str());
    }
    swprintf_s(buf, L"%d", g_presetIdx);                WritePrivateProfileStringW(L"state", L"preset", buf, g_iniPath.c_str());
    WritePrivateProfileStringW(L"state", L"presetVer", L"2", g_iniPath.c_str());   // banco por lotes
    for (int a = 0; a < 13; ++a) {
        wchar_t key[16]; swprintf_s(key, L"a%d", a);
        swprintf_s(buf, L"%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
                   (double)g_params.amp[a][0].load(), (double)g_params.amp[a][1].load(),
                   (double)g_params.amp[a][2].load(), (double)g_params.amp[a][3].load(),
                   (double)g_params.amp[a][4].load(), (double)g_params.amp[a][5].load(),
                   (double)g_params.amp[a][6].load(), (double)g_params.amp[a][7].load());
        WritePrivateProfileStringW(L"amp", key, buf, g_iniPath.c_str());
    }
    for (int f = 0; f < 18; ++f) {
        wchar_t key[16]; swprintf_s(key, L"f%d", f);
        swprintf_s(buf, L"%.4f,%.4f", (double)g_params.fx[f][0].load(), (double)g_params.fx[f][1].load());
        WritePrivateProfileStringW(L"fx", key, buf, g_iniPath.c_str());
    }
    {
        wchar_t on[40] = L"";
        for (int f = 0; f < 18; ++f) {
            wchar_t bit[4]; swprintf_s(bit, L"%d%s", g_params.fxOn[f].load() ? 1 : 0, f < 17 ? L"," : L"");
            wcscat_s(on, bit);
        }
        WritePrivateProfileStringW(L"fx", L"on", on, g_iniPath.c_str());
    }
    {
        std::lock_guard<std::mutex> lk(g_chainMutex);
        wchar_t pre[32] = L"", post[32] = L"";
        for (size_t i = 0; i < g_chainPre.size(); ++i) {
            wchar_t b[4]; swprintf_s(b, L"%d%s", g_chainPre[i], i + 1 < g_chainPre.size() ? L"," : L"");
            wcscat_s(pre, b);
        }
        for (size_t i = 0; i < g_chainPost.size(); ++i) {
            wchar_t b[4]; swprintf_s(b, L"%d%s", g_chainPost[i], i + 1 < g_chainPost.size() ? L"," : L"");
            wcscat_s(post, b);
        }
        WritePrivateProfileStringW(L"chain", L"pre", pre, g_iniPath.c_str());
        WritePrivateProfileStringW(L"chain", L"post", post, g_iniPath.c_str());
    }
    swprintf_s(buf, L"%d", (int)g_userPresets.size());  WritePrivateProfileStringW(L"user", L"count", buf, g_iniPath.c_str());
    for (size_t i = 0; i < g_userPresets.size() && i < (size_t)kMaxUserPresets; ++i) {
        const UserPreset& u = g_userPresets[i];
        wchar_t sec[16]; swprintf_s(sec, L"user%u", (unsigned)i);
        WritePrivateProfileStringW(sec, L"name", u.name, g_iniPath.c_str());
        swprintf_s(buf, L"%d", u.favColor);    WritePrivateProfileStringW(sec, L"fav", buf, g_iniPath.c_str());
        swprintf_s(buf, L"%d", u.model);  WritePrivateProfileStringW(sec, L"model", buf, g_iniPath.c_str());
        // Metadatos del navegador (paso 18). Las dos cadenas se borran cuando
        // quedan vacias (el mismo patron que la cadena de pedales): si no, el
        // .ini se quedaria con el autor que se acaba de quitar.
        WritePrivateProfileStringW(sec, L"author",  u.author[0]  ? u.author  : nullptr, g_iniPath.c_str());
        WritePrivateProfileStringW(sec, L"comment", u.comment[0] ? u.comment : nullptr, g_iniPath.c_str());
        swprintf_s(buf, L"%u", u.tags);      WritePrivateProfileStringW(sec, L"tags",    buf, g_iniPath.c_str());
        swprintf_s(buf, L"%lld", u.created); WritePrivateProfileStringW(sec, L"created", buf, g_iniPath.c_str());
        swprintf_s(buf, L"%lld", u.used);    WritePrivateProfileStringW(sec, L"used",    buf, g_iniPath.c_str());
        swprintf_s(buf, L"%.4f", (double)u.master); WritePrivateProfileStringW(sec, L"master", buf, g_iniPath.c_str());
        swprintf_s(buf, L"%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
                   (double)u.amp[0], (double)u.amp[1], (double)u.amp[2],
                   (double)u.amp[3], (double)u.amp[4], (double)u.amp[5]);
        WritePrivateProfileStringW(sec, L"am", buf, g_iniPath.c_str());
        swprintf_s(buf, L"%d", u.cabModel); WritePrivateProfileStringW(sec, L"cabModel", buf, g_iniPath.c_str());
        swprintf_s(buf, L"%d", u.cabMic);   WritePrivateProfileStringW(sec, L"cabMic", buf, g_iniPath.c_str());
        swprintf_s(buf, L"%d", u.cabDist);  WritePrivateProfileStringW(sec, L"cabDist", buf, g_iniPath.c_str());
        for (int f = 0; f < 18; ++f) {
            wchar_t key[16]; swprintf_s(key, L"f%d", f);
            swprintf_s(buf, L"%.4f,%.4f", (double)u.fx[f][0], (double)u.fx[f][1]);
            WritePrivateProfileStringW(sec, key, buf, g_iniPath.c_str());
        }
        {
            wchar_t on[40] = L"";
            for (int f = 0; f < 18; ++f) {
                wchar_t bit[4]; swprintf_s(bit, L"%d%s", u.fxOn[f] ? 1 : 0, f < 17 ? L"," : L"");
                wcscat_s(on, bit);
            }
            WritePrivateProfileStringW(sec, L"on", on, g_iniPath.c_str());
        }
        {
            wchar_t pre[32] = L"", post[32] = L"";
            for (int i2 = 0; i2 < u.preCount && i2 < 6; ++i2) {
                wchar_t b[4]; swprintf_s(b, L"%d%s", u.pre[i2], i2 + 1 < u.preCount ? L"," : L"");
                wcscat_s(pre, b);
            }
            for (int i2 = 0; i2 < u.postCount && i2 < 6; ++i2) {
                wchar_t b[4]; swprintf_s(b, L"%d%s", u.post[i2], i2 + 1 < u.postCount ? L"," : L"");
                wcscat_s(post, b);
            }
            WritePrivateProfileStringW(sec, L"pre", pre, g_iniPath.c_str());
            WritePrivateProfileStringW(sec, L"post", post, g_iniPath.c_str());
        }
    }
}

// Recarga la cadena del cabezal PERFIL desde el .ini. Es una RECARGA, no un
// anadido: vacia primero lo que hubiera en memoria (namChainClear) para que
// releer el .ini no pueda duplicar la cadena.
static void namChainLoadFromIni() {
    namChainClear();
    const int n = iniGetInt(L"chain", L"count", 0);
    for (int i = 0; i < n && i < 16; ++i) {
        wchar_t key[32], np[MAX_PATH] = L"";
        swprintf_s(key, L"p%d", i);
        GetPrivateProfileStringW(L"chain", key, L"", np, MAX_PATH, g_iniPath.c_str());
        if (!np[0]) continue;
        // El .ini guarda la ruta relativa a la carpeta del exe cuando el
        // .nam vive ahi dentro; lo que hubiera de antes venia absoluto y
        // iniPathLoad lo devuelve tal cual.
        const std::wstring abs = iniPathLoad(np);
        if (GetFileAttributesW(abs.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        std::wstring msg;
        const int idx = namChainAdd(abs.c_str(), msg);
        if (idx < 0) continue;
        NamChain snap;
        { std::lock_guard<std::mutex> lk(g_namMutex); snap = g_namUi; }
        if (idx < (int)snap.size()) {
            swprintf_s(key, L"on%d", i);
            snap[idx]->on.store(iniGetInt(L"chain", key, 1) != 0);
            swprintf_s(key, L"lv%d", i);
            float lv = 1.0f;
            iniGetFlt(L"chain", key, &lv, 1);
            snap[idx]->level.store(lv < 0.0f ? 0.0f : (lv > 2.0f ? 2.0f : lv));
        }
    }
    namPublish();
}

// Carga los presets de usuario del .ini (SUSTITUYE la lista en memoria, no se
// suma a ella). Es una funcion propia porque su contrato -que un preset viejo
// sin las claves nuevas del navegador siga cargando- se prueba solo, sin
// arrastrar el resto de loadIni (audio, ampli, efectos).
static void userPresetsLoadFromIni() {
    g_userPresets.clear();
    int cnt = iniGetInt(L"user", L"count", 0);
    if (cnt > kMaxUserPresets) cnt = kMaxUserPresets;
    for (int i = 0; i < cnt; ++i) {
        wchar_t sec[16]; swprintf_s(sec, L"user%d", i);
        wchar_t wname[64] = L"";
        GetPrivateProfileStringW(sec, L"name", L"", wname, 64, g_iniPath.c_str());
        if (!wname[0]) continue;
        UserPreset u = {};
        wcsncpy_s(u.name, wname, _TRUNCATE);
        u.model = iniGetInt(sec, L"model", -1);
        if (u.model < 0 || u.model >= kAmpCount) u.model = -1;
        u.favColor = iniGetInt(sec, L"fav", 0);   // color 0..4 ('fav=1' viejo = ambar)
        if (u.favColor < 0 || u.favColor > kFavColors - 1)
            u.favColor = (u.favColor != 0) ? 1 : 0;
        GetPrivateProfileStringW(sec, L"author",  L"", u.author,  32,  g_iniPath.c_str());
        GetPrivateProfileStringW(sec, L"comment", L"", u.comment, 160, g_iniPath.c_str());
        u.tags = (unsigned int)iniGetInt(sec, L"tags", 0);
        u.tags &= (1u << kCharCount) - 1;         // solo caracteres conocidos
        u.created = iniGetI64(sec, L"created", 0);
        u.used    = iniGetI64(sec, L"used", 0);
        float mv2 = 70.0f; iniGetFlt(sec, L"master", &mv2, 1); u.master = mv2;
        {
            float dam[6] = { 0,0,0,0,0,0 };
            iniGetFlt(sec, L"am", dam, 6);
            bool any = false;
            for (int k = 0; k < 5; ++k) if (dam[k] != 0.0f) any = true;
            if (any && dam[5] == 0.0f) dam[5] = 50.0f;
            for (int k = 0; k < 6; ++k) u.amp[k] = any ? dam[k] : 0.0f;
        }
        u.cabModel = 0; iniGetInt(sec, L"cabModel", u.cabModel);
        u.cabMic   = 0; iniGetInt(sec, L"cabMic",   u.cabMic);
        u.cabDist  = 1; iniGetInt(sec, L"cabDist",  u.cabDist);
        if (u.cabModel < 0 || u.cabModel > 6) u.cabModel = 0;
        if (u.cabMic < 0 || u.cabMic > 2) u.cabMic = 0;
        if (u.cabDist < 0 || u.cabDist > 2) u.cabDist = 1;
        for (int f = 0; f < 18; ++f) {
            wchar_t key[16]; swprintf_s(key, L"f%d", f);
            iniGetFlt(sec, key, u.fx[f], 2);
        }
        {
            int on[18] = {};
            iniGetIntL(sec, L"on", on, 18);
            for (int f = 0; f < 18; ++f) u.fxOn[f] = (on[f] != 0);
        }
        {
            int pre[6] = { 0,0,0,0,0,0 }, post[6] = { 0,0,0,0,0,0 };
            iniGetIntL(sec, L"pre", pre, 6);
            iniGetIntL(sec, L"post", post, 6);
            wchar_t bp[128] = L"", bq[128] = L"";
            GetPrivateProfileStringW(sec, L"pre", L"", bp, 128, g_iniPath.c_str());
            GetPrivateProfileStringW(sec, L"post", L"", bq, 128, g_iniPath.c_str());
            wchar_t* ctx = nullptr;
            for (wchar_t* tok = wcstok_s(bp, L",", &ctx); tok && u.preCount < 6; tok = wcstok_s(nullptr, L",", &ctx)) {
                int tv = _wtoi(tok);
                if (tv >= 0 && tv < 18) u.pre[u.preCount++] = tv;
            }
            ctx = nullptr;
            for (wchar_t* tok = wcstok_s(bq, L",", &ctx); tok && u.postCount < 6; tok = wcstok_s(nullptr, L",", &ctx)) {
                int tv = _wtoi(tok);
                if (tv >= 0 && tv < 18) u.post[u.postCount++] = tv;
            }
        }
        g_userPresets.push_back(u);
    }
}

// Los favoritos de PRESET de fabrica: una cadena de kNumPresets digitos, el
// color de cada uno. Un .ini viejo trae '0'/'1' y se lee igual ('1' = color 1,
// ambar). Funcion propia por lo mismo que la de presets: su migracion se prueba.
static void presetFavLoadFromIni() {
    wchar_t pf[kNumPresets + 8] = L"";
    GetPrivateProfileStringW(L"state", L"presetFav", L"", pf, kNumPresets + 8, g_iniPath.c_str());
    for (int i = 0; i < kNumPresets; ++i) {
        const WCHAR c = pf[i];
        int v = (c >= L'0' && c <= L'9') ? (int)(c - L'0') : 0;
        if (v > kFavColors - 1) v = kFavColors - 1;
        g_favPreset[i] = (unsigned char)v;
    }
}

static void loadIni() {
    iniBuildPath();
    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) return; // sin ini: presets de fabrica
    iniEnsureUtf16();   // migra el ANSI de las versiones anteriores antes de leerlo
    AudioSettings st;
    { std::lock_guard<std::mutex> lk(g_settingsMutex); st = g_appliedSet; }
    st.system = iniGetInt(L"audio", L"system", st.system);
    st.rateIdx = iniGetInt(L"audio", L"rate", st.rateIdx);
    st.bufIdx  = iniGetInt(L"audio", L"buf", st.bufIdx);
    if (st.rateIdx < 0 || st.rateIdx >= kNumRates) st.rateIdx = 1;
    if (st.bufIdx  < 0 || st.bufIdx  >= kNumBufs)  st.bufIdx  = 2;
    st.enableInput  = iniGetInt(L"audio", L"in",  st.enableInput  ? 1 : 0) != 0;
    st.enableOutput = iniGetInt(L"audio", L"out", st.enableOutput ? 1 : 0) != 0;
    // Idioma: si el .ini no lo trae se queda el que eligio el arranque (el del
    // sistema). Un valor raro tambien cae al espanol.
    g_lang = iniGetInt(L"state", L"lang", g_lang);
    if (g_lang != 0 && g_lang != 1) g_lang = 0;
    { std::lock_guard<std::mutex> lk(g_settingsMutex); g_appliedSet = st; g_settings = st; }
    // Dispositivos por nombre (sobreviven a reordenaciones y reinicios)
    wchar_t wantIn[256] = L"", wantOut[256] = L"";
    GetPrivateProfileStringW(L"audio", L"inName",  L"", wantIn,  256, g_iniPath.c_str());
    GetPrivateProfileStringW(L"audio", L"outName", L"", wantOut, 256, g_iniPath.c_str());
    for (int i = 0; i < (int)g_inputDevices.size(); ++i)
        if (wantIn[0] && g_inputDevices[i].name == wantIn)  { g_selectedInputIdx = i;  break; }
    for (int i = 0; i < (int)g_outputDevices.size(); ++i)
        if (wantOut[0] && g_outputDevices[i].name == wantOut) { g_selectedOutputIdx = i; break; }
    {
        std::lock_guard<std::mutex> lk(g_deviceMutex);
        if (!g_inputDevices.empty())  g_currentInputId  = g_inputDevices[g_selectedInputIdx].id;
        if (!g_outputDevices.empty()) g_currentOutputId = g_outputDevices[g_selectedOutputIdx].id;
    }
    g_setInIdx = g_selectedInputIdx; g_setOutIdx = g_selectedOutputIdx;
    // Estado DSP actual (se aplica encima de los defaults del preset 0)
    int mdl = iniGetInt(L"state", L"model", g_params.model.load());
    if (mdl >= 0 && mdl < 13) g_params.model.store(mdl);
    float mv = g_params.master.load();
    iniGetFlt(L"state", L"master", &mv, 1);
    // Un master fuera de rango se IGNORA (y queda el de fabrica): el DSP hace
    // pow(master/100, 1.3) y con base negativa devolveria NaN en toda la cadena.
    if (mv >= 0.0f && mv <= 100.0f) g_params.master.store(mv);
    float tv = g_params.inTrim.load();
    iniGetFlt(L"state", L"inTrim", &tv, 1);
    if (tv < -18.0f) tv = -18.0f;
    if (tv >  18.0f) tv =  18.0f;
    g_params.inTrim.store(tv);
    {
        int cmv = g_params.cabModel.load(); iniGetInt(L"state", L"cabModel", cmv);
        int cmc = g_params.cabMic.load();   iniGetInt(L"state", L"cabMic", cmc);
        int cdf = g_params.cabDist.load();  iniGetInt(L"state", L"cabDist", cdf);
        if (cmv < 0 || cmv > 6) cmv = 0;
        if (cmc < 0 || cmc > 2) cmc = 0;
        if (cdf < 0 || cdf > 2) cdf = 1;
        int ceng = g_cabEngine.load(); iniGetInt(L"state", L"cabEngine", ceng);
        g_cabEngine.store((ceng == 0) ? 0 : 1);
        g_params.cabModel.store(cmv);
        g_params.cabMic.store(cmc);
        g_params.cabDist.store(cdf);
    }
    float mxv = 100.0f; iniGetFlt(L"state", L"mixCab", &mxv, 1);
    if (mxv < 0.0f) mxv = 0.0f; if (mxv > 100.0f) mxv = 100.0f;
    g_params.mixCab.store(mxv);
    {
        wchar_t irp[MAX_PATH] = L"";
        GetPrivateProfileStringW(L"cab", L"irPath", L"", irp, MAX_PATH, g_iniPath.c_str());
        if (irp[0]) {   // el .ini guarda rutas relativas cuando el WAV vive junto al exe
            const std::wstring abs = iniPathLoad(irp);
            g_cabIRPath = abs;
            (void)loadIRFromWav(abs.c_str());   // silencioso si el WAV no sirve
        }
    }
    {
        wchar_t fm[16] = L"";
        GetPrivateProfileStringW(L"state", L"favMask", L"", fm, 16, g_iniPath.c_str());
        if (fm[0]) {
            // La mascara de favoritos de AMPLIFICADORES (la estrella de su lista).
            g_favMask = (DWORD)wcstoul(fm, nullptr, 16);
        } else {
            // .ini de antes del bitmask: el bool suelto marcaba el ampli cargado.
            const int mk = g_params.model.load();
            if (mk >= 0 && mk < 32 && iniGetInt(L"state", L"fav", 0) != 0)
                g_favMask |= (1u << mk);
        }
    }
    presetFavLoadFromIni();
    for (int a = 0; a < 13; ++a) {
        wchar_t key[16]; swprintf_s(key, L"a%d", a);
        float d[8] = { 0,0,0,0,0,0,50,50 };   // [6][7] = controles extra, neutros en 50
        iniGetFlt(L"amp", key, d, 8);
        bool any = false;
        for (int k = 0; k < 5; ++k) if (d[k] != 0.0f) any = true;
        if (any && d[5] == 0.0f) d[5] = 50.0f;    // ini antiguo sin PRESENCE -> neutro
        if (!any) continue;                       // sin datos: banco de fabrica
        for (int k = 6; k < 8; ++k)                        // ini sin los controles extra
            if (d[k] < 0.0f || d[k] > 100.0f) d[k] = ampExtraNeutral(ampExtraKindAt(a, k - 6));
        // El resto tambien se acota: estos valores entran directos al DSP (el
        // master pasa por pow(base<0, exp) y da NaN, y la realimentacion del
        // delay/reverb oscila). Un .ini editado a mano no puede envenenar el audio.
        for (int k = 0; k < 6; ++k)
            if (!(d[k] >= 0.0f && d[k] <= 100.0f)) d[k] = 50.0f;
        for (int k = 0; k < 8; ++k) g_params.amp[a][k].store(d[k]);
    }
    for (int f = 0; f < 18; ++f) {
        wchar_t key[16]; swprintf_s(key, L"f%d", f);
        float d[2] = { 0,0 };
        iniGetFlt(L"fx", key, d, 2);
        // Acotado al recorrido de un mando (0..100, y el THRESHOLD del gate a
        // -80): estos numeros entran directos al DSP y son la realimentacion de
        // delay/reverb/phaser. El .ini real ya traia un f0 = -50 sin limite.
        const int lo0 = (f == 0) ? -80 : 0;
        if (!(d[0] >= (float)lo0 && d[0] <= 100.0f)) d[0] = (float)lo0;
        if (!(d[1] >= 0.0f && d[1] <= 100.0f))       d[1] = 50.0f;
        g_params.fx[f][0].store(d[0]);
        g_params.fx[f][1].store(d[1]);
    }
    {
        int on[18] = {};
        iniGetIntL(L"fx", L"on", on, 18);
        for (int f = 0; f < 18; ++f) g_params.fxOn[f].store(on[f] != 0);
    }
    {
        int pre[6] = { 0,0,0,0,0,0 }, post[6] = { 0,0,0,0,0,0 };
        iniGetIntL(L"chain", L"pre", pre, 6);
        iniGetIntL(L"chain", L"post", post, 6);
        wchar_t bp[128] = L"", bq[128] = L"";
        GetPrivateProfileStringW(L"chain", L"pre", L"", bp, 128, g_iniPath.c_str());
        GetPrivateProfileStringW(L"chain", L"post", L"", bq, 128, g_iniPath.c_str());
        std::vector<int> pv, qv;
        wchar_t* ctx = nullptr;
        for (wchar_t* tok = wcstok_s(bp, L",", &ctx); tok; tok = wcstok_s(nullptr, L",", &ctx)) {
            int tv = _wtoi(tok);
            if (tv >= 0 && tv < 18) pv.push_back(tv);
        }
        ctx = nullptr;
        for (wchar_t* tok = wcstok_s(bq, L",", &ctx); tok; tok = wcstok_s(nullptr, L",", &ctx)) {
            int tv = _wtoi(tok);
            if (tv >= 0 && tv < 18) qv.push_back(tv);
        }
        if (bp[0] || bq[0]) publishChain(pv, qv);
    }
    userPresetsLoadFromIni();
    int pidx = iniGetInt(L"state", L"preset", 0);
    // Migracion del sistema viejo (un preset por amp, indices de usuario en 12):
    // el indice guardado apunta ahora al lote del amp que sonaba.
    const int pver = iniGetInt(L"state", L"presetVer", 1);
    if (pver < 2 && pidx >= 0) {
        if (pidx < 13) pidx *= kPresetsPerAmp;                 // preset unico -> primer lote del amp
        else           pidx = kNumPresets + (pidx - 12);       // primer preset de usuario
    }
    if (pidx < 0) pidx = 0;
    if (pidx >= presetTotal()) pidx = presetTotal() - 1;
    if (pidx < 0) pidx = 0;
    g_presetIdx = pidx;
    g_sceneDirty.store(true);   // el modelo cargado puede diferir del inicial

    // TONE3000: clave publicable y sesion guardada
    {
        // El campo de clave NO se rellena con lo guardado: una clave vieja o que
        // no vale dejaba el campo ocupado para siempre, asi que CONECTAR fallaba
        // siempre en la misma piedra y el camino comodo (campo vacio = sesion
        // instalada) no llegaba a ejecutarse nunca.
        wchar_t k[160] = L"";
        wchar_t tk[1024] = L"", rf[1024] = L"";
        GetPrivateProfileStringW(L"state", L"t3kToken", L"", tk, 1024, g_iniPath.c_str());
        GetPrivateProfileStringW(L"state", L"t3kRefresh", L"", rf, 1024, g_iniPath.c_str());
        if (tk[0]) {
            g_t3kToken = t3kUnprotect(asciiOf(tk));
            if (rf[0]) g_t3kRefresh = t3kUnprotect(asciiOf(rf));
            // Si el token guardado no se puede descifrar (otro usuario o otra
            // maquina), NO se marca conectado: pediria el token en cada intento y
            // el usuario no entenderia por que falla todo.
            g_t3kConnected = !g_t3kToken.empty();
        }
    }

    // Cadena del cabezal PERFIL: se recarga cada bloque guardado que siga existiendo
    namChainLoadFromIni();
    // El rig con el que arranca la app ES el del preset guardado: sin esto, la
    // marca de MODIFICADO saldria encendida nada mas abrir (la comparacion es
    // contra el rig del ultimo applyPreset, que era el preset 0).
    rigMark();
}

// --- Aviso efimero (arriba, centrado): lo pinta la capa dinamica ---
// El temporizador de 16 ms del app repinta mientras esta vivo, asi que no hace
// falta invalidar nada al caducar.
static WCHAR g_toast[80] = L"";
static DWORD g_toastT0 = 0;
static const DWORD kToastMs = 2600;
static bool toastVisible() { return g_toast[0] != 0 && (GetTickCount() - g_toastT0) < kToastMs; }
static void toast(const WCHAR* msg) {
    wcsncpy_s(g_toast, msg, _TRUNCATE);
    g_toastT0 = GetTickCount();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);   // que se vea al instante
}

// Vuelca el rig de ahora al slot `slot` de la lista del usuario, con ese nombre,
// y lo deja seleccionado. El tope no rota ni pisa a nadie en silencio: avisa.
static bool writeUserPreset(int slot, const WCHAR* name) {
    if (slot < 0 || slot >= kMaxUserPresets) { toast(L"SIN ESPACIO: BORRA UN PRESET"); return false; }
    UserPreset u = {};
    RigState s; rigCapture(s);
    u.model = (s.model >= 0 && s.model < kAmpCount) ? s.model : 0;
    for (int k = 0; k < 6; ++k) u.amp[k] = s.amp[k];
    for (int f = 0; f < 18; ++f) {
        u.fx[f][0] = s.fx[f][0];
        u.fx[f][1] = s.fx[f][1];
        u.fxOn[f]  = s.fxOn[f] != 0;
    }
    u.master   = s.master;
    u.cabModel = s.cabModel;
    u.cabMic   = s.cabMic;
    u.cabDist  = s.cabDist;
    u.preCount  = s.preCount;  for (int i = 0; i < s.preCount;  ++i) u.pre[i]  = s.pre[i];
    u.postCount = s.postCount; for (int i = 0; i < s.postCount; ++i) u.post[i] = s.post[i];
    if (slot >= (int)g_userPresets.size()) g_userPresets.resize(slot + 1);
    // Lo que NO es el rig y sobrevive a GUARDAR: el color de favorito y los
    // metadatos del navegador. La fecha de creacion se estrena la primera vez;
    // la de uso es ahora.
    u.favColor = g_userPresets[slot].favColor;
    wcsncpy_s(u.author,  g_userPresets[slot].author,  _TRUNCATE);
    wcsncpy_s(u.comment, g_userPresets[slot].comment, _TRUNCATE);
    u.tags    = g_userPresets[slot].tags;
    u.created = g_userPresets[slot].created ? g_userPresets[slot].created : nowUnix();
    u.used    = nowUnix();
    wcsncpy_s(u.name, name, _TRUNCATE);
    g_userPresets[slot] = u;
    g_presetIdx = kNumPresets + slot;
    g_sceneDirty.store(true);   // la lista cambia de contenido
    saveIni();                  // persistir al instante
    rigMark();                  // lo guardado es el rig de ahora: ya no hay nada modificado
    return true;
}
// GUARDAR sobre un preset del usuario: lo pisa y CONSERVA su nombre. Nunca
// sobrescribe otro preset en silencio ni rota por detrÃ¡s.
static void saveUserPreset() {
    if (!isUserPreset(g_presetIdx)) return;
    const int slot = g_presetIdx - kNumPresets;
    WCHAR nm[64];
    wcsncpy_s(nm, g_userPresets[slot].name, _TRUNCATE);
    if (writeUserPreset(slot, nm)) toast(L"PRESET ACTUALIZADO");
}
// GUARDAR COMO: un preset NUEVO con el nombre que el usuario escriba (el modal
// de renombrar, en su modo de guardar). Antes el boton GUARDAR, con un preset de
// fabrica delante, creaba uno nuevo por detras con un nombre automatico
// ("MIS PRESET 07") y sin preguntar nada.
static void savePresetAs(const WCHAR* nm) {
    if (!nm || !nm[0]) { toast(L"EL PRESET NECESITA UN NOMBRE"); return; }
    const int slot = (int)g_userPresets.size();
    if (slot >= kMaxUserPresets) { toast(L"SIN ESPACIO: BORRA UN PRESET"); return; }
    // GUARDAR CON METADATOS: el preset nuevo nace con el CARACTER del que se
    // estaba oyendo (y su color de favorito), para que se pueda encontrar por la
    // etiqueta en el navegador. Antes nacia sin ninguna etiqueta y solo
    // aparecia en "sin etiqueta".
    const int src = g_presetIdx;
    if (writeUserPreset(slot, nm)) {
        if (src >= 0 && src < presetTotal() && src != kNumPresets + slot) {
            g_userPresets[slot].tags     = presetTags(src) & ((1u << kCharCount) - 1);
            g_userPresets[slot].favColor = (unsigned char)presetFavColor(src);
            wcsncpy_s(g_userPresets[slot].author, presetAuthor(src), _TRUNCATE);
            saveIni();
        }
        toast(L"PRESET GUARDADO");
    }
}

// Renombra un preset de usuario (el modal de la lista llama aqui)
static void renameUserPreset(int idx, const WCHAR* nm) {
    if (!isUserPreset(idx) || !nm || !nm[0]) return;
    wcsncpy_s(g_userPresets[idx - kNumPresets].name, nm, _TRUNCATE);
    saveIni();
    toast(L"PRESET RENOMBRADO");
}

// Borra un preset de usuario. El indice activo se recoloca para no quedar fuera
// de rango (la lista se acorta en uno).
static void deleteUserPreset(int idx) {
    if (!isUserPreset(idx)) return;
    g_userPresets.erase(g_userPresets.begin() + (idx - kNumPresets));
    if (g_presetIdx >= presetTotal()) g_presetIdx = presetTotal() - 1;
    if (g_presetIdx < 0) g_presetIdx = 0;
    saveIni();
    toast(L"PRESET BORRADO");
}

// --- Modal de renombrado: buffer de escritura y estado ---
// El mismo modal hace dos cosas, porque son el mismo gesto (escribir el nombre
// de un preset): RENOMBRAR uno del usuario y GUARDAR COMO (crear uno nuevo).
static bool  g_renameOpen = false;
static bool  g_renameSave = false;       // true = GUARDAR COMO (crea), false = renombrar
static int   g_renameIdx  = -1;          // preset de usuario que se renombra
static WCHAR g_renameBuf[64] = L"";
static int   g_delArm     = -1;          // preset armado para borrar (0 clics = -1)
// Pedal armado para quitar: mismo patron de dos clics que los presets, con
// estado propio porque el indice no es el mismo (fila*100 + posicion, ya que
// el slot 0 de ANTES y el slot 0 de DESPUES son pedales distintos).
static int   g_pedDelArm  = -1;
static DWORD g_pedDelArmAt = 0;          // GetTickCount del armado: caduca

// ============================================================================
//  Grabadora WAV de la salida procesada (hilo de escritura propio)
// ============================================================================
static std::atomic<bool>  g_recActive{false};
static std::atomic<bool>  g_recStopReq{false};
static std::atomic<int>   g_recRate{48000};
static RingBuffer         g_recRing;       // estereo interleaved
static std::vector<float> g_recScratch;    // solo la usa el hilo de audio
static std::thread        g_recThread;
static FILE*              g_recFile = nullptr;
static std::wstring       g_recPath;
static DWORD              g_recTickStart = 0;

static void recWriterProc() {
    std::vector<float> chunk(8192);
    unsigned long long frames = 0;
    for (;;) {
        const bool stop = g_recStopReq.load(std::memory_order_relaxed);
        size_t got = g_recRing.pop(chunk.data(), chunk.size());
        if (got) {
            fwrite(chunk.data(), 4, got, g_recFile);
            frames += got / 2;
        } else if (stop) {
            break;                 // peticion de parada y buffer vacio
        } else {
            Sleep(5);
        }
    }
    if (g_recFile) {
        // Parchear cabecera WAV con los tamanios reales
        const unsigned long long dataBytes = frames * 8ull;   // 2 ch * 4 B float
        unsigned riff = (unsigned)(36ull + dataBytes);
        unsigned dsz  = (unsigned)dataBytes;
        fseek(g_recFile, 4, SEEK_SET);  fwrite(&riff, 4, 1, g_recFile);
        fseek(g_recFile, 40, SEEK_SET); fwrite(&dsz,  4, 1, g_recFile);
        fclose(g_recFile);
        g_recFile = nullptr;
    }
}

static bool recStart() {
    if (g_recActive.load()) return false;
    std::wstring dir = exeDirW() + L"\\Grabaciones";
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t fn[128];
    swprintf_s(fn, L"\\GuitarAmp_%04d-%02d-%02d_%02d-%02d-%02d.wav",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_recPath = dir + fn;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_recPath.c_str(), L"wb") != 0 || !f) return false;
    int rate;
    { std::lock_guard<std::mutex> lk(g_settingsMutex); rate = kRates[g_appliedSet.rateIdx]; }
    g_recRate.store(rate);
    // Cabecera WAV float32 estereo (tamanios a 0: se parchean al parar)
    unsigned char hdr[44] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 3,0, 2,0,
        0,0,0,0, 0,0,0,0,  8,0,  32,0,
        'd','a','t','a', 0,0,0,0
    };
    unsigned r = (unsigned)rate;
    memcpy(hdr + 24, &r, 4);
    unsigned br = r * 8u;
    memcpy(hdr + 28, &br, 4);
    fwrite(hdr, 1, 44, f);
    g_recFile = f;
    g_recRing.clear();   // la capacidad ya estaba reservada al abrir el dispositivo
    g_recStopReq.store(false);
    g_recThread = std::thread(recWriterProc);
    g_recTickStart = GetTickCount();
    g_recActive.store(true);
    return true;
}

static void recStop() {
    if (!g_recActive.load()) {
        if (g_recThread.joinable()) { g_recStopReq.store(true); g_recThread.join(); }
        return;
    }
    g_recActive.store(false);     // el hilo de audio deja de empujar
    g_recStopReq.store(true);
    if (g_recThread.joinable()) g_recThread.join();   // drena y parchea el WAV
}

// ============================================================================
//  Afinador cromatico: ACF sobre la entrada decimada (solo con el modal abierto)
// ============================================================================
static const int TUN_N = 2048;
static float       tunBuf[TUN_N];
static int         tunW = 0, tunFill = 0, tunNew = 0, tunDecCnt = 0;
static DWORD       tunLastAn = 0;
static int    tunDec = 4;          // factor de decimacion (~fs/12000)
static float  tunFs = 48000.0f;

// Preparacion por sesion del afinador. Una sola fuente para el hilo de audio y
// para el autotest: tunBuf se llena DECIMADO, asi que la tasa del analisis es
// fs/tunDec, no fs (con fs a secas la frecuencia informada salia x tunDec).
static void tunerPrepare(float fs) {
    tunDec = (int)(fs / 12000.0f); if (tunDec < 1) tunDec = 1;
    tunFs  = fs / (float)tunDec;
    tunW = tunFill = tunNew = tunDecCnt = 0;
    tunLastAn = 0;
}

static void tunerAnalyze() {
    if (tunFill < TUN_N/2) return;
    // Ventana de 1024 muestras recientes con peso de Hann
    static std::vector<float> w(1024), re(1024), im(1024);
    const int start = tunW - 1024;      // puede ser negativo: el modulo lo corrige
    for (int i = 0; i < 1024; ++i) {
        const int   idx  = (start + i + TUN_N*4) % TUN_N;
        const float hann = 0.5f - 0.5f*std::cos(2.0f*kPi*(float)i/1023.0f);
        w[i] = tunBuf[idx]*hann;
    }
    float rms = 0.0f;
    for (int i = 0; i < 1024; ++i) rms += w[i]*w[i];
    rms = std::sqrt(rms/1024.0f);
    g_tunerLevel.store(rms);
    if (rms < 0.004f) { g_tunerNote.store(0); g_tunerHz.store(0.0f); return; }
    // ACF por FFT: ifft(|W|^2) = correlacion circular sin normalizar. Hay que
    // DES-SESGARLA por la propia ventana (sum w[i]*w[i+lag]): sin eso la caida
    // de la ventana hace ganar a los lags cortos y el afinador informaba la
    // frecuencia del limite inferior de la busqueda, no la de la cuerda.
    static std::vector<float> wA;
    if (wA.empty()) {
        std::vector<float> wf(1024);
        for (int i = 0; i < 1024; ++i) wf[i] = 0.5f - 0.5f*std::cos(2.0f*kPi*(float)i/1023.0f);
        wA.assign(1024, 0.0f);
        for (int L = 0; L < 1024; ++L) {
            double s = 0.0;
            for (int i = 0; i < 1024; ++i) s += (double)wf[i]*(double)wf[(i + L) & 1023];
            wA[L] = (float)s;
        }
    }
    re = w; im.assign(1024, 0.0f);
    fftRadix2(re, im, false);
    for (int i = 0; i < 1024; ++i) {
        const float pr = re[i]*re[i] + im[i]*im[i];
        re[i] = pr; im[i] = 0.0f;
    }
    fftRadix2(re, im, true);
    for (int i = 0; i < 1024; ++i) re[i] /= (wA[i] + 1e-6f);
    const float acf0 = std::max(re[0], 1e-9f);
    // Buscar el maximo en el rango de frecuencias de guitarra/bajo. Los lags
    // son de tunBuf, que corre a tunFs (~12 kHz): 65 Hz .. 1200 Hz.
    const int lagLo = std::max(2,    (int)(tunFs/1200.0f));
    const int lagHi = std::min(1022, (int)(tunFs/65.0f));
    if (lagHi <= lagLo) { g_tunerNote.store(0); g_tunerHz.store(0.0f); return; }
    // El ACF tiene un pico en CADA multiplo del periodo, asi que el maximo
    // global da errores de octava (una cuerda de 329 Hz se informaba como 165).
    // Se coge el PRIMER maximo local que supera el umbral; si el ataque o el
    // ruido dejan el ACF bajo, se cae al maximo global para no quedarse mudo.
    int   best  = -1;
    float bestV = 0.0f;
    int   gmax  = lagLo;
    float gmaxV = -1e9f;
    for (int lag = lagLo; lag <= lagHi; ++lag) {
        if (re[lag] > gmaxV) { gmaxV = re[lag]; gmax = lag; }
        if (best >= 0) continue;
        if (re[lag] >= re[lag-1] && re[lag] >= re[lag+1] && re[lag] >= 0.80f*acf0) {
            best = lag; bestV = re[lag];
        }
    }
    if (best < 0) { best = gmax; bestV = gmaxV; }
    const float conf = bestV/acf0;
    if (conf < 0.5f) { g_tunerNote.store(0); g_tunerHz.store(0.0f); return; }
    // Interpolacion parabolica del lag -> frecuencia mas fina
    double lagD = (double)best;
    if (best > lagLo && best < lagHi) {
        const double y0 = re[best-1], y1 = re[best], y2 = re[best+1];
        const double den = y0 - 2.0*y1 + y2;
        if (std::fabs(den) > 1e-12) lagD += 0.5*(y0 - y2)/den;
    }
    const float f0 = tunFs/(float)lagD;
    if (f0 < 60.0f || f0 > 1400.0f) { g_tunerNote.store(0); g_tunerHz.store(0.0f); return; }
    // Nota MIDI + centavos de desvio respecto a A4 = 440 Hz
    int midi = (int)std::lround(69.0 + 12.0*std::log2((double)f0/440.0));
    if (midi < 24)  midi = 24;
    if (midi > 108) midi = 108;
    const double ref = 440.0*std::pow(2.0, (midi - 69)/12.0);
    g_tunerNote.store(midi);
    g_tunerHz.store(f0);
    g_tunerCents.store((float)(1200.0*std::log2((double)f0/ref)));
}

// ============================================================================
//  Limite del BLOQUE de audio
// ============================================================================
// El hilo de audio trabaja por bloques: al principio de cada uno copia la
// cadena de pedales publicada y refresca la instantanea del perfil, y despues
// procesa las muestras con esa foto fija. Antes ese trabajo estaba en linea
// dentro del bucle de captura; aqui esta aislado porque es EL sitio donde el
// camino de tiempo real no puede coger cerrojos ni reservar memoria, y ademas
// se puede probar (T12/T13 llaman a esta misma funcion).
static void audioBlockBegin() {
    // Marca de bloque ANTES de leer el perfil: asi la interfaz sabe que bloques
    // han empezado cuando retira una cadena (ver namRetiroFlush).
    g_audioBlocks.fetch_add(1, std::memory_order_release);
    // Instantanea del perfil del bloque: una sola carga atomica, sin cerrojo.
    g_amp.namBlk = g_namAudio.load(std::memory_order_acquire);
    g_amp.namConfigure();          // tasa de cada captura (no hace nada si no cambia)
    g_namSnapGets.fetch_add(1, std::memory_order_relaxed);
    // Cadena de pedales del bloque: copia sin cerrojo y sin reservar memoria.
    chainSnapRead();
}

// ============================================================================
//  Hilo de audio: captura -> DSP -> reproduccion (WASAPI event-driven)
// ============================================================================
// Balistica de VU, expresada en TIEMPO y no por bloque. Antes la caida era un
// 0.82 fijo por buffer: la misma nota caia mas rapido con buffer pequeno que
// con buffer grande, y el medido bailaba distinto segun la configuracion de
// audio de cada maquina. Ataque ~10 ms (el transitorio se ve), caida ~250 ms
// (la aguja se lee y no parpadea), y salto maximo de 250 ms al reanudar.
static void updateMeter(std::atomic<float>& m, float pk) {
    static ULONGLONG tPrev  = 0;      // instante del bloque anterior (solo hilo audio)
    static double    dtPrev = 0.010;  // ultimo intervalo util (GetTickCount64 es grueso)
    const ULONGLONG  now    = GetTickCount64();
    double dt = dtPrev;
    if (now > tPrev) {
        dt = (double)(now - tPrev)*1e-3;
        if (dt > 0.25) dt = 0.25;
        dtPrev = dt;
    }
    tPrev = now;
    if (dt < 0.0002) dt = 0.0002;
    const float kAtk = (float)(1.0 - std::exp(-dt/0.010));
    const float kRel = (float)(1.0 - std::exp(-dt/0.250));
    float cur = m.load(std::memory_order_relaxed);
    float v   = (pk > cur) ? (cur + (pk - cur)*kAtk)
                           : (cur + (pk - cur)*kRel);
    if (v > 1.0f) v = 1.0f;
    if (v < 0.0f) v = 0.0f;
    m.store(v, std::memory_order_relaxed);
}

// --- GA_PROF: salud del hilo de audio (los lee profPaint en la UI) ---------
static std::atomic<long long> g_xrunUnderrun{0};  // eventos con el ring corto
static std::atomic<long long> g_xrunLate{0};      // huecos > 2,5 periodos de buffer
static std::atomic<long long> g_xrunMaxGap{0};    // mayor hueco observado (ms)
static std::atomic<long long> g_xrunShortMax{0};  // peor hueco del anillo (muestras)

// --- Puente captura -> reproduccion (solo el hilo de audio) -----------------
//  El render pide un periodo por evento y del anillo no siempre hay un periodo
//  entero: los dos flujos de WASAPI no entregan en el mismo instante y, en modo
//  compartido con AUTOCONVERTPCM, la conversion de frecuencia hace que el numero
//  de muestras por paquete no cuadre exacto (pedir 44100 sobre una interfaz de
//  48000 es el caso normal). Antes esos huecos se rellenaban con CEROS: un
//  escalon de senal a cero y de vuelta, o sea el "crack" que se oye de vez en
//  cuando al tocar. Ahora se mantiene la ultima muestra y se desvanece en unos
//  milisegundos, que solo se nota si el hueco es largo de verdad (un corte).
// Siguiente muestra del relleno de un hueco: se mantiene la ultima y se
// desvanece poco a poco (ver g_bridgeLast). Aislado para poder probarlo: lo que
// NO puede pasar es que el hueco meta un escalon a cero, que es el chasquido.
static inline float bridgeNextGapSample(float& last, float& fade, float fadeK)
{
    const float s = last*fade;
    fade *= fadeK;
    return s;
}
static float g_bridgeLast = 0.0f;
static float g_bridgeFade = 1.0f;
static int   g_bridgeCushion = 0;   // muestras de colchon metidas al arrancar
// Rampa del interruptor AUDIO: cortar la senal en una muestra es un escalon
// (chasquido al encender y al apagar la app). 10 ms bastan para que no se oiga.
static float g_enGain = 0.0f;
static ULONGLONG              g_paLastEv = 0;     // instante del ultimo evento WASAPI

// --- VIGILANCIA DE VOLUMEN (SOLO LECTURA) -----------------------------------
//  El motor abre el endpoint en modo COMPARTIDO y no escribe jamas el volumen ni
//  el mute del sistema: no hay ninguna llamada a SetMasterVolumeLevel* / SetMute
//  en todo el proyecto. Aqui solo se LEE el nivel (por defecto y el de la salida
//  del motor) y se anota en GuitarAmp-audio.log cuando cambia, con la hora y si
//  el motor tenia stream abierto: asi se ve quien lo baja y se descarta que
//  seamos nosotros. Solo lectura tambien por diseno: nada de este bloque puede
//  alterar el audio del sistema.
static std::wstring      g_outDevId;            // id del endpoint de salida del motor
static std::mutex        g_outDevIdMx;
static std::atomic<bool> g_volWatchStop{false};

static void volLogLine(const char* txt) {
    WCHAR p[MAX_PATH];
    swprintf_s(p, L"%s\\GuitarAmp-audio.log", exeDirW().c_str());
    FILE* f = nullptr;
    if (_wfopen_s(&f, p, L"a") != 0 || !f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, txt);
    fclose(f);
}

static bool readEndpointVol(const wchar_t* idOrNull, float* vol, int* mute) {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en) return false;
    IMMDevice* dev = nullptr;
    const HRESULT hrDev = idOrNull ? en->GetDevice(idOrNull, &dev)
                                   : en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    en->Release();
    if (FAILED(hrDev) || !dev) return false;
    IAudioEndpointVolume* ev = nullptr;
    const HRESULT hrEv = dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&ev);
    dev->Release();
    if (FAILED(hrEv) || !ev) return false;
    float v = 0.0f; BOOL m = FALSE;
    ev->GetMasterVolumeLevelScalar(&v);   // lectura, nunca escritura
    ev->GetMute(&m);
    ev->Release();
    if (vol)  *vol  = v;
    if (mute) *mute = m ? 1 : 0;
    return true;
}

static void volSnapshot(const char* tag) {
    float v = 0.0f; int m = 0;
    char b[320];
    if (!readEndpointVol(nullptr, &v, &m)) {
        sprintf_s(b, "%s endpoint por defecto ilegible", tag);
    } else {
        sprintf_s(b, "%s endpoint por defecto: vol=%.5f mute=%d (motor activo=%d)",
                  tag, v, m, g_params.audioActive.load() ? 1 : 0);
    }
    volLogLine(b);
    std::wstring id;
    { std::lock_guard<std::mutex> lk(g_outDevIdMx); id = g_outDevId; }
    if (!id.empty() && readEndpointVol(id.c_str(), &v, &m)) {
        sprintf_s(b, "%s salida del motor: vol=%.5f mute=%d (motor activo=%d)",
                  tag, v, m, g_params.audioActive.load() ? 1 : 0);
        volLogLine(b);
    }
}

static DWORD WINAPI volWatchProc(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    float prevDef = -2.0f, prevOut = -2.0f;
    int   prevDefM = -1,  prevOutM = -1;
    volLogLine("vigilancia de volumen: SOLO LECTURA (el motor no escribe el volumen del sistema)");
    while (!g_volWatchStop.load() && !g_audioQuit.load()) {
        float v = 0.0f; int m = 0;
        char b[320];
        if (readEndpointVol(nullptr, &v, &m)) {
            if (prevDef < -1.0f || fabsf(v - prevDef) > 0.0005f || m != prevDefM) {
                sprintf_s(b, "CAMBIO endpoint por defecto: vol=%.5f (antes %.5f) mute=%d (antes %d) motor activo=%d",
                          v, (prevDef < -1.0f ? -1.0f : prevDef), m, prevDefM,
                          g_params.audioActive.load() ? 1 : 0);
                volLogLine(b);
                prevDef = v; prevDefM = m;
            }
        }
        std::wstring id;
        { std::lock_guard<std::mutex> lk(g_outDevIdMx); id = g_outDevId; }
        if (!id.empty() && readEndpointVol(id.c_str(), &v, &m)) {
            if (prevOut < -1.0f || fabsf(v - prevOut) > 0.0005f || m != prevOutM) {
                sprintf_s(b, "CAMBIO salida del motor: vol=%.5f (antes %.5f) mute=%d (antes %d) motor activo=%d",
                          v, (prevOut < -1.0f ? -1.0f : prevOut), m, prevOutM,
                          g_params.audioActive.load() ? 1 : 0);
                volLogLine(b);
                prevOut = v; prevOutM = m;
            }
        }
        for (int i = 0; i < 20 && !g_volWatchStop.load() && !g_audioQuit.load(); ++i) Sleep(50);
    }
    CoUninitialize();
    return 0;
}

// ---------------------------------------------------------------------------
//  Prioridad de tiempo real del hilo de audio
// ---------------------------------------------------------------------------
// SetThreadPriority(TIME_CRITICAL) no basta: en un pico del sistema (mientras la
// interfaz pinta, por ejemplo) el hilo de audio puede quedarse fuera y ese
// desalojo se oye como un crujido. Aqui se pide la prioridad por sesion, para
// que las esperas entre sesiones (los Sleep del arranque y del cierre) no corran
// con el hilo subido de prioridad.
//
// Se pide la clase "Pro Audio" de MMCSS, que es la que usa cualquier host de
// audio serio: el sistema le reserva CPU y lo mantiene por delante del trabajo
// normal. Si el registro no se concede, queda la prioridad de siempre (que ya es
// mejor que la normal, solo que sin reserva de CPU).
static bool g_rtBoostMmcss = false;   // lo que consiguio la ultima sesion
static HANDLE audioRtEnter() {
    DWORD tarea = 0;
    HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &tarea);
    if (h) {
        g_rtBoostMmcss = true;
        return h;
    }
    g_rtBoostMmcss = false;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    return nullptr;
}
static void audioRtLeave(HANDLE h) {
    if (h) { AvRevertMmThreadCharacteristics(h); return; }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);   // reserva: se quita
}

static DWORD WINAPI AudioThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // Sin denormales: evita crackles por caidas de CPU con senal en silencio
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    // La prioridad la pone audioRtEnter() al arrancar cada sesion (no aqui: los
    // caminos de espera de este hilo no deben correr con prioridad de tiempo real).

    while (!g_audioQuit.load()) {
    g_deviceChanged.store(false);
    HANDLE mmcss = nullptr;      // registro de prioridad de la sesion

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)) || !enumerator) {
        if (!g_audioQuit.load()) Sleep(500);
        continue;
    }

    std::wstring inId, outId;
    AudioSettings st;
    {
        std::lock_guard<std::mutex> lock(g_deviceMutex);
        inId  = g_currentInputId;
        outId = g_currentOutputId;
    }
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        st = g_appliedSet;
    }
    const bool wantInput  = st.enableInput;
    const bool wantOutput = st.enableOutput;
    const int  rate  = kRates[st.rateIdx];
    const int  bsize = kBuffers[st.bufIdx];

    IMMDevice* capDev = nullptr;
    IMMDevice* renDev = nullptr;
    if (wantInput)  capDev = OpenDevice(enumerator, inId,  eCapture);
    if (wantOutput) renDev = OpenDevice(enumerator, outId, eRender);
    if (renDev) {   // guardar el id del endpoint: la vigilancia lo lee (nunca lo escribe)
        LPWSTR rid = nullptr;
        if (SUCCEEDED(renDev->GetId(&rid)) && rid) {
            std::lock_guard<std::mutex> lk(g_outDevIdMx);
            g_outDevId.assign(rid);
            CoTaskMemFree(rid);
        }
    }
    if ((!capDev && wantInput) || (!renDev && wantOutput)) {
        if (capDev) capDev->Release();
        if (renDev) renDev->Release();
        enumerator->Release();
        if (!g_audioQuit.load()) Sleep(500);
        continue;
    }

    IAudioClient* capCli = nullptr;  IAudioClient* renCli = nullptr;
    if (capDev) capDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&capCli);
    if (renDev) renDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&renCli);

    const double bufMs = (double)bsize * 1000.0 / (double)rate;   // GA_PROF

    // Formato solicitado: float32 estereo a la frecuencia elegida
    WAVEFORMATEXTENSIBLE req;
    ZeroMemory(&req, sizeof(req));
    req.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    req.Format.nChannels       = 2;
    req.Format.nSamplesPerSec  = (DWORD)rate;
    req.Format.wBitsPerSample  = 32;
    req.Format.nBlockAlign     = 8;
    req.Format.nAvgBytesPerSec = (DWORD)rate * 8;
    req.Format.cbSize          = 22;
    req.Samples.wValidBitsPerSample = 32;
    req.dwChannelMask          = 0x3;
    req.SubFormat              = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    bool exclusive = (st.system == 1);
    HRESULT hrCap = E_FAIL, hrRen = E_FAIL;
    const WAVEFORMATEX* capFmt = nullptr;
    const WAVEFORMATEX* renFmt = nullptr;
    // Duracion del buffer para N frames a la frecuencia de ESE stream.
    const auto bufFor = [](int frames, int sr) {
        return (REFERENCE_TIME)((double)frames * 10000000.0 / (double)sr + 0.5);
    };
    // Inicializa un stream EXCLUSIVO. La duracion va en el 3er argumento y la
    // periodicidad en 0: antes iba al reves (0, BUF), que es invalido y hacia
    // que exclusivo fallara SIEMPRE y se cayera a compartido en silencio.
    const auto initExcl = [](IAudioClient* cli, const WAVEFORMATEX* fmt, REFERENCE_TIME dur) {
        HRESULT hr = cli->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     (UINT32)dur, 0, fmt, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            // El driver exige un buffer alineado: da el tamano bueno y se
            // reintenta con ESA duracion (patron documentado por Microsoft).
            UINT32 fr = 0;
            if (SUCCEEDED(cli->GetBufferSize(&fr)) && fr > 0) {
                const REFERENCE_TIME d2 =
                    (REFERENCE_TIME)(10000000.0 * fr / fmt->nSamplesPerSec + 0.5);
                hr = cli->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     (UINT32)d2, 0, fmt, nullptr);
            }
        }
        return hr;
    };
    // Formatos que se prueban en EXCLUSIVO, TODOS a la frecuencia PEDIDA. El
    // motor entero (IR del gabinete, afinador, grabadora, medidores) corre a UNA
    // tasa; aceptar el mix del dispositivo a otra frecuencia dejaba el gabinete
    // desafinado un 8,8% (48/44,1). Si el endpoint no acepta ninguno a la tasa
    // pedida, se cae a COMPARTIDO, que convierte a esa misma tasa.
    WAVEFORMATEXTENSIBLE reqPcm16{}, reqPcm24{};
    {
        const auto mkPcm = [&](WAVEFORMATEXTENSIBLE& f, int bits) {
            f = req;
            f.Format.wBitsPerSample = (WORD)bits;
            f.Format.nBlockAlign    = (WORD)(2 * bits / 8);
            f.Format.nAvgBytesPerSec= (DWORD)(rate * 2 * bits / 8);
            f.Samples.wValidBitsPerSample = (WORD)bits;
            f.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        };
        mkPcm(reqPcm16, 16);
        mkPcm(reqPcm24, 24);
    }
    const auto openExcl = [&](IAudioClient* cli, const WAVEFORMATEX** fmtOut) -> HRESULT {
        const WAVEFORMATEX* tries[3] = {
            (const WAVEFORMATEX*)&req,
            (const WAVEFORMATEX*)&reqPcm16,
            (const WAVEFORMATEX*)&reqPcm24
        };
        for (int i = 0; i < 3; ++i) {
            const HRESULT hr = initExcl(cli, tries[i], bufFor(bsize, rate));
            if (SUCCEEDED(hr)) { *fmtOut = tries[i]; return hr; }
        }
        return E_FAIL;
    };

    if (exclusive) {
        if (capCli) hrCap = openExcl(capCli, &capFmt);
        if (renCli) hrRen = openExcl(renCli, &renFmt);
        if (FAILED(hrCap) || FAILED(hrRen)) {
            if (capCli) { capCli->Release(); capCli = nullptr; }
            if (renCli) { renCli->Release(); renCli = nullptr; }
            if (capDev) capDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&capCli);
            if (renDev) renDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&renCli);
            exclusive = false;
            capFmt = renFmt = nullptr;
            hrCap = hrRen = E_FAIL;
        }
    }
    if (!exclusive) {
        // La duracion va en el 3er argumento (hnsBufferDuration) y la
        // periodicidad en 0. Antes iba (0, BUF) y el motor IGNORABA el ajuste de
        // BUFFER: todas las opciones daban el mismo buffer del sistema.
        const REFERENCE_TIME dur = bufFor(bsize, rate);
        const DWORD sflags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                             AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if (capCli) hrCap = capCli->Initialize(AUDCLNT_SHAREMODE_SHARED, sflags,
                                               (UINT32)dur, 0, (WAVEFORMATEX*)&req, nullptr);
        if (renCli) hrRen = renCli->Initialize(AUDCLNT_SHAREMODE_SHARED, sflags,
                                               (UINT32)dur, 0, (WAVEFORMATEX*)&req, nullptr);
        if (SUCCEEDED(hrCap)) capFmt = (const WAVEFORMATEX*)&req;
        if (SUCCEEDED(hrRen)) renFmt = (const WAVEFORMATEX*)&req;
    }
    if ((wantInput && FAILED(hrCap)) || (wantOutput && FAILED(hrRen))) {
        if (capCli) capCli->Release();
        if (renCli) renCli->Release();
        if (capDev) capDev->Release();
        if (renDev) renDev->Release();
        enumerator->Release();
        if (!g_audioQuit.load()) Sleep(500);
        continue;
    }

    // Formato del stream = el que se PIDIO al Initialize (req o el mix). Antes
    // se preguntaba GetMixFormat() otra vez, que devuelve el mix del endpoint y
    // NO el formato aceptado por el stream exclusivo: si el endpoint aceptaba
    // otro, la copia leia bytes con el layout equivocado.
    const WAVEFORMATEX* usedCapFmt = capFmt;
    const WAVEFORMATEX* usedRenFmt = renFmt;
    const auto isFloat = [](const WAVEFORMATEX* f) {
        if (!f) return false;
        if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
        if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            return ((const WAVEFORMATEXTENSIBLE*)f)->SubFormat.Data1 == 3;
        return false;
    };
    const bool capFloat = isFloat(usedCapFmt);
    const bool renFloat = isFloat(usedRenFmt);
    const int  capCh = usedCapFmt ? usedCapFmt->nChannels : 2;
    const int  renCh = usedRenFmt ? usedRenFmt->nChannels : 2;
    const int  capBps = usedCapFmt ? usedCapFmt->wBitsPerSample : 32;
    const int  renBps = usedRenFmt ? usedRenFmt->wBitsPerSample : 32;
    const int  capBytes = (capBps >= 8) ? capBps/8 : 2;
    const int  renBytes = (renBps >= 8) ? renBps/8 : 2;
    const UINT capAlign = usedCapFmt ? usedCapFmt->nBlockAlign : (UINT)(capBytes*capCh);
    const UINT renAlign = usedRenFmt ? usedRenFmt->nBlockAlign : (UINT)(renBytes*renCh);

    // Eventos separados para captura y reproduccion
    HANDLE capEvent = capCli ? CreateEvent(nullptr, FALSE, FALSE, nullptr) : nullptr;
    HANDLE renEvent = renCli ? CreateEvent(nullptr, FALSE, FALSE, nullptr) : nullptr;
    if (capCli && capEvent) capCli->SetEventHandle(capEvent);
    if (renCli && renEvent) renCli->SetEventHandle(renEvent);

    IAudioCaptureClient* capCap = nullptr;  IAudioRenderClient* renCap = nullptr;
    if (capCli) capCli->GetService(__uuidof(IAudioCaptureClient), (void**)&capCap);
    if (renCli) renCli->GetService(__uuidof(IAudioRenderClient),  (void**)&renCap);

    float fs = (float)rate;
    // En modo exclusivo el stream corre al ritmo nativo del dispositivo
    if (exclusive && usedCapFmt && usedCapFmt->nSamplesPerSec > 0)
        fs = (float)usedCapFmt->nSamplesPerSec;
    g_amp.prepare(fs);
    g_recRate.store((int)fs);   // la grabadora usa la frecuencia real de la sesion
    const size_t maxFrames = (size_t)(fs * 0.10f) + 512;
    std::vector<float> procBuf(maxFrames);
    std::vector<float> outBuf(maxFrames * 8);
    g_ring.resize((size_t)(fs * 0.25f));
    // Colchon UNA vez al arrancar: unas muestras de silencio ya dentro del
    // anillo. Sin el, el anillo vive entre 0 y un periodo y cualquier desfase de
    // una o dos muestras entre captura y reproduccion deja el render sin datos.
    // El desfase tipico es pequeÃ±o (los dos flujos son del mismo motor, pero con
    // AUTOCONVERTPCM el paquete de captura no trae un numero entero de muestras
    // respecto al de salida), asi que 1/8 de periodo basta y cuesta ~3 ms con
    // buffer de 1024 y ~0,7 ms con 128.
    {
        static float zeros[2048] = { 0.0f };
        int cush = bsize/8;
        if (cush < 32) cush = 32;
        if (cush > 256) cush = 256;
        g_ring.push(zeros, (size_t)cush);
        g_bridgeCushion = cush;
    }
    g_bridgeLast = 0.0f;
    g_bridgeFade = 1.0f;
    g_xrunShortMax.store(0, std::memory_order_relaxed);
    // El anillo de grabacion reserva aqui su capacidad y NO vuelve a crecer.
    // recStart() corre en el hilo de UI y el de audio puede estar empujando en
    // este mismo instante: un resize() ahi reasignaria el vector mientras se
    // escribe, y el que empuja se quedaria sobre memoria liberada. Se reserva
    // con margen (fs*4 = 2 s de estereo, +4096 para el caso de que la frecuencia
    // nominal y la del dispositivo difieran en modo exclusivo); si aun asi
    // quedara corto, push() descarta lo mas antiguo, que es lo de siempre.
    g_recRing.ensure((size_t)(fs * 4.0f) + 4096);
    // El scratch de intercalado de la grabadora tambien se reserva aqui, con el
    // maximo que puede traer un bloque: grabar no reserva en el hilo de audio.
    g_recScratch.resize(maxFrames*2, 0.0f);
    // Afinador + visualizador: preparar por sesion
    tunerPrepare(fs);
    g_scopeRing.resize((size_t)(fs * 0.06f) + 1024);

    if (capCli) capCli->Start();
    if (renCli) renCli->Start();
    // Lo que CORRE de verdad (la UI lo ensena en la linea de estado).
    {
        UINT32 rf = 0;
        if (renCli && FAILED(renCli->GetBufferSize(&rf))) rf = 0;
        if (rf == 0 && capCli) capCli->GetBufferSize(&rf);
        g_sessRate.store((int)fs);
        g_sessBufFrames.store((int)rf);
        g_sessExclusive.store(exclusive ? 1 : 0);
    }
    g_params.audioActive.store(true);
    // Prioridad de tiempo real de la sesion (MMCSS si esta; T15).
    mmcss = audioRtEnter();
    volSnapshot("arranque del motor:");   // nivel del endpoint al abrir el stream
    g_paLastEv = 0;   // GA_PROF: reiniciar el medidor de huecos de audio

    // Bucle principal: timeout NO destruye la sesion (evita cortes)
    HANDLE evts[2] = { renEvent ? renEvent : capEvent, capEvent ? capEvent : renEvent };
    const bool useBoth = (renEvent && capEvent);
    int devDead = 0;   // llamadas seguidas con DEVICE_INVALIDATED (endpoint ido)
    for (;;) {
        DWORD w = WaitForMultipleObjects(useBoth ? 2 : 1, evts, FALSE, 50);
        if (g_audioQuit.load() || g_deviceChanged.load()) break;
        if (w != WAIT_OBJECT_0 && w != WAIT_OBJECT_0 + 1 && w != WAIT_TIMEOUT) break;
        // GA_PROF: hueco entre eventos (late = mas de 2,5 periodos de buffer)
        {
            const ULONGLONG evNow = GetTickCount64();
            if (g_paLastEv != 0) {
                const long long gap = (long long)(evNow - g_paLastEv);
                if (gap > (long long)(bufMs * 2.5))
                    g_xrunLate.fetch_add(1, std::memory_order_relaxed);
                long long prev = g_xrunMaxGap.load(std::memory_order_relaxed);
                while (gap > prev && !g_xrunMaxGap.compare_exchange_weak(
                           prev, gap, std::memory_order_relaxed)) { }
            }
            g_paLastEv = evNow;
        }

        HRESULT hrCapPoll = S_OK, hrRenPoll = S_OK;   // estado de los endpoints
        float pkIn = 0.0f, pkOut = 0.0f;
        // Trim de entrada (una vez por bloque): el medidor INPUT y el DSP ven
        // la senal ya recortada, como el gain de una preamp real.
        const float trimMul = std::pow(10.0f, g_params.inTrim.load() / 20.0f);
        const float enStep = 1.0f/(0.010f*fs);   // rampa del interruptor AUDIO

        // --- Captura ---
        if (capCap && (w == WAIT_OBJECT_0 + 1 || !useBoth || w == WAIT_TIMEOUT)) {
            const bool en = g_audioEnabled.load();   // silencio global de la app
            // Cadena y perfil del bloque: UNA vez por bloque, no por muestra
            audioBlockBegin();
            UINT32 packet = 0;
            hrCapPoll = capCap->GetNextPacketSize(&packet);
            if (FAILED(hrCapPoll)) packet = 0;
            while (packet > 0) {
                BYTE* capData = nullptr;  UINT32 frames = 0;  DWORD flags = 0;
                if (SUCCEEDED(capCap->GetBuffer(&capData, &frames, &flags, nullptr, nullptr))) {
                    // SILENT = "el buffer no trae datos, tratalo como silencio".
                    // Antes se SALTABA el bloque entero: no se alimentaba el anillo
                    // (el render se quedaba sin datos) y no corria el DSP, asi que
                    // la cola del reverb/delay se cortaba en seco al dejar de
                    // sonar la entrada -- otro chasquido. Ahora se procesa con
                    // ceros: el motor sigue y el anillo cuadra.
                    const bool silentPkt = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    if (frames > 0) {
                        size_t outCount = 0;
                        bool huboNaN = false;
                        for (UINT32 i = 0; i < frames && outCount < maxFrames; ++i) {
                            float s = 0.0f;
                            if (!silentPkt) {
                                if (capFloat) {
                                    const float* in = (const float*)capData;
                                    for (int c = 0; c < capCh; ++c) s += in[i*capCh + c];
                                } else {
                                    const BYTE* in = capData + (size_t)i*capAlign;
                                    for (int c = 0; c < capCh; ++c) {
                                        const BYTE* p = in + (size_t)c*capBytes;
                                        if (capBps == 32) {
                                            int v; memcpy(&v, p, 4);
                                            s += v / 2147483648.0f;
                                        } else if (capBps == 24) {
                                            int v = p[0] | (p[1] << 8) | (p[2] << 16);
                                            if (v & 0x800000) v |= ~0xFFFFFF;
                                            s += v / 8388608.0f;
                                        } else {
                                            short v; memcpy(&v, p, 2);
                                            s += v / 32768.0f;
                                        }
                                    }
                                }
                                s /= (float)capCh;
                            }   // !silentPkt
                            // --- Afinador: decimacion de la entrada (solo con el modal abierto) ---
                            if (g_tunerOpen.load(std::memory_order_relaxed)) {
                                if (++tunDecCnt >= tunDec) {
                                    tunDecCnt = 0;
                                    tunBuf[tunW] = s;
                                    tunW = (tunW + 1) % TUN_N;
                                    if (tunFill < TUN_N) ++tunFill;
                                    if (++tunNew >= 512) {
                                        const DWORD nowT = GetTickCount();
                                        if (nowT - tunLastAn >= 120) {
                                            tunNew = 0; tunLastAn = nowT;
                                            tunerAnalyze();
                                        }
                                    }
                                }
                            }
                            s *= trimMul;               // trim: medidor y cadena
                            float pk = std::fabs(s);
                            if (pk > pkIn) pkIn = pk;
                            // Apagado: entrada sigue medida (para verificar el dispositivo)
                            // y se empuja silencio para que el ring y la sesion no se rompan
                            if (!std::isfinite(s)) s = 0.0f;    // una muestra basura del dispositivo
                            // Interruptor AUDIO con rampa (10 ms). Con el corte a
                            // hueso, encender o apagar la app metia un escalon.
                            // El DSP sigue corriendo mientras la rampa baja, para
                            // que la cola del reverb se apague en vez de cortarse.
                            float y = (en || g_enGain > 1e-4f)
                                    ? g_amp.process(s, g_preSnap, g_postSnap) : 0.0f;
                            g_enGain += en ? enStep : -enStep;
                            if (g_enGain < 0.0f) g_enGain = 0.0f;
                            else if (g_enGain > 1.0f) g_enGain = 1.0f;
                            y *= g_enGain;
                            if (!std::isfinite(y)) {
                                // Tapar la salida no basta: un NaN que entre en el estado de un
                                // biquad o de un buffer realimentado (delay, reverb, tanque) se
                                // queda ahi y envenena el filtro hasta reiniciar el motor. Se
                                // limpia la cadena.
                                //
                                // La muestra sale a cero y la limpieza se pide UNA VEZ por bloque
                                // (huboNaN), no por muestra: esto corre en el hilo de tiempo real,
                                // y prepare() es trabajo pesado. Si la causa persiste, antes esta
                                // linea regeneraba las 54 IR del gabinete para cada muestra
                                // contaminada: decenas de ms por muestra, con el hilo a
                                // THREAD_PRIORITY_TIME_CRITICAL y el resto del sistema ceder paso.
                                y = 0.0f;
                                huboNaN = true;
                            }
                            float pko = std::fabs(y);
                            if (pko > pkOut) pkOut = pko;
                            procBuf[outCount++] = y;
                        }
                        // Recuperacion del motor, una vez por bloque. false = no rehacer
                        // las IR del gabinete: solo se limpia el estado envenenado.
                        // Las muestras del bloque ya salen limpias (y = 0.0f arriba).
                        if (huboNaN) g_amp.prepare(fs, false);
                        g_ring.push(procBuf.data(), outCount);
                        // Visualizador: historial de la senal procesada (el hilo UI lo drena)
                        if (outCount > 0) {
                            g_scopeRing.push(procBuf.data(), outCount);
                            // hash del bloque: ultimo sample (con senal plana es
                            // 0 y no cambia; con tono puro oscila con la fase y
                            // mantiene vivo el scope)
                            g_scopeHash.store(procBuf[outCount - 1]);
                        }
                        // Grabadora WAV: copia estereo de la senal procesada
                        if (g_recActive.load() && outCount > 0) {
                            g_recScratch.resize(outCount * 2);
                            for (size_t i = 0; i < outCount; ++i)
                                g_recScratch[i*2] = g_recScratch[i*2 + 1] = procBuf[i];
                            g_recRing.push(g_recScratch.data(), outCount * 2);
                        }
                    }
                    capCap->ReleaseBuffer(frames);
                }
                capCap->GetNextPacketSize(&packet);
            }
            updateMeter(g_params.meterIn, pkIn);
            updateMeter(g_params.meterOut, pkOut);
        }

        // --- Reproduccion ---
        // CRITICO: escribir SOLO los frames disponibles (bufSize - padding).
        // Escribir bufSize completo en cada evento inunda el buffer del
        // dispositivo y provoca crujidos y cortes continuos.
        if (renCap && (w == WAIT_OBJECT_0 || !useBoth || w == WAIT_TIMEOUT)) {
            UINT32 bufSize = 0, padding = 0;
            hrRenPoll = renCli->GetBufferSize(&bufSize);
            if (SUCCEEDED(hrRenPoll)) hrRenPoll = renCli->GetCurrentPadding(&padding);
            if (SUCCEEDED(hrRenPoll) && bufSize > padding) {
                UINT32 renFrames = bufSize - padding;
                if (renFrames > (UINT32)outBuf.size()) renFrames = (UINT32)outBuf.size();
                BYTE* renData = nullptr;
                if (renFrames > 0 && SUCCEEDED(renCap->GetBuffer(renFrames, &renData))) {
                    size_t got = g_ring.pop(outBuf.data(), (size_t)renFrames);
                    int falta = 0;
                    if (got < (size_t)renFrames) {
                        falta = (int)((size_t)renFrames - got);
                        // Sin entrada configurada el anillo esta vacio a proposito
                        // (solo salida): eso no es un corte y no se cuenta.
                        if (wantInput) {
                            g_xrunUnderrun.fetch_add(1, std::memory_order_relaxed);
                            long long prev = g_xrunShortMax.load(std::memory_order_relaxed);
                            while ((long long)falta > prev && !g_xrunShortMax.compare_exchange_weak(
                                       prev, (long long)falta, std::memory_order_relaxed)) { }
                        }
                    }
                    // Relleno del hueco: se MANTIENE la ultima muestra y se
                    // desvanece en ~5 ms. Antes se ponia 0, y ese escalon es un
                    // chasquido (era una muestra de silencio en medio de la onda).
                    const float fadeK = std::exp(-1.0f/(0.005f*fs));
                    for (UINT32 i = 0; i < renFrames; ++i) {
                        float s;
                        if (i < got) {
                            s = outBuf[i];
                            g_bridgeLast = s;
                            g_bridgeFade = 1.0f;
                        } else {
                            s = bridgeNextGapSample(g_bridgeLast, g_bridgeFade, fadeK);
                        }
                        if (!std::isfinite(s)) s = 0.0f;
                        s = (s > 1.0f) ? 1.0f : ((s < -1.0f) ? -1.0f : s);
                        BYTE* ob = renData + (size_t)i*renAlign;
                        if (renFloat) {
                            float* o = (float*)ob;
                            for (int c = 0; c < renCh; ++c) o[c] = s;
                        } else {
                            for (int c = 0; c < renCh; ++c) {
                                BYTE* q = ob + (size_t)c*renBytes;
                                if (renBps == 32) {
                                    int v = (int)(s * 2147483647.0f);
                                    memcpy(q, &v, 4);
                                } else if (renBps == 24) {
                                    int v = (int)(s * 8388607.0f);
                                    q[0] = (BYTE)(v & 0xFF);
                                    q[1] = (BYTE)((v >> 8) & 0xFF);
                                    q[2] = (BYTE)((v >> 16) & 0xFF);
                                } else {
                                    short v = (short)(s * 32767.0f);
                                    memcpy(q, &v, 2);
                                }
                            }
                        }
                    }
                    renCap->ReleaseBuffer(renFrames, 0);
                }
            }
        }
        // ENDPOINT IDO (USB desenchufado, Bluetooth apagado, cambio de
        // dispositivo por defecto): WASAPI devuelve DEVICE_INVALIDATED y antes el
        // motor se quedaba mudo para siempre hasta pulsar APLICAR. A la decima
        // llamada seguida se cierra la sesion y el bucle exterior la reabre con
        // los dispositivos que haya ahora.
        if (hrCapPoll == AUDCLNT_E_DEVICE_INVALIDATED ||
            hrRenPoll == AUDCLNT_E_DEVICE_INVALIDATED) ++devDead;
        else devDead = 0;
        if (devDead >= 10) {
            g_deviceChanged.store(true);
            break;
        }
    }

    g_params.audioActive.store(false);
    g_sessRate.store(0);
    g_sessBufFrames.store(0);
    g_sessExclusive.store(0);
    audioRtLeave(mmcss);                 // se suelta ANTES de las esperas del cierre
    volSnapshot("parada del motor:");    // y al cerrarlo: si algo lo bajo, se ve el antes/despues
    if (capCli) capCli->Stop();
    if (renCli) renCli->Stop();
    if (capCap) capCap->Release();
    if (renCap) renCap->Release();
    if (capCli) capCli->Release();
    if (renCli) renCli->Release();
    if (capDev) capDev->Release();
    if (renDev) renDev->Release();
    enumerator->Release();
    if (capEvent) CloseHandle(capEvent);
    if (renEvent) CloseHandle(renEvent);
    }   // fin del ciclo de sesiones

    CoUninitialize();
    return 0;
}

// ============================================================================
//  Interfaz nativa estilo GUITAR ONE (GDI+)
// ============================================================================
static const int WIN_W = 1440, WIN_H = 956;   // tamanyo preferido (cuando cabe)

// La ventana es REDIMENSIONABLE. Todo el codigo de pintado e hit-test trabaja en
// COORDENADAS DE DISENO (1440x956): la escala la aplica una transformacion de
// GDI+ en el Graphics (formas, plumas, sombras y tipografia juntas) y la entrada
// se convierte con uiX/uiY. Asi no hay ni un literal pixel suelto que se olvide
// de escalar, y dibujo y raton no se pueden desalinear nunca.
// Antes la ventana era fija y en un portatil 1366x768 se salia de la pantalla:
// columnas, bridge y linea de estado quedaban inalcanzables, sin scroll ni resize.
static const int WIN_MIN_W = 1120, WIN_MIN_H = 760;
static float g_uiS  = 1.0f;    // escala diseno -> cliente
static float g_uiOX = 0.0f, g_uiOY = 0.0f;   // donde empieza el lienzo en el cliente
static void uiCompute(int W, int H) {
    float s = (float)W/(float)WIN_W;
    const float sy = (float)H/(float)WIN_H;
    if (sy < s) s = sy;
    if (s <= 0.0f) s = 1.0f;
    g_uiS  = s;
    g_uiOX = ((float)W - (float)WIN_W*s)*0.5f;
    g_uiOY = ((float)H - (float)WIN_H*s)*0.5f;
}
// Cliente -> diseno. Se usa en TODA la entrada del raton y en NINGUNA otra parte.
static inline int uiX(int x) { return (int)((x - g_uiOX)/g_uiS + 0.5f); }
static inline int uiY(int y) { return (int)((y - g_uiOY)/g_uiS + 0.5f); }
// Composicion p' = S*p + o (centro del lienzo de diseno en el cliente).
// Se fija la matriz explicita en vez de encadenar Scale/Translate: el orden
// Prepend/Append de GDI+ es facil de confundir y un desajuste de unos pocos
// pixeles entre pintado e hit-test romperia el raton sin que se notara.
static void uiApplyTransform(G::Graphics& g, int W, int H) {
    uiCompute(W, H);
    G::Matrix m(g_uiS, 0.0f, 0.0f, g_uiS, g_uiOX, g_uiOY);
    g.SetTransform(&m);
}
// Diseno -> cliente para los rectangulos sucios. InvalidateRect habla en pixeles de
// cliente, pero las tres zonas que se repintan por trozos durante un arrastre (la
// tarjeta de pedal, el bloque de mezcla y el mando) se piensan en diseno. Colar el
// rectangulo de diseno tal cual solo funcionaba a 1:1: al ampliar la ventana el
// trozo se quedaba corto y el arrastre dejaba estelas. El margen de 3px cubre el
// antialiasing del borde que queda fuera del rectangulo exacto.
static void uiInvalidate(HWND hwnd, const RECT& design) {
    RECT r;
    r.left   = (LONG)((float)design.left  *g_uiS + g_uiOX) - 3;
    r.top    = (LONG)((float)design.top   *g_uiS + g_uiOY) - 3;
    r.right  = (LONG)((float)design.right *g_uiS + g_uiOX) + 4;
    r.bottom = (LONG)((float)design.bottom*g_uiS + g_uiOY) + 4;
    InvalidateRect(hwnd, &r, FALSE);
}

// ============================================================
//  Layout GUITAR ONE: rejilla de 3 columnas (copia del diseno)
// ============================================================
// --- Rejilla principal (paneles) ---
static const int MARG = 24;
static const int PANEL_Y = 76,  PANEL_H = 754;      // 76..830 (toda la torre)
static const int COL_L_X = 24,  COL_L_W = 300;      // lista de amps
static const int COL_C_X = 340, COL_C_W = 720;      // torre de senal
static const int COL_R_X = 1076, COL_R_W = 340;     // panel CABINA

// Panel izquierdo: busqueda + lista. La banda de pestanas AMPLIFICADORES|PRESETS
// se retiro (paso 19): con el navegador superpuesto, la lista de presets de la
// izquierda era un SEGUNDO catalogo que contaba lo mismo, y el panel nacio para
// elegir AMPLIFICADOR (de el dependen el frontal y las perillas). Los presets
// viven solo en el navegador.
static const int TAB_H = 30;                 // alto del filete de la pestana CABINA
// 12px de aire entre buscador y lista: es el ritmo del resto del diseno. Con 8 el
// navegador se leia apretado de arriba.
static const int SEARCH_X = 34, SEARCH_Y = 86, SEARCH_W = 236, SEARCH_H = 32;
// La ESTRELLA del filtro "solo favoritos" de los amplis: al canto del buscador,
// en la misma fila. Antes vivia en la banda de pestanas y filtraba la pestana
// activa; ahora solo hay una lista que filtrar, asi que el mando se queda con
// ella (y el buscador cede el ancho justo para que no se pisen).
static const int SEARCH_STAR_X = SEARCH_X + SEARCH_W + 8, SEARCH_STAR_W = 36;
static const int LIST_Y = 130, LIST_H = PANEL_Y + PANEL_H - LIST_Y - 36;
static const int HINT_Y = PANEL_Y + PANEL_H - 24;   // leyenda de atajos al pie del navegador
// Fila de la lista: 56 y no 48. El aire es la mitad del rediseno, y con 56 la
// fila deja de ser un renglon apretado: caben 11 amplis sin scroll (11x56 = 616).
static const int LIST_ROW_H = 56;
// La caja de la ESTRELLA del filtro de la lista: UNA definicion para el pintado y
// para el clic, que es lo que impide que el mando y su zona vuelvan a separarse.
static RECT searchStarRect() {
    RECT r = { SEARCH_STAR_X, SEARCH_Y + 2,
               SEARCH_STAR_X + SEARCH_STAR_W, SEARCH_Y + SEARCH_H - 2 };
    return r;
}
// ============================================================================
//  RACK: la columna central es la torre de senal.
//  Antes y despues del ampli se leen ARRIBA y ABAJO de el, no en dos filas
//  sueltas bajo la rejilla: asi el orden del sonido se ve sin etiquetarlo y la
//  pantalla se comporta como un rack de verdad (unidad tras unidad, en
//  vertical). El carrusel coverflow de la version anterior se ha retirado: su
//  banda de clic invisible era el unico gesto que cambiaba de ampli y nadie lo
//  descubria; la lista de la izquierda ya hace ese trabajo.
// ============================================================================
static const int RACK_X = COL_C_X, RACK_W = COL_C_W;
static const int RACK_Y = 82;                       // primera unidad
static const int RACK_GAP = 12;                     // aire entre unidades
static const int UNIT_HDR_H = 26;                   // banda de titulo de la unidad

// Estante de pedales: fila de modulos + su banda de titulo
static const int PED_SLOT_W = 112, PED_SLOT_H = 108, PED_PITCH = 116;
static const int PED_MAX_ROW = 6;
static const int PED_SHELF_H = UNIT_HDR_H + 6 + PED_SLOT_H + 8;    // 148
static const int PED_TMPL_M  = 6;
static const int PED_TMPL_W = PED_SLOT_W + PED_TMPL_M*2;
static const int PED_TMPL_H = PED_SLOT_H + PED_TMPL_M*2;
// Piezas de la chapa de un modulo: UNA fuente para la plantilla, la capa viva y
// el clic. Cada sitio tenia sus literales y el switch vivo se pintaba 68px a la
// derecha de donde la plantilla habia pintado la pista: fuera de la chapa, con el
// clic a 40px de lo que se veia.
static const int PED_TRK_X = 30, PED_TRK_Y = 90, PED_TRK_W = 52, PED_TRK_H = 14;  // pista del bypass
static const int PED_KB1_X = 36, PED_KB2_X = 76, PED_KB_Y = 60, PED_KB_R = 12;    // centros de perilla
static const int PED_RM_DX = PED_SLOT_W - 16, PED_RM_DY = PED_SLOT_H - 12;        // boton quitar
static const int PED_X0 = RACK_X + 8;    // x de la plaza 0: dibujo, hit-test y drop
static G::REAL pedSlotX(int idx) { return (G::REAL)(PED_X0 + idx*PED_PITCH); }

// Unidad 1: estante ANTES del ampli
static const int PRE_SHELF_Y = RACK_Y;
static const int PRE_ROW_Y   = PRE_SHELF_Y + UNIT_HDR_H + 6;
// Unidad 2: el cabezal, con el frontal real a todo el ancho de la torre
static const int AMP_UNIT_Y  = PRE_SHELF_Y + PED_SHELF_H + RACK_GAP;
static const int ART_X = RACK_X + 6, ART_W = RACK_W - 12;
// Modal TONE3000: centrado en la ventana
static const G::REAL T3K_W = 900.0f, T3K_H = 560.0f;
static const G::REAL T3K_X = ((G::REAL)WIN_W - T3K_W)*0.5f;
static const G::REAL T3K_Y = ((G::REAL)WIN_H - T3K_H)*0.5f;

// --- Geometria del explorador ---------------------------------------------
//  Una sola fuente para pintar y para el raton: si el dibujo y el hit-test
//  calculasen las cajas por separado, volverian a desalinearse.
static const int T3K_PAD      = 24;
// Reserva de la linea de estado para el aviso legal de la derecha. Su tinta mide
// ~324px con la misma fuente que lo pinta (medido), y el aviso va alineado a la
// derecha, asi que crece hacia la izquierda: con la reserva vieja de 300 el estado
// podia entrar 24px dentro de el. 340 deja 12px de aire al recuento.
static const G::REAL T3K_DISC_RESERVE = 340.0f;
// 8 chips tienen que caber dentro del margen del panel (900 - 2*24): con 104
// el ultimo se comia el margen derecho y quedaba pegado al borde.
static const int T3K_CHIP_W   = 101, T3K_CHIP_H = 30, T3K_CHIP_GAP = 6;
static const int T3K_CARD_W   = 274, T3K_CARD_H = 168, T3K_CARD_GX = 12, T3K_CARD_GY = 12;
static const int T3K_CARDS_Y  = 138;      // relativo al panel
static const int T3K_SEARCH_Y = 56;
static const int T3K_CHIPS_Y  = 98;
static const int T3K_PAGER_Y  = 494;
static int t3kSearchX()    { return (int)T3K_X + T3K_PAD; }
static int t3kChipX(int i) { return (int)T3K_X + T3K_PAD + i*(T3K_CHIP_W + T3K_CHIP_GAP); }
static void t3kCardXY(int i, int& cx, int& cy) {
    cx = (int)T3K_X + T3K_PAD + (i % 3)*(T3K_CARD_W + T3K_CARD_GX);
    cy = (int)T3K_Y + T3K_CARDS_Y + (i / 3)*(T3K_CARD_H + T3K_CARD_GY);
}
// Cuantas tarjetas caben de verdad entre la rejilla de filtros y el paginador:
// pintar mas de las que caben las sacaba fuera del panel (una tercera fila se
// salia por abajo). El numero sale de la geometria, no de una constante suelta.
static int t3kCardMax() {
    const int rows = (T3K_PAGER_Y - T3K_CARDS_Y + T3K_CARD_GY)/(T3K_CARD_H + T3K_CARD_GY);
    return (rows < 1 ? 1 : rows)*3;
}
static int t3kCardCount() { const int n = (int)g_t3kItems.size(), m = t3kCardMax(); return n > m ? m : n; }
// Cada pagina trae justo las tarjetas que se ven: con 24 por pagina solo se
// alcanzaban 6 y las otras 18 no habia forma de verlas.
static int t3kPageSize()  { return t3kCardMax(); }   // se pinta lo mismo que se pide
// Caja del boton "solo verificados" (fila de paginacion, tras las flechas)
static int t3kVerifX() { return (int)T3K_X + T3K_PAD + 250; }
// Â¿El flujo de dispositivo ya tiene codigo para confirmar?
static bool t3kDevWaiting() {
    std::lock_guard<std::mutex> lk(g_t3kMutex);
    return !g_t3kUserCode.empty();
}
static int t3kPageCount() {
    const int n = (int)((g_t3kTotal + t3kPageSize() - 1) / t3kPageSize());
    return n < 1 ? 1 : n;
}
// Pestanyas de lista (tendencias/nuevos/favoritos/descargados), a la derecha de
// la fila del paginador: es el mismo ancho de pastilla que el resto del sistema.
static const int T3K_LIST_W = 96, T3K_LIST_H = 28, T3K_LIST_GAP = 6;
static int t3kListX(int i) {
    return (int)T3K_X + (int)T3K_W - T3K_PAD - 4*T3K_LIST_W - 3*T3K_LIST_GAP + i*(T3K_LIST_W + T3K_LIST_GAP);
}
// Hoja de conexion (encima del panel)
static const int T3K_SHEET_W = 760, T3K_SHEET_H = 240;
static int t3kSheetX() { return (int)T3K_X + ((int)T3K_W - T3K_SHEET_W)/2; }
static int t3kSheetY() { return (int)T3K_Y + ((int)T3K_H - T3K_SHEET_H)/2; }
// La fila de botones es la MISMA en los dos estados de la hoja (campo de clave o
// codigo de confirmacion): antes el boton saltaba 10px y el de cancelar 12 al
// aparecer el codigo. Las x salen de aqui, no de numeros sueltos por el codigo.
static const G::REAL T3K_BTN_Y = 186.0f, T3K_BTN_H = 36.0f;
static const G::REAL T3K_BTN_GO_W = 300.0f, T3K_BTN_CANCEL_W = 140.0f, T3K_BTN_GAP = 16.0f;
static G::REAL t3kBtnGoX() {
    const G::REAL total = T3K_BTN_GO_W + T3K_BTN_GAP + T3K_BTN_CANCEL_W;
    return ((G::REAL)T3K_SHEET_W - total)*0.5f;      // centrados en la hoja
}
static G::REAL t3kBtnCancelX() { return t3kBtnGoX() + T3K_BTN_GO_W + T3K_BTN_GAP; }

// Identificadores de zona: los comparten el hover y el clic
enum {
    T3K_HIT_NONE = 0, T3K_HIT_SEARCH = 1, T3K_HIT_GO = 2, T3K_HIT_VER = 3, T3K_HIT_CLOSE = 4,
    T3K_HIT_CONN = 5, T3K_HIT_VERIF = 6, T3K_HIT_LIB = 7, T3K_HIT_CHIP0 = 10, T3K_HIT_CARD0 = 20, T3K_HIT_PREV = 30, T3K_HIT_NEXT = 31,
    T3K_HIT_PAGE0 = 32, T3K_HIT_KEYFIELD = 40, T3K_HIT_KEYGO = 41, T3K_HIT_KEYCANCEL = 42,
    T3K_HIT_KEYOPEN = 43, T3K_HIT_LIST0 = 50
};
static bool mouseIn(int x0, int y0, int x1, int y1);   // definida mas abajo (capa UI)
static int t3kHit(int x, int y) {
    if (!g_t3kOpen) return -1;
    const int px = (int)T3K_X, py = (int)T3K_Y, pw = (int)T3K_W, ph = (int)T3K_H;
    if (x < px || x >= px + pw || y < py || y >= py + ph) return -1;
    if (g_t3kConnOpen) {
        const int sx = t3kSheetX(), sy = t3kSheetY();
        if (mouseIn(sx, sy, sx + T3K_SHEET_W, sy + T3K_SHEET_H)) {
            if (t3kDevWaiting()) {   // ya hay codigo: abrir el navegador o cerrar
                if (mouseIn(sx + (int)t3kBtnGoX(), sy + (int)T3K_BTN_Y,
                            sx + (int)(t3kBtnGoX() + T3K_BTN_GO_W), sy + (int)(T3K_BTN_Y + T3K_BTN_H)))
                    return T3K_HIT_KEYOPEN;
                if (mouseIn(sx + (int)t3kBtnCancelX(), sy + (int)T3K_BTN_Y,
                            sx + (int)(t3kBtnCancelX() + T3K_BTN_CANCEL_W), sy + (int)(T3K_BTN_Y + T3K_BTN_H)))
                    return T3K_HIT_KEYCANCEL;
            } else {                 // sin codigo: campo de la clave + CONECTAR
                if (mouseIn(sx + 24, sy + 78, sx + T3K_SHEET_W - 24, sy + 112)) return T3K_HIT_KEYFIELD;
                if (mouseIn(sx + (int)t3kBtnGoX(), sy + (int)T3K_BTN_Y,
                            sx + (int)(t3kBtnGoX() + T3K_BTN_GO_W), sy + (int)(T3K_BTN_Y + T3K_BTN_H)))
                    return T3K_HIT_KEYGO;
                if (mouseIn(sx + (int)t3kBtnCancelX(), sy + (int)T3K_BTN_Y,
                            sx + (int)(t3kBtnCancelX() + T3K_BTN_CANCEL_W), sy + (int)(T3K_BTN_Y + T3K_BTN_H)))
                    return T3K_HIT_KEYCANCEL;
            }
            return T3K_HIT_NONE;
        }
        return -1;
    }
    if (mouseIn(px + pw - 46, py + 13, px + pw - 14, py + 45)) return T3K_HIT_CLOSE;
    if (mouseIn(t3kSearchX(), py + T3K_SEARCH_Y, t3kSearchX() + 372, py + T3K_SEARCH_Y + 34))
        return T3K_HIT_SEARCH;
    if (mouseIn(t3kSearchX() + 380, py + T3K_SEARCH_Y, t3kSearchX() + 476, py + T3K_SEARCH_Y + 34))
        return T3K_HIT_GO;
    // Hueco libre entre BUSCAR y CONECTAR (500..598 del panel)
    if (mouseIn(t3kSearchX() + 480, py + T3K_SEARCH_Y, t3kSearchX() + 570, py + T3K_SEARCH_Y + 34))
        return T3K_HIT_LIB;
    if (mouseIn(px + pw - T3K_PAD - 150, py + T3K_SEARCH_Y, px + pw - T3K_PAD, py + T3K_SEARCH_Y + 34))
        return T3K_HIT_VER;
    if (mouseIn(px + pw - T3K_PAD - 150 - 8 - 120, py + T3K_SEARCH_Y,
                px + pw - T3K_PAD - 158, py + T3K_SEARCH_Y + 34)) return T3K_HIT_CONN;
    for (int i = 0; i < 8; ++i)
        if (mouseIn(t3kChipX(i), py + T3K_CHIPS_Y, t3kChipX(i) + T3K_CHIP_W, py + T3K_CHIPS_Y + T3K_CHIP_H))
            return T3K_HIT_CHIP0 + i;
    for (int i = 0; i < t3kCardCount(); ++i) {
        int cx, cy; t3kCardXY(i, cx, cy);
        if (mouseIn(cx, cy, cx + T3K_CARD_W, cy + T3K_CARD_H)) return T3K_HIT_CARD0 + i;
    }
    const int pgX = px + T3K_PAD, pgY = py + T3K_PAGER_Y;
    if (mouseIn(pgX, pgY, pgX + 28, pgY + 28)) return T3K_HIT_PREV;
    const int np = t3kPageCount() < 6 ? t3kPageCount() : 6;
    for (int k = 0; k < np; ++k)
        if (mouseIn(pgX + 34 + k*30, pgY, pgX + 34 + k*30 + 26, pgY + 28)) return T3K_HIT_PAGE0 + k;
    if (mouseIn(pgX + 34 + np*30 + 4, pgY, pgX + 34 + np*30 + 32, pgY + 28)) return T3K_HIT_NEXT;
    if (mouseIn(t3kVerifX(), pgY, t3kVerifX() + 168, pgY + 28)) return T3K_HIT_VERIF;
    for (int i = 0; i < 4; ++i)
        if (mouseIn(t3kListX(i), pgY, t3kListX(i) + T3K_LIST_W, pgY + T3K_LIST_H))
            return T3K_HIT_LIST0 + i;
    return T3K_HIT_NONE;
}
static const int ART_H = ART_W*540/1200;            // 324 con ART_W = 720
static const int ART_Y = AMP_UNIT_Y + UNIT_HDR_H + 8;
// El cabezal mide 372 (26 de banda + 8 + 324 de frontal + 14 de canto). Es la
// medida del diseno: con ella la torre termina en 828 y el pie empieza en 846.
static const int AMP_UNIT_H = UNIT_HDR_H + 8 + ART_H + 14;
// ...pero el frontal del PERFIL no es un ampli de verdad: su arte (1200x540) es
// medio panel de mandos y media rejilla vacia con la placa del nombre flotando.
// Se dibuja solo el PANEL (arte y 0..300) a la misma escala y el resto de la
// unidad se dedica a la cadena de capturas. Antes esa fila se montaba ENCIMA
// del frontal (NAMROW_Y = 534 caia dentro del arte 276..594, sobre su rejilla):
// se veian cajas pegadas al ampli. Sacarla por debajo sin recortar la cara
// engordaba la torre 78px (lo avisa el comentario de AMP_UNIT_H): recortando,
// no hace falta mover nada de la torre.
static const int NAM_FACE_ART_H = 300;                        // alto util en px de arte
static const int NAM_FACE_H     = ART_H*NAM_FACE_ART_H/540;   // 177 en pantalla
// Unidad 3: estante DESPUES del ampli
static const int POST_SHELF_Y = AMP_UNIT_Y + AMP_UNIT_H + RACK_GAP;
// Banda de la cadena de capturas: bahia propia (mismo ancho que la torre, como
// el estante de pedales) entre la cara recortada y el canto de la unidad.
static const int NAM_BAY_Y  = ART_Y + NAM_FACE_H + 6;                        // 459
static const int NAM_BAY_H  = (AMP_UNIT_Y + AMP_UNIT_H - 14) - NAM_BAY_Y;    // 135
static const int NAMROW_H   = 100;   // altura de la celda: estado, nombre y botones
static const int NAMROW_Y   = NAM_BAY_Y + (NAM_BAY_H - NAMROW_H)/2;          // 498
static const int NAM_ADD_X  = RACK_X + 8;                     // 348, como las plazas
static const int NAM_ADD_W  = 130;
static const int NAM_BLK_W = 140, NAM_BLK_GAP = 6;   // 3 celdas caben en la fila (mas aire por celda)
// Fila de capturas del PERFIL: + ANADIR | T3K | bloques. La geometria vive aqui
// y la leen el dibujo y el raton: separarla fue el fallo (el chip T3K caia
// justo encima del primer bloque y desaparecia debajo de el).
static const int NAM_T3K_W = 96;
static const int NAM_T3K_X = NAM_ADD_X + NAM_ADD_W + 10;
// Boton impreso CARGAR PERFIL de la pantalla del cabezal PERFIL: su caja vive en
// coordenadas de ARTE (1200x540) y la comparten pintura, hover y clic. Antes el
// hover leia g_lastPt (que nadie escribe, siempre -1,-1: nunca encendia) y el
// clic no lo miraba en absoluto: un rÃ³tulo que prometia y no hacia nada.
static const int NAM_BTN_AX = 170, NAM_BTN_AY = 212, NAM_BTN_AW = 240, NAM_BTN_AH = 32;
static void namBtnBox(G::REAL& x, G::REAL& y, G::REAL& w, G::REAL& h) {
    x = (G::REAL)ART_X + NAM_BTN_AX*ART_W/1200.0f;
    y = (G::REAL)ART_Y + NAM_BTN_AY*ART_H/540.0f;
    w = NAM_BTN_AW*ART_W/1200.0f;
    h = NAM_BTN_AH*ART_H/540.0f;
}
// Dialogo .nam y alta en la cadena: lo comparten el boton ANADIR de la fila y el
// boton impreso CARGAR PERFIL de la pantalla. Dos rotulos, UNA accion.
static void namPickAndAdd(HWND hwnd) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = g_lang ? L"Neural captures (*.nam)\0*.nam\0All files\0*.*\0"
                                : L"Capturas neurales (*.nam)\0*.nam\0Todos los archivos\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = tr(L"Anadir captura (.nam) a la cadena del PERFIL");
    ofn.lpstrInitialDir = g_t3kProfilesDir;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        std::wstring msg;
        if (namChainAdd(file, msg) >= 0) {
            // El bloque entra en la cadena del cabezal PERFIL, pero si el que
            // suena es otro ampli, esa captura NO se oye: se anadia y "no
            // cargaba". El camino de TONE3000 ya cambiaba de cabezal; este no.
            if (g_params.model.load() != 12) {
                g_params.model.store(12);
                rebuildKnobs();
            }
            toast(L"CAPTURA A\u00D1ADIDA A LA CADENA");
        }
        else                             toast(msg.c_str());
        g_sceneDirty.store(true);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}
static const int NAM_BLK_X0 = NAM_T3K_X + NAM_T3K_W + 8;
static int namBlkX(int i) { return NAM_BLK_X0 + i*(NAM_BLK_W + NAM_BLK_GAP); }
static int namBlkVis() {
    const int room = ART_X + ART_W - 12 - NAM_BLK_X0;
    const int n = (room + NAM_BLK_GAP)/(NAM_BLK_W + NAM_BLK_GAP);
    return n < 1 ? 1 : (n > 4 ? 4 : n);
}
static const int POST_ROW_Y   = POST_SHELF_Y + UNIT_HDR_H + 6;
static const int PED_ROW_Y[2] = { PRE_ROW_Y, POST_ROW_Y };
// Banda de la cadena de senal: resumen del recorrido, al pie de la torre
static const int CHAIN_Y = POST_SHELF_Y + PED_SHELF_H + 8;
static const int CHAIN_H = 46;
// El boton AFINADOR vive en la banda de la cadena, al extremo derecho
static const int TUNER_BTN_W = 108, TUNER_BTN_H = 30;
// 25px al canto de la unidad: con 14 el boton pisaba su tornillo derecho
static const int TUNER_BTN_X = RACK_X + RACK_W - TUNER_BTN_W - 33;
static const int TUNER_BTN_Y = CHAIN_Y + (CHAIN_H - TUNER_BTN_H)/2;

// Radio del mando pequeno para los parametros que el frontal NO imprime. Vive
// EN LA CHAPA, en el hueco que dejan los mandos impresos (hoy solo AMBER TWEED,
// cuyo diseno serigrafia tres mandos para seis parametros de motor); los offsets
// de posicion que estaban aqui (AMP_EXTRA_Y/X0/DX) pertenecian a la banda de
// titulo de una version anterior y nadie los usaba.
static const int AMP_EXTRA_R  = 22;   // ~80px de arte: cabe entre los impresos

// El cabezal ocupa el ancho de la torre: su frontal real (1200x540) manda.
static G::REAL artKnobR() { return 35.0f*(G::REAL)ART_W/744.0f; }

// Panel derecho: CABINA (preview + IR + micro + distancia + mezcla + scope).
// UN solo ancho de contenido (el panel menos 8px por lado) para todo lo que va
// dentro: el previo, la rejilla de pastillas, la mezcla y el visualizador. La
// pista de la mezcla vivia en su propio margen (18) y el visualizador en otro
// (40): la mitad de abajo del panel leia como tres piezas sueltas.
static const int CAB_TAB_Y = 86;
// Sin linea de estado encima del previo: el estado vive en el selector CAB de
// abajo ("â€¹ CAB 2x12 OPEN â€º"), y repetir el mismo valor 24px mas arriba era el
// unico texto sobrante de la columna. El mueble empieza bajo la pestana.
// El previo crece con los 22px que suelta esa linea (292 -> 308) y el objeto
// gana el aire que la columna le estaba negando.
static const int CAB_PV_X = COL_R_X + 8, CAB_PV_Y = 132, CAB_PV_W = COL_R_W - 16, CAB_PV_H = 308;
static const int CAB_BTN_X = COL_R_X + 8,   CAB_BTN_Y = 470, CAB_BTN_W = 150, CAB_BTN_H = 30;
static const int CAB_SEL_X = COL_R_X + 166, CAB_SEL_Y = 470, CAB_SEL_W = 166, CAB_SEL_H = 30;
static const int CAB_MIC_X = COL_R_X + 8,   CAB_MIC_Y = 512, CAB_MIC_W = 150, CAB_MIC_H = 30;
static const int CAB_DST_X = COL_R_X + 166, CAB_DST_Y = 512, CAB_DST_W = 166, CAB_DST_H = 30;
static const int CAB_SEP_Y = 566;   // filete que cierra el bloque de cabina

// --- MEZCLA: un bloque de TRES lineas, con su propio aire -------------------
//   rotulo + valor  /  la pista con su mango  /  los dos extremos
// El mango viaja SIEMPRE dentro de la pista: el recorrido util se mete media
// manija por cada extremo. Antes su centro se calculaba sobre el ancho entero,
// asi que al 100% (el valor de fabrica) medio mango quedaba fuera del carril.
static const int MIX_SL_X = COL_R_X + 8, MIX_SL_W = COL_R_W - 16;   // ancho de contenido
static const int MIX_SL_H = 12;                                     // la pista
static const int MIX_LBL_Y = 580, MIX_LBL_H = 16;                   // rotulo + valor
static const int MIX_SL_Y  = 602;                                   // la pista
static const int MIX_LEG_Y = 620;                                   // los dos extremos
static const int MIX_HND_W = 7, MIX_HND_H = 20;                     // el mango
static const int MIX_INSET = 6;    // media manija + 2px de aire por extremo
static const int MIX_SL_PAD = 10;  // margen de captura del arrastre y de la rueda
static const int MIX_BLK_Y = 576, MIX_BLK_B = 634;   // la caja del bloque, entera

// El recorrido de la mezcla: UNA formula para pintar, arrastrar y la rueda.
// (estaba escrita a mano en tres sitios, con dos geometrias distintas)
static G::REAL mixCapX(G::REAL v01) {
    if (v01 < 0.0f) v01 = 0.0f;
    if (v01 > 1.0f) v01 = 1.0f;
    return (G::REAL)MIX_SL_X + (G::REAL)MIX_INSET +
           v01*(G::REAL)(MIX_SL_W - 2*MIX_INSET);
}
static float mixSliderFrac(int mx) {
    G::REAL v = ((G::REAL)mx - ((G::REAL)MIX_SL_X + (G::REAL)MIX_INSET))/
                (G::REAL)(MIX_SL_W - 2*MIX_INSET);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (float)v;
}

// Visualizador SALIDA: proporcion de PANTALLA (3:2) centrada en el ancho de
// contenido. Era 324x154 (2,1:1): una cinta baja de canto a canto del panel que
// en reposo leia como un rectangulo vacio, y con 65px de amplitud el trazo
// quedaba aplastado. El resto de bloques del panel conserva el ancho unico (el
// previo, la rejilla de pastillas y la mezcla) -eso lo unifico el paso 12-; lo
// que cambia es la FORMA de la ventana de la onda, no la alineacion de la
// columna, y el rotulo ya iba centrado.
static const int SCOPE_W = 240, SCOPE_H = 160;
static const int SCOPE_X = COL_R_X + (COL_R_W - SCOPE_W)/2;
static const int SCOPE_Y = 664;

// --- Estado de la lista (panel izquierdo) ---
static int   g_listScroll = 0;
static bool  g_searchFocus = false;
static WCHAR g_ampFilter[40] = L"";
static bool  g_mixDrag = false;

// --- Footer del diseno + estado ---
static const int FOOT_Y = 846, FOOT_H = 70;   // debajo de la banda de cadena (846..916)
// El pie es UNA tira de canales: los rotulos en una sola linea arriba
// (BOT_LBL_Y) y todos los controles centrados en la misma linea de abajo
// (FOOT_Y+44). Antes cada rotulo traia su altura y su tamano y el pie leia como
// un cajon de cosas sueltas.
//  El ritmo de la tira era 2 / 14 (rotulos) / 4 / 44 (controles) / 4: la linea
//  de rotulos quedaba a DOS pixeles del canto del panel y la tira leia apoyada
//  en el suelo. Ahora es 6 / 14 / 4 / 40 / 6 con el CENTRO de los controles
//  donde estaba (FOOT_Y+44 = 890): la caja y los mandos bajan 4px de alto y
//  ganan el aire, sin mover ni un centro.
static const int BOT_LBL_Y  = FOOT_Y + 6;     // 852: linea comun de rotulos
// El rotulo del pie, en UN solo sitio: familia, tamano y alto de la caja. El
// pintado y la prueba T11 leen de aqui, asi que no pueden discrepar. La caja
// tiene que dar cabida a la LINEA entera de la fuente (ascendente + descendente
// + interlineado), no solo a las mayusculas: con una caja de 14 px y una fuente
// de linea mas alta, el pintado recorta por abajo la ultima fila de las letras.
static const WCHAR*  kFootLblFam = kFontTec;     // Bahnschrift SemiCondensed
static const G::REAL kFootLblPt  = kTRotulo;     // 9,5 (papel de rotulo: mayusculas)
static const int     BOT_LBL_H   = 20;
static const int BOT_BOX_Y  = FOOT_Y + 24;    // 870: caja de preset (40px)
static const int BOT_MTR_Y  = FOOT_Y + 31;    // 877: pista en 888..893 (centro 890)
static const int BOT_STAT_Y = WIN_H - 26;
// Pie: medidores + LED de clip (posiciones compartidas por estatica y dinamica)
static const int MTR_IN_X   = 560,  MTR_IN_W  = 120;
static const int MTR_OUT_X  = 780,  MTR_OUT_W = 120;
static const int MTR_GT_X   = 1000, MTR_GT_W  = 70;
static const int CLIP_LED_X = 1132;
static const int MASTER_X = WIN_W - 130, MASTER_Y = FOOT_Y + 44, MASTER_R = 20;
// Perilla de trim del canal, justo a la izquierda del medidor INPUT
static const int IN_TRIM_X = 476, IN_TRIM_Y = FOOT_Y + 44, IN_TRIM_R = 16;

// --- Cabecera: caja de preset + botones (derecha) ---
static const int TOP_AUDIO_X = WIN_W - 176;
static const int TOP_GEAR_X  = WIN_W - MARG - 40;   // mismo margen (24) que el resto del layout
static const int TOP_BOX_X   = WIN_W/2 - 200;
// Caja del preset: el control mas grande de la cabecera, y el que dice QUE
// suena. Alto, ancho y posicion viven aqui porque dibujo y zona de raton
// tienen que moverse juntos.
static const int TOP_BOX_Y   = 12;
static const int TOP_BOX_W   = 400;
static const int TOP_BOX_H   = 52;
// Separador vertical dentro de la caja de preset: reposo y hover usan EL MISMO
// valor (356 = 2px antes de la flecha, que empieza en 358). Antes reposo=322 y
// hover=356: el divisor saltaba 34px con el raton.
static const int TOP_DIV_X   = 356;
static const int TOP_ARROW_X = 358;
static const int TOP_SAVE_X  = WIN_W - 402;
static const int TOP_REC_X   = WIN_W - 286;
// El boton del NAVEGADOR de presets (paso 18): pegado a la caja del preset, que
// es de quien abre la ventana. Deja el mismo aire (10px) que hay antes de GUARDAR.
static const int TOP_BRW_X   = TOP_BOX_X + TOP_BOX_W + 8;
// Filete de 1px que separa la accion que cambia el documento (GUARDAR) de los
// dos conmutadores de estado (GRABAR, AUDIO): dos grupos, no tres botones
// sueltos. Vive entre los dos, con el mismo aire a cada lado.
static const int TOP_SEP_X   = WIN_W - 294;

// --- Palette de pedales + modal afinador ---
static const int PAL_X = (WIN_W - 660)/2, PAL_Y = 158, PAL_W = 660, PAL_H = 640;
static const int PAL_TX0 = 16, PAL_TY0 = 60;
static const int PAL_TILE_W = 200, PAL_TILE_H = 72;
static const int PAL_TILE_DX = 214, PAL_TILE_DY = 84;
static const int TUN_X = 470, TUN_Y = 240, TUN_W = 500, TUN_H = 440;
static const int TUN_CLOSE_W = 140;
static const int TUN_CLOSE_X = TUN_X + TUN_W - 164, TUN_CLOSE_Y = TUN_Y + TUN_H - 60;

// --- Modal RENOMBRAR preset (nombre propio del usuario) ---
static const int REN_W = 560, REN_H = 200;
static const int REN_X = (WIN_W - REN_W)/2, REN_Y = (WIN_H - REN_H)/2;
static const int REN_F_X = REN_X + 28, REN_F_Y = REN_Y + 78, REN_F_W = REN_W - 56, REN_F_H = 44;
static const int REN_OK_W = 132, REN_OK_H = 36;
static const int REN_OK_X = REN_X + REN_W - 28 - REN_OK_W, REN_OK_Y = REN_Y + REN_H - 58;
static const int REN_NO_W = 128;
static const int REN_NO_X = REN_OK_X - 12 - REN_NO_W, REN_NO_Y = REN_OK_Y, REN_NO_H = REN_OK_H;

// Colores del diseno (12 amps: familias clean / crunch / alta distorsion)
// Acento de amp: MONOCROMO. En el lenguaje nuevo el chrome no tiene color: la
// identidad de cada amplificador la da su arte (frontal real), no un tono de
// interfaz. El unico color vivo de la app (kAcc) queda para lo interactivo.
static G::Color colAmp(int i) {
    (void)i;
    return G::Color(255, 0xf1, 0xf1, 0xf4);
}
// Color por tipo de pedal (18 tipos)
// Color por tipo de pedal. En FILAMENT un pedal no tiene color propio: la
// unica distincion es funcional. Los pedales de ganancia llevan el ambar
// (son los que empujan el amp); el resto, un champan neutro. Asi el ojo lee
// "esto satura" de un vistazo sin que el pedalboard parezca una feria.
static G::Color colFx(int t) {
    switch (t) {
    case 2:  case 10: case 11: case 12: case 16:
        return kAcc;        // ganancia: los unicos que empujan el ampli
    default:
        return kFTxt2;      // el resto es chrome neutro, no un segundo acento
    }
}

// Nombres y simbolos de FX (18 tipos)
static const char*   kFxNames[18]  = { "NOISE GATE", "COMPRESSOR", "DRIVE",
                                       "MODULATION", "DELAY", "REVERB",
                                       "TREMOLO", "PHASER", "FLANGER", "EQ",
                                       "OVERDRIVE", "FUZZ", "DISTORTION",
                                       "CHORUS", "TAPE ECHO", "SPRING",
                                       "BOOST", "AUTO-WAH" };
static const WCHAR*  kFxIcons[18]  = { L"\x25CC", L"\x2571", L"\x25CF",
                                       L"\x223F", L"\x25C9", L"\x25C7",
                                       L"\x2248", L"\x25D0", L"\x21BB", L"\x2261",
                                       L"\x2240", L"\x2731", L"\x25B2",
                                       L"\x224A", L"\x25CE", L"\x224B",
                                       L"\x2191", L"\x25A1" };
static const char*   kFxParams[18][2] = { { "THRESHOLD", "DECAY" }, { "SUSTAIN", "LEVEL" },
                                          { "DRIVE", "TONE" }, { "RATE", "DEPTH" },
                                          { "TIME", "FEEDBACK" }, { "SIZE", "MIX" },
                                          { "RATE", "DEPTH" }, { "RATE", "DEPTH" },
                                          { "RATE", "DEPTH" }, { "LOW", "HIGH" },
                                          { "DRIVE", "TONE" }, { "FUZZ", "TONE" },
                                          { "DIST", "FILTER" }, { "RATE", "DEPTH" },
                                          { "TIME", "FEEDBACK" }, { "DWELL", "MIX" },
                                          { "LEVEL", "TONE" }, { "SENS", "RANGE" } };
// Etiquetas cortas para los knobs mini de cada slot del pedalboard
static const char*   kFxShort[18][2] = { { "THR", "DEC" }, { "SUS", "LVL" }, { "DRV", "TONE" },
                                        { "RATE", "DEEP" }, { "TIME", "FB" }, { "SIZE", "MIX" },
                                        { "RATE", "DEEP" }, { "RATE", "DEEP" },
                                        { "RATE", "DEEP" }, { "LOW", "HIGH" },
                                        { "DRV", "TONE" }, { "FUZZ", "TONE" },
                                        { "DIST", "FLT" }, { "RATE", "DEEP" },
                                        { "TIME", "FB" }, { "DWELL", "MIX" },
                                        { "LVL", "TONE" }, { "SENS", "RNG" } };
// Las ranuras de la lista del navegador: una por entrada de la lista. Desde el
// paso 19 la lista es solo de amplificadores y es PLANA, asi que sobra el tope de
// encabezados; se conserva por si volviera una lista con filas intercaladas, y
// porque las ranuras miden todas lo mismo: la fila, el clic y el hover se miden
// con navRowRect y lo unico que cambia es QUE hay en cada ranura.
static const int kMaxNavRows = kMaxListEntries + kAmpCount;
static const WCHAR* kAmpNames[13]  = { L"SPARKLE TWIN", L"CHIME 30", L"AMBER TWEED",
                                        L"CHORUS 120", L"MARAUDER 800", L"LEAD 1959",
                                        L"TANGERINE 120", L"LONDON BLUES", L"SESSION HOT",
                                        L"DUO RECTO", L"KRAFT V4", L"HAVOC 100",
                                        L"PERFIL" };
static const WCHAR*  kAmpSubs[13]   = { L"TWIN \xB7 HEADROOM", L"CLASS A \xB7 CHIME", L"5E3 \xB7 EDGE",
                                        L"SOLID \xB7 PRISTINE", L"BRIT \xB7 MID DRIVE", L"PLEXI \xB7 HARMONIC",
                                        L"THICK \xB7 WOOLLY", L"BOUTIQUE \xB7 TOUCH", L"SMOOTH \xB7 VOCAL",
                                        L"RECTO \xB7 V-SHAPE", L"MODERN \xB7 TIGHT", L"LEAD \xB7 SATURATED",
                                        L"CAPTURA \xB7 NEURONAL" };
static const char*   kAmpKnobNames[6] = { "GAIN", "BASS", "MIDDLE", "TREBLE", "VOLUME", "PRESENCE" };

// Etiquetas IMPRESAS en cada frontal (las del arte, no las nuestras): el
// tooltip de una perilla tiene que decir lo mismo que la chapa que hay debajo.
// Leidas de native/amps_frontales/fondos/*_posiciones.json.
static const WCHAR* kAmpHoleLabels[13][10] = {
    { L"VOLUME", L"TREBLE", L"MIDDLE", L"BASS", L"REVERB", L"SPEED", L"INTENSITY" },
    { L"VOLUME", L"TREBLE", L"BASS", L"CUT", L"TONE", L"MASTER" },
    { L"MIC VOLUME", L"INST VOLUME", L"TONE" },
    { L"VOLUME", L"BASS", L"MIDDLE", L"TREBLE", L"DISTORTION", L"REVERB", L"RATE", L"DEPTH" },
    { L"PRESENCE", L"BASS", L"MIDDLE", L"TREBLE", L"MASTER", L"GAIN" },
    { L"PRESENCE", L"BASS", L"MIDDLE", L"TREBLE", L"VOLUME I", L"VOLUME II" },
    { L"GAIN", L"VOLUME", L"TREBLE", L"MIDDLE", L"BASS", L"MASTER" },
    { L"VOLUME", L"TREBLE", L"MIDDLE", L"BASS", L"REVERB", L"TREMOLO", L"COMP" },
    { L"VOLUME", L"BASS", L"MIDDLE", L"TREBLE", L"RATIO", L"MASTER" },
    { L"GAIN", L"BASS", L"MIDDLE", L"TREBLE", L"PRESENCE", L"MASTER", L"OUTPUT" },
    { L"GAIN", L"BASS", L"MIDDLE", L"TREBLE", L"PRESENCE", L"DEEP", L"VOLUME", L"MASTER" },
    { L"PRE GAIN", L"POST GAIN", L"LOW", L"MID", L"HIGH", L"PRESENCE", L"RESONANCE" },
    { L"GAIN", L"BASS", L"MIDDLE", L"TREBLE", L"VOLUME" },
};
// Parametros del amp que su frontal NO imprime en ningun hueco (AMBER TWEED
// solo serigrafia tres mandos): se montan como mandos pequenos en la banda de
// titulo del cabezal, con su nombre, para que ningun parametro quede sin
// control. Devuelve la lista y cuantos son.
// Que HACE el control extra de un ampli. No se inventa: lo decide el nombre que
// su chapa imprime en el hueco que el motor no usa. Lo que no se puede honrar de
// verdad (INTENSITY, RATE, DEPTH, COMP...) se queda aparcado y la UI lo dice.
//   0 = sin modelar (mando inerte)   1 = resonancia de graves   2 = nivel de salida
//   0 sin modelar | 1 resonancia de graves | 2 nivel de salida | 3 tremolo (profundidad)
//   4 tremolo (velocidad) | 5 chorus (velocidad) | 6 chorus (profundidad) | 7 compresion
static int ampExtraKindForLabel(const WCHAR* lbl) {
    if (!lbl || !lbl[0]) return 0;
    if (wcsstr(lbl, L"RESONANCE") || wcsstr(lbl, L"DEEP")) return 1;
    if (wcsstr(lbl, L"OUTPUT") || wcsstr(lbl, L"VOLUME") || wcsstr(lbl, L"MASTER")) return 2;
    if (wcsstr(lbl, L"INTENSITY")) return 3;
    if (wcsstr(lbl, L"SPEED")) return 4;
    if (wcsstr(lbl, L"RATE")) return 5;
    if (wcsstr(lbl, L"DEPTH")) return 6;
    if (wcsstr(lbl, L"COMP")) return 7;
    return 0;
}

// Huecos impresos (0..9) que llevan los controles extra del ampli, en orden.
// Devuelve cuantos son (0..2) y los indices en out.
static int ampExtraHoles(int m, int* out) {
    int n = 0;
    for (int i = 0; i < kAmpArt[m].nall && i < 10 && n < 2; ++i) {
        bool used = false;
        for (int k = 0; k < 6; ++k) if (kAmpArt[m].map[k] == i) used = true;
        if (used) continue;
        if (ampExtraKindForLabel(kAmpHoleLabels[m][i]) != 0) out[n++] = i;
    }
    return n;
}
// Que funcion tiene el control extra numero idx (0 o 1) del ampli m.
static int ampExtraKindAt(int m, int idx) {
    int h[2];
    const int n = ampExtraHoles(m, h);
    if (idx >= n) return 0;
    return ampExtraKindForLabel(kAmpHoleLabels[m][h[idx]]);
}
static int ampExtraKind(int m) { return ampExtraKindAt(m, 0); }

// Neutro de cada funcion: el valor con el que el control NO hace nada. Los de
// profundidad (tremolo, chorus) son 0 â€”el efecto esta apagadoâ€” y el resto 50.
static float ampExtraNeutral(int kind) {
    switch (kind) {
    case 3: case 6: return 0.0f;      // profundidad: apagado
    default:        return 50.0f;     // resonancia, nivel, velocidad, compresion
    }
}

// Valor del control extra que hace la funcion `kind` en el ampli m, o su neutro.
static float ampExtraVal(int m, int kind) {
    for (int i = 0; i < 2; ++i)
        if (ampExtraKindAt(m, i) == kind) return g_params.amp[m][6 + i].load();
    return ampExtraNeutral(kind);
}

static int ampExtraParams(int m, int* out, int cap) {
    // El PERFIL (captura NAM) no tiene presencia: su frontal imprime cinco
    // mandos y esos cinco son todos los que la captura necesita. Montar un
    // PRESENCE sin accion seria un control que miente.
    if (m == 12) return 0;
    int n = 0;
    for (int k = 0; k < 6 && n < cap; ++k)
        if (kAmpArt[m].map[k] < 0) out[n++] = k;
    return n;
}
static const char*   kCabNames[6]   = { "1x12 COMBO", "2x12 OPEN", "4x12 CLOSED",
                                        "4x12 MODERN", "1x15 BASS", "BOUTIQUE 2x10" };
static const WCHAR*  kCabMicNames[3]  = { L"DYN", L"RIBBON", L"COND" };
static const WCHAR*  kCabDistNames[3] = { L"NEAR", L"MID", L"FAR" };

// Knobs arrastrables
struct KnobUI {
    const char*         label;
    std::atomic<float>* value;
    float vmin, vmax;
    int x, y, r;
    bool dB;
    int amp  = -1;   // ampli al que pertenece un mando del frontal (-1: no es de amp)
    int hole = -1;   // hueco serigrafiado que ocupa (-1: mando de banda)
};
static std::vector<KnobUI> g_knobs;
// Valor de referencia de cada mando, guardado POR PARAMETRO (no por
// posicion): los mandos del frontal cambian de orden y de numero con cada
// ampli, asi que un vector paralelo acabaria devolviendo el valor de otro
// mando. El doble clic sobre un mando lo restaura.
static std::vector<std::pair<std::atomic<float>*, float> > g_knobsDef;
static float knobDefault(std::atomic<float>* v) {
    for (size_t i = 0; i < g_knobsDef.size(); ++i)
        if (g_knobsDef[i].first == v) return g_knobsDef[i].second;
    return -1e9f;                     // sin referencia registrada
}
static void knobRemember(std::atomic<float>* v, float val) {
    for (size_t i = 0; i < g_knobsDef.size(); ++i)
        if (g_knobsDef[i].first == v) { g_knobsDef[i].second = val; return; }
    g_knobsDef.push_back(std::make_pair(v, val));
}
// Cuantos de los mandos registrados son del ampli (frontal + banda). Antes el
// corte entre mandos de ampli y de pedal era el literal 6: con los mandos de
// banda de AMBER TWEED el corte se corria y el fantasma de arrastre se comia
// perillas del ampli.
static int   g_ampKnobCount = 6;
static int   g_dragIdx = -1;
static int   g_dragY = 0;
static float g_dragStart = 0.0f;
static bool  g_dragFine  = false;   // SHIFT pulsado al empezar el arrastre del mando
// Estados visuales: hover del raton y retencion de pico de los medidores
static int   g_mouseX = -1, g_mouseY = -1;
static bool  g_mouseTracking = false;
static float g_pkHoldIn = 0.0f, g_pkHoldOut = 0.0f, g_pkHoldGr = 0.0f;

// Boton izquierdo del raton pulsado. Se marca en WM_LBUTTONDOWN y se libera
// en WM_LBUTTONUP / WM_MOUSEMOVE (sin boton): da feedback de "press" en toda
// la capa dinamica, como un control fisico de verdad.
static bool g_lbtn = false;

static bool mouseIn(int x0, int y0, int x1, int y1) {
    return g_mouseX >= x0 && g_mouseX < x1 && g_mouseY >= y0 && g_mouseY < y1;
}

// ---------------------------------------------------------------------------
//  TABLA UNICA DE ZONAS
//  Cada rectangulo de control estaba escrito TRES veces: lo pinta la capa
//  dinamica, lo mira hoverKey y lo lee WM_LBUTTONDOWN. Cuando uno cambiaba y
//  los otros no, el clic dejaba de coincidir con lo que se veia: ese fue el
//  fallo del chip T3K de la cadena, que caia justo encima del primer bloque.
//  Ahora se calcula UNA vez por frame en buildZones() y los tres leen la misma
//  tabla. Los identificadores son los que hoverKey ya devolvia, asi que el
//  codigo que compara contra 4, 5, 6, 70... sigue igual.
//  Los mandos (circulares) y el panel TONE3000 se quedan fuera a proposito:
//  cada uno tiene su propio hit-test y ya lo usan todos sus sitios.
// ---------------------------------------------------------------------------
static const int ZONE_MAX = 260;
struct Zone { int x0, y0, x1, y1; };
static Zone g_zone[ZONE_MAX];
static bool g_zoneOn[ZONE_MAX];

static void zoneBeginFrame() {
    for (int i = 0; i < ZONE_MAX; ++i) g_zoneOn[i] = false;
}
static void zoneSet(int id, int x0, int y0, int x1, int y1) {
    if (id <= 0 || id >= ZONE_MAX) return;
    g_zone[id].x0 = x0; g_zone[id].y0 = y0;
    g_zone[id].x1 = x1; g_zone[id].y1 = y1;
    g_zoneOn[id] = true;
}
static void zoneSetBox(int id, int x, int y, int w, int h) { zoneSet(id, x, y, x + w, y + h); }
static bool zoneHit(int id) {
    if (id <= 0 || id >= ZONE_MAX || !g_zoneOn[id]) return false;
    return mouseIn(g_zone[id].x0, g_zone[id].y0, g_zone[id].x1, g_zone[id].y1);
}
static bool zoneHitAt(int id, int x, int y) {
    if (id <= 0 || id >= ZONE_MAX || !g_zoneOn[id]) return false;
    return x >= g_zone[id].x0 && x < g_zone[id].x1 && y >= g_zone[id].y0 && y < g_zone[id].y1;
}
// El centro de una zona: lo usa el pintado para colocar iconos y texto.
static void zoneCenter(int id, int& cx, int& cy) {
    cx = (g_zone[id].x0 + g_zone[id].x1)/2;
    cy = (g_zone[id].y0 + g_zone[id].y1)/2;
}
// El rectangulo de una zona, para el codigo que ya trabaja con RECT + PtInRect.
// Si la zona no existe este frame (modal abierto), sale un rectangulo vacio y
// PtInRect nunca acierta: el clic queda desactivado sin ningun caso especial.
static RECT zoneRect(int id) {
    RECT r = { 0, 0, 0, 0 };
    if (id > 0 && id < ZONE_MAX && g_zoneOn[id]) {
        r.left = g_zone[id].x0; r.top    = g_zone[id].y0;
        r.right = g_zone[id].x1; r.bottom = g_zone[id].y1;
    }
    return r;
}
// Cuerpo mas abajo (junto a hoverKey, que es su primer lector): se declara aqui
// porque paintWindow lo llama antes de llegar a su definicion.
static void buildZones();

// --- Cache de render: escena estatica + buffer trasero reutilizados ---
// La escena estatica (toxel, paÃ±os, caras, chasis) se renderiza UNA vez por
// tamano; cada frame solo se dibuja la capa dinamica. Sin esto, GDI+ tarda
// decenas de ms por frame y la interfaz se ve a trompicones.
struct StaticScene {
    HDC dc = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr;
    int w = 0, h = 0; bool valid = false;
};
static StaticScene g_scene;
static HDC     g_bufDC = nullptr;
static HBITMAP g_bufBmp = nullptr;
static HGDIOBJ g_bufOld = nullptr;
// Vista GDI+ de los bits del buffer trasero. Con GDI+ sobre el HDC, cada
// primitiva obliga a reconvertir el formato de la superficie (medido: 77ms por
// frame); con un Bitmap de GDI+ sobre los mismos bits, pinta directo en memoria.
static G::Bitmap* g_bufGpBmp = nullptr;
static void*      g_bufBits  = nullptr;
static int     g_bufW = 0, g_bufH = 0;
static int     g_lastHoverKey = 0;

static void freeStaticScene() {
    if (g_scene.dc) {
        if (g_scene.old) SelectObject(g_scene.dc, g_scene.old);
        if (g_scene.bmp) DeleteObject(g_scene.bmp);
        DeleteDC(g_scene.dc);
    }
    g_scene.dc = nullptr; g_scene.bmp = nullptr; g_scene.old = nullptr;
    g_scene.w = g_scene.h = 0; g_scene.valid = false;
}
// Superficie de pintado: DIB de 32bpp top-down en memoria del sistema.
// Un CreateCompatibleBitmap (formato de pantalla) obliga a GDI+ a reconvertir
// en cada primitiva; con un DIB 32bpp pinta directo sobre los bits. Medido en
// esta maquina: un frame completo pasaba de ~90ms a ~10ms.
static HBITMAP createSurfaceDib(HDC ref, int w, int h, void** outBits = nullptr) {
    if (outBits) *outBits = nullptr;
    if (w <= 0 || h <= 0) return nullptr;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;      // top-down (fila 0 arriba)
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && outBits) *outBits = bits;
    return bmp;
}
// GDI+ sobre un HDC deja el canal alfa a 0; las superficies que luego se usan
// como origen de AlphaBlend (la plantilla del fantasma) lo necesitan opaco.
static void surfaceDibOpaque(void* bits, int w, int h) {
    if (!bits) return;
    BYTE* p = (BYTE*)bits;
    for (size_t i = 0, n = (size_t)w*(size_t)h; i < n; ++i) p[i*4 + 3] = 255;
}

static void freeBackbuffer() {
    if (g_bufGpBmp) { delete g_bufGpBmp; g_bufGpBmp = nullptr; }
    g_bufBits = nullptr;
    if (g_bufDC) {
        if (g_bufOld) SelectObject(g_bufDC, g_bufOld);
        if (g_bufBmp) DeleteObject(g_bufBmp);
        DeleteDC(g_bufDC);
    }
    g_bufDC = nullptr; g_bufBmp = nullptr; g_bufOld = nullptr;
    g_bufW = g_bufH = 0;
}

// --- Estado del pedalboard: arrastre y panel de anadido ---
static int  g_dragPedalRow = -1, g_dragPedalIdx = -1, g_dragPedalType = -1;
static int  g_dropRow = -1, g_dropIdx = -1;             // insercion valida
static int  g_dropSwap = -1;                            // plaza a INTERCAMBIAR (arrastrar sobre una tarjeta)
static bool g_addOpen = false;                          // panel "anadir pedal"
static int  g_addRow = 0;                               // fila destino del anadido
static bool g_brwOpen = false;                          // navegador de presets (paso 18)

// Un solo modal a la vez. Cada apertura solo ponia SU flag, asi que podian
// coexistir AJUSTES + AFINADOR o TONE3000 + RENOMBRAR: el orden de pintado
// (add < t3k < settings < tuner < rename) dejaba el de abajo robando el clic
// al de arriba, y el hoverKey ni siquiera conocia a RENOMBRAR.
static void modalCloseAll() {
    g_topOpen      = false;
    g_settingsOpen = false;
    g_addOpen      = false;
    g_t3kOpen      = false;
    g_brwOpen      = false;
    g_tunerOpen.store(false);
    g_renameOpen   = false;
    g_renameSave   = false;
    g_renameIdx    = -1;
    g_delArm       = -1;
    g_pedDelArm    = -1;   // un pedal armado tambien se desarma al abrir un modal
    g_t3kConnOpen  = false;
    g_t3kFocus     = 0;
    g_sceneDirty.store(true);       // el fondo se vuelve a ver entero
}

// Abre el modal de nombre en sus dos modos: renombrar un preset del usuario
// (idx = el que se renombra, con su nombre en el campo) o GUARDAR COMO (idx = -1,
// con el nombre del lote que suena como propuesta, para que se pueda escribir
// encima o aceptar tal cual).
static void openNameModal(int idx, bool saveAs, const WCHAR* suggested) {
    modalCloseAll();                       // un modal a la vez
    g_renameIdx  = idx;
    g_renameSave = saveAs;
    if (suggested && suggested[0]) wcsncpy_s(g_renameBuf, suggested, _TRUNCATE);
    else                           g_renameBuf[0] = 0;
    g_renameOpen = true;
}

static void rebuildKnobs() {
    g_knobs.clear();
    // Knobs del amp activo (6): sobre el frontal si hay arte (posiciones del
    // JSON del zip); sin arte, la fila tipica bajo la cara procedural
    const int m = g_params.model.load();
    artEnsureStrip(m);   // el filmstrip acompana al amp activo
    g_ampKnobCount = 6;
    if (g_artOk[m]) {
        for (int k = 0; k < 6; ++k) {
            // Hueco del frontal (map). La etiqueta es LA IMPRESA en la chapa:
            // antes el tooltip decia MIDDLE sobre una perilla serigrafiada
            // MASTER, y eso es la interfaz contradiciendo al producto.
            const int pi = kAmpArt[m].map[k];
            if (pi < 0) continue;
            const int kx = (int)(ART_X + kAmpArt[m].ax[pi]*(G::REAL)ART_W/1200.0f + 0.5f);
            const int ky = (int)(ART_Y + kAmpArt[m].ay[pi]*(G::REAL)ART_H/540.0f + 0.5f);
            g_knobs.push_back({ kAmpKnobNames[k], &g_params.amp[m][k], 0.0f, 100.0f, kx, ky,
                                (int)(artKnobR() + 0.5f), false, m, pi });
        }
        // Hueco impreso que lleva el control EXTRA del ampli (RESONANCE en HAVOC,
        // OUTPUT/VOLUME/MASTER en RECTO/KRAFT): ese mando tiene accion de verdad.
        {
            int eh2[2];
            const int neh = ampExtraHoles(m, eh2);
            for (int i = 0; i < neh; ++i) {
                const int hh = eh2[i];
                const int kx = (int)(ART_X + kAmpArt[m].ax[hh]*(G::REAL)ART_W/1200.0f + 0.5f);
                const int ky = (int)(ART_Y + kAmpArt[m].ay[hh]*(G::REAL)ART_H/540.0f + 0.5f);
                g_knobs.push_back({ kAmpKnobNames[0], &g_params.amp[m][6 + i], 0.0f, 100.0f, kx, ky,
                                    (int)(artKnobR() + 0.5f), false, m, hh });
            }
        }
        // Parametros que el frontal no imprime (solo AMBER TWEED: su chapa
        // serigrafia tres mandos para seis parametros de motor). Se integran EN
        // LA PROPIA CHAPA, en los huecos que dejan los mandos impresos, a su
        // misma altura y con etiquetas de su misma voz: la fila lee como seis
        // mandos del ampli, no como tres controles colgados del chrome.
        int ex[6];
        const int ne = ampExtraParams(m, ex, 6);
        for (int i = 0; i < ne; ++i) {
            const int k = ex[i];
            const int ax = 270 + i*220;                      // art x: huecos 1, 2 y 3
            const int kx = (int)(ART_X + ax*(G::REAL)ART_W/1200.0f + 0.5f);
            const int ky = (int)(ART_Y + 164*(G::REAL)ART_H/540.0f + 0.5f);
            g_knobs.push_back({ kAmpKnobNames[k], &g_params.amp[m][k], 0.0f, 100.0f,
                                kx, ky, AMP_EXTRA_R, false, m, i });
        }
        g_ampKnobCount = 6 + ne;
    } else {
        for (int k = 0; k < 6; ++k)
            g_knobs.push_back({ kAmpKnobNames[k], &g_params.amp[m][k], 0.0f, 100.0f,
                                (int)(ART_X + ART_W*(k + 1)/7), ART_Y + ART_H*55/100, 24, false });
    }
    // Knobs de los pedales en la cadena (2 por pedal, en su slot)
    std::vector<int> pre, post;
    {
        std::lock_guard<std::mutex> lock(g_chainMutex);
        pre = g_chainPre; post = g_chainPost;
    }
    for (int row = 0; row < 2; ++row) {
        const std::vector<int>& ch = (row == 0) ? pre : post;
        for (size_t i = 0; i < ch.size(); ++i) {
            const int t = ch[i];
            const int sx = (int)pedSlotX((int)i);
            const int cy = PED_ROW_Y[row] + PED_KB_Y;
            const int cx1 = sx + PED_KB1_X, cx2 = sx + PED_KB2_X;
            if (t == 0)
                g_knobs.push_back({ kFxParams[0][0], &g_params.fx[0][0], -80.0f, -20.0f, cx1, cy, PED_KB_R, true });
            else
                g_knobs.push_back({ kFxParams[t][0], &g_params.fx[t][0], 0.0f, 100.0f, cx1, cy, PED_KB_R, false });
            g_knobs.push_back({ kFxParams[t][1], &g_params.fx[t][1], 0.0f, 100.0f, cx2, cy, PED_KB_R, false });
        }
    }
    // Trim de entrada del canal: SIEMPRE antes de master (el pintado reconoce
    // a master por ser el ultimo y a INPUT por ser el penultimo).
    g_knobs.push_back({ "TRIM", &g_params.inTrim, -18.0f, 18.0f, IN_TRIM_X, IN_TRIM_Y, IN_TRIM_R, true });
    // Master (siempre el ultimo: el pintado lo reconoce por su indice). La mezcla
    // del cab es el slider MEZCLA del panel derecho, no un knob.
    g_knobs.push_back({ "MASTER", &g_params.master, 0.0f, 100.0f, MASTER_X, MASTER_Y, MASTER_R, false });

    // Valores de referencia para el doble clic. Se toman UNA sola vez, al
    // construir por primera vez (despues de cargar el ini): si se tomaran en
    // cada reconstruccion, el doble clic acabaria "restaurando" lo que el
    // usuario acababa de mover. Se registran por parametro, no por posicion.
    for (const KnobUI& k : g_knobs) {
        const float d = knobDefault(k.value);
        if (d < -1e8f) knobRemember(k.value, k.value->load());
    }
}

static int hitKnob(int x, int y) {
    for (size_t i = 0; i < g_knobs.size(); ++i) {
        const KnobUI& k = g_knobs[i];
        int dx = x - k.x, dy = y - k.y;
        // Mismo radio que el hover y el tooltip (k.r+10): con k.r+8 quedaba un
        // anillo de 2px donde salia el cursor de arrastre y el clic no arrastraba.
        int rr = k.r + 10;
        if (dx*dx + dy*dy <= rr*rr) return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
//  Helpers de dibujo GDI+
// ---------------------------------------------------------------------------
// AVANCE REAL DE UN GLIFO para el texto espaciado.
//  MeasureString, sin un StringFormat explicito, devuelve la CAJA DE
//  MAQUETACION: anade ~1/6 em de relleno por caracter (3px a 10pt, 5px a 21pt).
//  Sumar ese relleno al tracking declarado dejaba el espaciado real en ~3x lo
//  previsto (los rotulos se leian letra a letra) y hacia creer a fitSpacedFont
//  que nada cabia, bajando todo al cuerpo minimo. Con NoFitBlackBox se mide la
//  tinta del glifo y el tracking vuelve a ser el declarado. El espacio no tiene
//  tinta: se le da el avance nominal de la fuente.
// Relleno de maquetacion que GDI+ suma a CADA MeasureString (4px a 9pt, y
// constante para un mismo tamano: medido, W("A")=12.7 con avance real 8.7).
// Se calcula una vez por tamano/estilo y se resta del avance.
static G::REAL spacedPad(G::Graphics& g, const G::Font& f) {
    struct PadCache { float pt; int style; G::REAL pad; };
    static PadCache cache[24];
    static int n = 0;
    const float pt = f.GetSize();
    const int   st = (int)f.GetStyle();
    for (int i = 0; i < n; ++i)
        if (cache[i].pt == pt && cache[i].style == st) return cache[i].pad;
    G::RectF bx, bh, bxh;
    const G::RectF box(0.0f, 0.0f, 400.0f, 80.0f);
    g.MeasureString(L"x",  -1, &f, box, nullptr, &bx);
    g.MeasureString(L"H",  -1, &f, box, nullptr, &bh);
    g.MeasureString(L"xH", -1, &f, box, nullptr, &bxh);
    const G::REAL pad = bx.Width + bh.Width - bxh.Width;
    if (n < 24) { cache[n].pt = pt; cache[n].style = st; cache[n].pad = pad; ++n; }
    return pad;
}

static G::REAL glyphAdvance(G::Graphics& g, const G::Font& f, WCHAR ch) {
    if (ch == L' ')
        return (G::REAL)f.GetSize()*(96.0f/72.0f)*0.26f;
    const WCHAR c[2] = { ch, 0 };
    G::RectF b;
    g.MeasureString(c, -1, &f, G::RectF(0.0f, 0.0f, 400.0f, 80.0f), nullptr, &b);
    const G::REAL a = b.Width - spacedPad(g, f);
    return (a > 0.0f) ? a : b.Width;
}

static G::REAL drawSpaced(G::Graphics& g, const WCHAR* s, const G::Font& f, G::Color c,
                          G::REAL x, G::REAL y, G::REAL spacing) {
    s = tr(s);
    G::SolidBrush br(c);
    // NoClip: la tilde de una mayuscula (É) sobresale de la caja de la celda y,
    // sin esta bandera, GDI+ la recortaba al escalar (a 1,25 se leia "DESPUES").
    G::StringFormat sf((INT)(G::StringFormatFlagsNoFitBlackBox | G::StringFormatFlagsNoClip));
    G::REAL cx = x;
    for (const WCHAR* p = s; *p; ++p) {
        const WCHAR ch[2] = { *p, 0 };
        g.DrawString(ch, -1, &f, G::RectF(cx, y, 300.0f, 50.0f), &sf, &br);
        cx += glyphAdvance(g, f, *p) + spacing;
    }
    return cx;
}

// Igual que drawSpaced pero con un pincel cualquiera: es lo que permite el
// wordmark en metal (degradado dentro de las letras, no un color plano).
static G::REAL drawSpacedB(G::Graphics& g, const WCHAR* s, const G::Font& f, G::Brush* br,
                           G::REAL x, G::REAL y, G::REAL spacing) {
    s = tr(s);
    G::REAL cx = x;
    for (const WCHAR* p = s; *p; ++p) {
        WCHAR ch[2] = { *p, 0 };
        g.DrawString(ch, -1, &f, G::RectF(cx, y, 300.0f, 50.0f), nullptr, br);
        G::RectF bounds;
        g.MeasureString(ch, -1, &f, G::RectF(0, 0, 300.0f, 50.0f), nullptr, &bounds);
        cx += bounds.Width + spacing;
    }
    return cx;
}

// Ancho total de un texto con espaciado entre caracteres
static G::REAL measureSpaced(G::Graphics& g, const WCHAR* s, const G::Font& f, G::REAL spacing) {
    G::REAL w = 0.0f;
    int n = 0;
    for (const WCHAR* p = s; *p; ++p, ++n)
        w += glyphAdvance(g, f, *p) + spacing;
    return (n > 0) ? (w - spacing) : 0.0f;
}

// Elige el cuerpo (<= ptBase) y el espaciado que caben en maxW
static void fitSpacedFont(G::Graphics& g, const WCHAR* s, G::FontStyle style,
                          G::REAL spacing, G::REAL maxW, G::REAL ptBase, G::REAL ptMin,
                          G::REAL& ptOut, G::REAL& spOut, const WCHAR* fam = kFontUI) {
    ptOut = ptBase; spOut = spacing;
    // El bucle tiene que llegar hasta ptMin. Con un numero fijo de vueltas
    // (8, o sea 4 pt de margen) los textos que nacen muy por encima del suelo
    // -el nombre del perfil, que usa sh*0.22 = ~23 pt contra un suelo de 9-
    // se quedaban a medio encoger y no cabian nunca: ni al tamano de diseno
    // con un nombre largo, y menos aun con la ventana en su minimo. El texto
    // que ya cabe sale en la primera vuelta, asi que esto solo cambia el
    // resultado de los que antes se recortaban sin haber probado a encogerse.
    const int steps = (int)((ptBase - ptMin) / 0.5f) + 1;
    for (int i = 0; i < steps; ++i) {
        G::Font f(fam, ptOut, style);
        if (measureSpaced(g, s, f, spOut) <= maxW || ptOut <= ptMin) return;
        ptOut -= 0.5f;
        if (ptOut < ptMin) ptOut = ptMin;
        if (ptOut <= ptMin) spOut = spacing*0.45f;
    }
}

// Recorta por la cola con puntos suspensivos hasta que quepa. Existe porque
// fitSpacedFont tiene un suelo (ptMin): si ni al tamano minimo el texto cabe -un
// rotulo largo, una ruta, un titulo de disco-, antes se dibujaba igual y se salia
// de su caja y se montaba encima de lo de al lado (el boton ABRIR del panel de
// TONE3000 llevaba "ABRIR " + la URL entera, ~55 caracteres en 300px).
static std::wstring fitEllipsis(G::Graphics& g, const WCHAR* s, const G::Font& f,
                                G::REAL sp, G::REAL maxW) {
    std::wstring t(s);
    if (measureSpaced(g, t.c_str(), f, sp) <= maxW) return t;
    while (!t.empty() && measureSpaced(g, (t + L"\x2026").c_str(), f, sp) > maxW)
        t.pop_back();
    return t + L"\x2026";
}

// ROTULO DE UNA LINEA con su kerning normal (DrawString). Si no cabe en su caja
// baja el cuerpo medio punto a medio punto hasta ptMin y, si ni asi cabe, corta
// por la cola con puntos suspensivos. Existe porque DrawString a secas RECORTA
// el texto contra la caja: asi se leia "MARAUDER 80" en la lista de amplis.
static void drawTextFit(G::Graphics& g, const WCHAR* s, G::FontStyle style, G::Color c,
                        G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                        G::REAL pt, G::REAL ptMin, const WCHAR* fam = kFontUI,
                        bool center = false, bool vcenter = false) {
    s = tr(s);
    if (!s || !s[0]) return;
    G::StringFormat sf;
    sf.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoClip);
    if (center)  sf.SetAlignment(G::StringAlignmentCenter);     // eje horizontal
    if (vcenter) sf.SetLineAlignment(G::StringAlignmentCenter); // eje vertical
    const G::RectF box(0, 0, 4000, h);
    G::RectF b;
    G::REAL size = pt;
    for (;;) {
        G::Font f(fam, size, style);
        g.MeasureString(s, -1, &f, box, &sf, &b);
        if (b.Width <= w || size <= ptMin) break;
        size -= 0.5f;
        if (size < ptMin) size = ptMin;
    }
    G::Font f(fam, size, style);
    g.MeasureString(s, -1, &f, box, &sf, &b);
    // Con la interfaz ESCALADA, GridFit redondea el avance de cada glifo a
    // pixeles enteros de dispositivo: MeasureString mide el avance ideal y la
    // linea sale 2-5px mas ancha, asi que se comia la ultima letra ("MARAUDER 8").
    // Para este rotulo se pinta sin GridFit cuando hay escala (medida y pintado
    // vuelven a coincidir) y se deja el hint como estaba. A 1:1 no cambia nada.
    const G::TextRenderingHint hint0 = (g_uiS != 1.0f) ? G::TextRenderingHintAntiAlias
                                                      : G::TextRenderingHintAntiAliasGridFit;
    g.SetTextRenderingHint(hint0);
    G::SolidBrush br(c);
    if (b.Width <= w) {
        g.DrawString(s, -1, &f, G::RectF(x, y, w, h), &sf, &br);
        g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
        return;
    }
    std::wstring t(s);
    while (t.size() > 1) {
        t.pop_back();
        const std::wstring tp = t + L"\x2026";
        g.MeasureString(tp.c_str(), -1, &f, box, &sf, &b);
        if (b.Width <= w) {
            g.DrawString(tp.c_str(), -1, &f, G::RectF(x, y, w, h), &sf, &br);
            g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
            return;
        }
    }
    g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
}

// drawSpacedFit garantiza que el texto cabe en maxW: encoge hasta ptMin y, si aun
// asi no cabe, corta con puntos suspensivos. Nunca se sale de su caja.
static G::REAL drawSpacedFit(G::Graphics& g, const WCHAR* s, G::FontStyle style, G::Color c,
                             G::REAL x, G::REAL y, G::REAL spacing, G::REAL maxW,
                             G::REAL ptBase, G::REAL ptMin, const WCHAR* fam = kFontUI) {
    s = tr(s);
    G::REAL pt, sp;
    fitSpacedFont(g, s, style, spacing, maxW, ptBase, ptMin, pt, sp, fam);
    G::Font f(fam, pt, style);
    const std::wstring t = fitEllipsis(g, s, f, sp, maxW);
    return drawSpaced(g, t.c_str(), f, c, x, y, sp);
}

// Igual, pero centrado en cx (placas, medallones, botonones)
static void drawSpacedFitC(G::Graphics& g, const WCHAR* s, G::FontStyle style, G::Color c,
                           G::REAL cx, G::REAL y, G::REAL spacing, G::REAL maxW,
                           G::REAL ptBase, G::REAL ptMin, const WCHAR* fam = kFontUI) {
    s = tr(s);
    G::REAL pt, sp;
    fitSpacedFont(g, s, style, spacing, maxW, ptBase, ptMin, pt, sp, fam);
    G::Font f(fam, pt, style);
    const std::wstring t = fitEllipsis(g, s, f, sp, maxW);
    const G::REAL w = measureSpaced(g, t.c_str(), f, sp);
    drawSpaced(g, t.c_str(), f, c, cx - w/2.0f, y, sp);
}

// --- Los dos embudos por PAPEL ----------------------------------------------
// Un sitio dice QUE ES su texto (TxRole) y aqui se resuelven familia, medida y
// suelo de ajuste desde kTx. Asi la jerarquia entera se cambia en un sitio, y
// ningun sitio de texto puede inventarse una medida propia.
static G::REAL drawTxFit(G::Graphics& g, const WCHAR* s, G::FontStyle style, G::Color c,
                         G::REAL x, G::REAL y, G::REAL spacing, G::REAL maxW, TxRole r) {
    return drawSpacedFit(g, s, style, c, x, y, spacing, maxW,
                         kTx[r].pt, kTx[r].min, kTx[r].fam);
}
static void drawTxFitC(G::Graphics& g, const WCHAR* s, G::FontStyle style, G::Color c,
                       G::REAL cx, G::REAL y, G::REAL spacing, G::REAL maxW, TxRole r) {
    drawSpacedFitC(g, s, style, c, cx, y, spacing, maxW,
                   kTx[r].pt, kTx[r].min, kTx[r].fam);
}

static void roundRect(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                      G::REAL r, G::Pen* pen, G::Brush* br) {
    if (r < 1.0f) r = 1.0f;
    G::GraphicsPath p;
    G::REAL d = r*2.0f;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    if (br) g.FillPath(br, &p);
    if (pen) g.DrawPath(pen, &p);
}

// --- Cache del arte escalado -------------------------------------------------
// GDI+ escala muy lento: reducir la foto del frontal a la miniatura de la lista
// costaba ~4 ms por fila (~40 ms de frame) y escalar el fotograma de 224x224
// del filmstrip ~5 ms por perilla (~29 ms las seis del frontal). Ambas cosas se
// rasterizan UNA vez a la medida de destino; por frame solo queda un blit 1:1.
struct AmpThumb { G::Bitmap* normal = nullptr; G::Bitmap* dim = nullptr; };
static AmpThumb g_thumb[13];
static void freeAmpThumbs() {
    for (int i = 0; i < 13; ++i) {
        delete g_thumb[i].normal; delete g_thumb[i].dim;
        g_thumb[i] = AmpThumb();
    }
}
// Celda de la miniatura de la lista: marco hundido + foto recortada a las
// esquinas, exactamente como se pintaba por frame. dim = con el velo que llevan
// las filas no activas (antes se aplicaba dentro del mismo clip).
static G::Bitmap* ampThumb(int a, bool dim) {
    if (a < 0 || a >= kAmpCount) return nullptr;
    G::Image* img = g_fondoImg[a];
    if (!img) return nullptr;
    G::Bitmap*& slot = dim ? g_thumb[a].dim : g_thumb[a].normal;
    if (slot) return slot;
    const int iw = (int)img->GetWidth(), ih = (int)img->GetHeight();
    if (iw <= 0 || ih <= 0) return nullptr;
    const G::REAL TW = 88.0f, TH = 40.0f;
    G::Bitmap* bm = new G::Bitmap((INT)TW, (INT)TH, PixelFormat32bppPARGB);
    if (!bm || bm->GetLastStatus() != G::Ok) { delete bm; return nullptr; }
    {
        G::Graphics gs(bm);
        gs.SetSmoothingMode(G::SmoothingModeAntiAlias);
        gs.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
        G::SolidBrush mb(G::Color(255, 0x0d, 0x0a, 0x03));
        G::Pen mp(G::Color(46, 0xe6, 0xe3, 0xdc), 1.0f);
        roundRect(gs, 0.0f, 0.0f, TW, TH, 5.0f, &mp, &mb);
        G::REAL k = (TW - 6.0f)/(G::REAL)iw;
        if ((TH - 6.0f)/(G::REAL)ih < k) k = (TH - 6.0f)/(G::REAL)ih;
        const G::REAL dw = (G::REAL)iw*k, dh = (G::REAL)ih*k;
        const G::REAL dx = (TW - dw)/2.0f, dy = (TH - dh)/2.0f;
        G::GraphicsPath cl;
        {
            const G::REAL d = 8.0f;
            cl.AddArc(2.0f, 2.0f, d, d, 180, 90);
            cl.AddArc(TW - d - 2.0f, 2.0f, d, d, 270, 90);
            cl.AddArc(TW - d - 2.0f, TH - d - 2.0f, d, d, 0, 90);
            cl.AddArc(2.0f, TH - d - 2.0f, d, d, 90, 90);
            cl.CloseFigure();
        }
        gs.SetClip(&cl, G::CombineModeReplace);
        gs.DrawImage(img, G::RectF(dx, dy, dw, dh),
                     0.0f, 0.0f, (G::REAL)iw, (G::REAL)ih, G::UnitPixel);
        if (dim) {
            G::SolidBrush veil(G::Color(64, 0x0d, 0x0a, 0x03));
            gs.FillRectangle(&veil, dx - 1.0f, dy - 1.0f, dw + 2.0f, dh + 2.0f);
        }
    }
    slot = bm;
    return bm;
}

// Filmstrip escalado al diametro del knob, fotograma a fotograma y a peticion:
// asi cambiar de amp no cuesta un tiron (solo se paga por lo que se llega a ver)
// y cada perilla se dibuja luego con un blit 1:1.
struct StripCache {
    const G::Image* src = nullptr;
    int d = 0;
    std::vector<G::Bitmap*> frames;
};
static StripCache g_stripCache;
static void freeScaledStrip() {
    for (size_t i = 0; i < g_stripCache.frames.size(); ++i) delete g_stripCache.frames[i];
    g_stripCache.frames.clear();
    g_stripCache.src = nullptr;
    g_stripCache.d = 0;
}
static G::Bitmap* scaledStripFrame(G::Image* strip, int d, int fr) {
    if (!strip || d <= 0) return nullptr;
    const int total = (int)(strip->GetHeight()/224);
    if (total <= 0) return nullptr;
    if (fr < 0) fr = 0;
    if (fr >= total) fr = total - 1;
    if (g_stripCache.src != strip || g_stripCache.d != d) {
        freeScaledStrip();
        g_stripCache.src = strip;
        g_stripCache.d = d;
        g_stripCache.frames.assign((size_t)total, (G::Bitmap*)nullptr);
    }
    G::Bitmap*& slot = g_stripCache.frames[(size_t)fr];
    if (slot) return slot;
    G::Bitmap* bm = new G::Bitmap(d, d, PixelFormat32bppPARGB);
    if (!bm || bm->GetLastStatus() != G::Ok) { delete bm; return nullptr; }
    {
        G::Graphics gs(bm);
        gs.SetSmoothingMode(G::SmoothingModeHighQuality);
        gs.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
        gs.DrawImage(strip, G::RectF(0.0f, 0.0f, (G::REAL)d, (G::REAL)d),
                     0.0f, (G::REAL)(fr*224), 224.0f, 224.0f, G::UnitPixel);
    }
    slot = bm;
    return bm;
}

// Sombra proyectada. El lenguaje plano NO proyecta sombras: la profundidad la da
// el tono. La funcion se queda con su firma (y sin cuerpo) para no tener que
// tocar los sitios que la llaman ni las piezas que la reciben por parametro.
static void softShadow(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                       G::REAL r, BYTE baseAlpha = 120) {
    (void)g; (void)x; (void)y; (void)w; (void)h; (void)r; (void)baseAlpha;
}

// ---------------------------------------------------------------------------
//  Piel compartida: una sola paleta y un solo canto para toda la app.
//  Antes cada panel/boton inventaba su azul, su radio y su hover, y el
//  resultado se leia como una maqueta: aqui queda todo en un sitio.
// ---------------------------------------------------------------------------
// PLACA: el bloque con el que se construye casi toda la app.
//  sombra de contacto ->  se apoya sobre el fondo, no flota
//  dos tonos planos    ->  la luz viene de arriba, sin degradado decorativo
//  filete de 1px       ->  un solo filo claro arriba; abajo nada
//  (sin grano: sobre negro el ruido de 1px lee como suciedad, no como material)

// Velo de press: solo si el boton izq. esta pulsado Y el raton encima.
// Mismo trato para todos los controles pulsables de la UI.
static void pressVeil(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h, G::REAL r = 7.0f) {
    if (!g_lbtn || !mouseIn((int)x, (int)y, (int)(x + w), (int)(y + h))) return;
    G::SolidBrush veil(G::Color(70, 0, 0, 0));
    roundRect(g, x, y, w, h, r, nullptr, &veil);
}

// ---------------------------------------------------------------------------
//  SUPERFICIES. El lenguaje plano (v3) es el ACTUAL desde el rediseno. Estas
//  funciones conservan su nombre y su FIRMA originales para no tocar los ~40
//  sitios que las llaman, pero ya delegan en las piezas planas: UNA sola
//  implementacion de cada superficie, la que el autotest verifica con pixeles
//  en J1..J6 y que este rediseno no puede degradar sin que salte el banco.
//  `shadow` se acepta y se ignora: el lenguaje plano no proyecta sombras.
// ---------------------------------------------------------------------------
// Las piezas planas viven en su seccion, mas abajo; se declaran aqui para que
// estas puedan delegar en ellas. (La definicion trae los argumentos por defecto.)
static void flatPanel(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                      G::REAL r, bool border);
static void flatWell(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h, G::REAL r);
static void flatPillBody(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                         bool hov, bool lit, G::Color litCol);

// PLACA: un tono y un filete de 1px. La usan casi todos los paneles de la app,
// asi que el cambio a plano se ve de golpe en toda la ventana.
static void glassPanel(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                       G::REAL r, bool shadow = true) {
    (void)shadow;
    flatPanel(g, x, y, w, h, r, true);
}

// El tono del estante de pedales. Lo usa la plantilla de pedales: se blitea
// OPACA, asi que los 6px de margen tienen que traer cocido el tono de la unidad
// sobre la que se apoya. Sin esto, cada pedal lucia un marco opaco sobre el
// estante. (Con el estante plano el tono ya no depende de la altura.)
static G::Color glassBandAt(G::REAL f) {
    (void)f;
    return kFSurf;   // el estante plano es un solo tono: no hay banda que seguir
}

// POZO: la superficie hundida del sistema (visualizadores, canales de medidor,
// ranuras). El pozo plano se distingue por el TONO (el del lienzo), no por una
// sombra interior.
static void glassWell(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h, G::REAL r) {
    flatWell(g, x, y, w, h, r);
}

// PASTILLA: boton de accion. Los estados cambian de TONO, nunca de luz:
// reposo = superficie media; hover = un escalon mas claro; activo = ambar,
// porque el ambar sigue significando "esto suena" y no "esto es importante".
// El CUERPO de la pastilla, sin el velo de pulsacion: lo comparten los botones
// que se pintan enteros cada frame y los que tienen la base cacheada en la
// escena estatica (esos no pueden coger pressVeil: congelaria el estado).
static void glassPillBody(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                          bool hov, bool lit, G::Color litCol, bool shadow = true) {
    (void)shadow;
    flatPillBody(g, x, y, w, h, hov, lit, litCol);
}

static void glassPill(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                      bool hov, bool lit, G::Color litCol) {
    glassPillBody(g, x, y, w, h, hov, lit, litCol);
    pressVeil(g, x, y, w, h, kRPill);
}

// ============================================================================
//  LENGUAJE PLANO (v3)
//  La misma app sin cromo. Ni degradados, ni sombras, ni biseles: la
//  profundidad la da el TONO, y la unica luz de la interfaz es el acento.
//  Estas piezas son el sustituto directo de las de cristal
//     glassPanel + glassWell + glassPill   ->  flatPanel / flatWell / flatPill
//     drawKnobBody + drawKnobDetail        ->  flatKnob
//     drawMeterTrack + drawMeterFill       ->  flatMeterTrack / flatMeterFill
//  y se enchufan zona a zona: mientras un panel siga pintando con las viejas,
//  el resto no se mueve. El ambar NO cambia: es la marca del producto.
// ============================================================================
// Los tokens del lenguaje plano viven arriba, con los demas: los necesita
// glassBandAt, que se define antes de esta seccion.

// Mezcla de un color sobre otro. Evita el "tinte a ojo" repartido por el codigo.
static G::Color flatTint(G::Color base, G::Color tint, G::REAL k) {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return G::Color(255,
        (BYTE)(base.GetR() + ((int)tint.GetR() - base.GetR())*k + 0.5f),
        (BYTE)(base.GetG() + ((int)tint.GetG() - base.GetG())*k + 0.5f),
        (BYTE)(base.GetB() + ((int)tint.GetB() - base.GetB())*k + 0.5f));
}

// SUPERFICIE PLANA: un tono y un filete. Sin sombra, sin luz de canto.
static void flatPanel(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                      G::REAL r, bool border = true) {
    G::SolidBrush bg(kFSurf);
    if (border) { G::Pen bp(kFLine, 1.0f); roundRect(g, x, y, w, h, r, &bp, &bg); }
    else        { roundRect(g, x, y, w, h, r, nullptr, &bg); }
}

// SUPERFICIE HUNDIDA: el pozo del sistema plano (visualizador, pistas, listas).
static void flatWell(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h, G::REAL r) {
    G::SolidBrush bg(kFBg);
    G::Pen bp(kFLine, 1.0f);
    roundRect(g, x, y, w, h, r, &bp, &bg);
}

// PASTILLA PLANA: los estados cambian de tono, nunca de luz.
static void flatPillBody(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                         bool hov, bool lit, G::Color litCol) {
    const G::REAL r = kRPill;
    const G::Color fill  = lit ? flatTint(kFSurf3, litCol, 0.30f) : (hov ? kFSurf3 : kFSurf2);
    const G::Color line  = lit ? litCol : kFLine;
    G::SolidBrush bg(fill);
    G::Pen bp(line, 1.0f);
    roundRect(g, x, y, w, h, r, &bp, &bg);
    if (lit) {   // piloto: un punto, no un halo
        G::SolidBrush led(G::Color(255, 0xff, 0xdd, 0x9a));
        g.FillEllipse(&led, x + 9.0f, y + h*0.5f - 2.0f, 4.0f, 4.0f);
    }
}

static void flatPill(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                     bool hov, bool lit, G::Color litCol) {
    flatPillBody(g, x, y, w, h, hov, lit, litCol);
    pressVeil(g, x, y, w, h, kRPill);
}

// PASTILLA DE LA LINEA DE ESTADO: el estado de audio lleva cuerpo propio (no es
// un campo con rotulo y valor: es el dato que hay que encontrar de un vistazo
// cuando no suena nada) y su piloto solo se enciende si HAY sesion. Devuelve el
// ancho que ocupa, para que la linea siga avanzando por medida y no por numeros.
static G::REAL statPill(G::Graphics& g, G::REAL x, G::REAL py, const WCHAR* word,
                        G::Color col, bool lit, G::Font& f) {
    G::RectF mb;
    WCHAR full[96];
    swprintf_s(full, L"AUDIO %s", word);
    g.MeasureString(full, -1, &f, G::RectF(0, 0, 900, 24), nullptr, &mb);
    const G::REAL pw = mb.Width + 34.0f;      // piloto + 9px de aire por lado
    G::SolidBrush pf(lit ? flatTint(kFSurf2, kAcc, 0.16f) : kFSurf2);
    G::Pen pl(col, 1.0f);
    roundRect(g, x, py - 3.0f, pw, 22.0f, 11.0f, &pl, &pf);
    G::SolidBrush led(lit ? G::Color(255, 0xff, 0xdd, 0x9a) : G::Color(255, 0x3a, 0x38, 0x3e));
    g.FillEllipse(&led, x + 9.0f, py + 8.0f, 4.0f, 4.0f);
    G::SolidBrush lbl(kFTxt3);
    g.DrawString(L"AUDIO ", -1, &f, G::RectF(x + 18.0f, py, 900.0f, 22), nullptr, &lbl);
    g.MeasureString(L"AUDIO ", -1, &f, G::RectF(0, 0, 900, 24), nullptr, &mb);
    G::SolidBrush bst(col);
    g.DrawString(word, -1, &f, G::RectF(x + 18.0f + mb.Width, py, 900.0f, 22), nullptr, &bst);
    return pw;
}

// MANDO PLANO: disco, arco de valor y puntero. Ni aro cromado ni domo.
//  v01 en 0..1; el barrido es de 270 grados empezando a las siete y media.
static void flatKnob(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, float v01,
                     G::Color accent, bool hov, bool active) {
    if (v01 < 0.0f) v01 = 0.0f;
    if (v01 > 1.0f) v01 = 1.0f;
    const G::REAL d = r*2.0f;
    const G::REAL arcIn = 3.5f;                       // el arco vive dentro del disco
    const G::REAL ax = cx - r + arcIn, ay = cy - r + arcIn, ad = d - arcIn*2.0f;
    {   // pista completa: se ve el recorrido que queda, no solo el consumido
        G::Pen tr(kFSurf3, 3.0f);
        g.DrawArc(&tr, ax, ay, ad, ad, 135.0f, 270.0f);
    }
    {   // arco de valor
        G::Pen vp(accent, 3.0f);
        g.DrawArc(&vp, ax, ay, ad, ad, 135.0f, 270.0f*v01);
    }
    {   // disco
        G::SolidBrush bg(kFSurf2);
        g.FillEllipse(&bg, cx - r + 4.0f, cy - r + 4.0f, d - 8.0f, d - 8.0f);
        G::Pen bp(hov || active ? accent : kFLine, 1.0f);
        g.DrawEllipse(&bp, cx - r + 4.0f, cy - r + 4.0f, d - 8.0f, d - 8.0f);
    }
    {   // puntero: una capsula, no una aguja
        const G::REAL ang = (135.0f + 270.0f*v01)*(G::REAL)3.14159265f/180.0f;
        const G::REAL px = cx + (r - 8.0f)*(G::REAL)cos(ang);
        const G::REAL py = cy + (r - 8.0f)*(G::REAL)sin(ang);
        G::Pen pp(kFTxt, 2.4f);
        g.DrawLine(&pp, cx, cy, px, py);
    }
}

// MEDIDOR PLANO: pista de 5px y relleno continuo. Sin marco ni tornillo.
static void flatMeterTrack(G::Graphics& g, G::Font& fLbl, const WCHAR* label,
                           G::REAL x, G::REAL y, G::REAL w) {
    label = tr(label);
    flatWell(g, x, y + 11.0f, w, 5.0f, 2.5f);
    // Etiqueta ARRIBA y a la izquierda de la pista: el pie pasa a ser una tira
    // de canales (rotulos en UNA linea, controles debajo) en vez de etiquetas
    // sueltas a distinta altura. Tono medio: el rotulo no compite con el valor,
    // que es lo unico que se mueve.
    // La caja va en LA MISMA linea que el resto de rotulos del pie (BOT_LBL_Y,
    // que es BOT_MTR_Y menos 25) y con el alto de BOT_LBL_H. Antes era y-29 con
    // 14 px: cuatro pixeles mas arriba que los demas y mas corta que la linea de
    // su fuente, asi que el pintado le recortaba la fila de abajo de las letras.
    G::SolidBrush lb(kFTxt3);
    g.DrawString(label, -1, &fLbl,
                 G::RectF(x, (G::REAL)(y - (BOT_MTR_Y - BOT_LBL_Y)), 140.0f,
                          (G::REAL)BOT_LBL_H),
                 nullptr, &lb);
}

static void flatMeterFill(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w,
                          float v, float hold) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (v <= 0.0001f) return;
    const G::REAL h = 5.0f, yy = y + 11.0f;
    // tres tramos en un solo pincel: marfil, ambar, techo
    G::LinearGradientBrush gr(G::RectF(x, yy, w, h), kFTxt2, kFHot, 0.0f);
    G::Color cols[3] = { kFTxt2, kAcc, kFHot };
    G::REAL  pos[3]  = { 0.0f, 0.70f, 0.92f };
    gr.SetInterpolationColors(cols, pos, 3);
    const G::REAL ww = w*v;
    // recorte: se rellena solo hasta ww, para no pasarse de la pista
    G::Region old; g.GetClip(&old);
    g.SetClip(G::RectF(x, yy, ww, h));
    g.FillRectangle(&gr, x, yy, ww, h);
    g.SetClip(&old);
    if (hold > 0.002f) {                                 // aguja de retencion de pico
        G::Pen hp(G::Color(255, 0xff, 0xe0, 0xa0), 1.6f);
        g.DrawLine(&hp, x + w*hold, yy, x + w*hold, yy + h);
    }
}

// UNIDAD: el contenedor de todo lo que se apila en la torre (estantes y
// cabezal). Una placa de cristal y una regla que separa su banda de titulo; los
// tornillos del rack se han ido con el lenguaje viejo, porque el titulo ya se
// separa solo y cuatro cabezas metalicas eran el ruido que hacia leer la torre
// como un mueble en lugar de como una consola.
static void rackUnit(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h) {
    glassPanel(g, x, y, w, h, kRPanel, true);
    G::Pen rl(G::Color(26, 0xff, 0xff, 0xff), 1.0f);
    g.DrawLine(&rl, x + 14.0f, y + (G::REAL)UNIT_HDR_H + 1.0f,
                    x + w - 14.0f, y + (G::REAL)UNIT_HDR_H + 1.0f);
}

// BOTON de la cabecera: pastilla plana con piloto. Ya no lleva gloss, bisel ni
// luz de canto -las tres marcas de "plastico pulido" del lenguaje viejo-: el
// estado lo dice el TONO (reposo/hover/activo) y el piloto.
static void topBtn(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                   const WCHAR* label, G::Font& f, bool hov, bool lit, G::Color litCol,
                   bool strong = false) {
    label = tr(label);
    if (strong && !lit && !hov) {
        // El PRIMARIO descansa un tono por encima del panel: es toda la jerarquia
        // de los tres botones de cabecera (antes los tres eran el mismo cuerpo y
        // el mismo tono de rotulo, asi que ninguno se leia primero).
        G::SolidBrush sb(kFSurf3);
        G::Pen sp(kFLine, 1.0f);
        roundRect(g, x, y, w, h, kRPill, &sp, &sb);
    } else {
        flatPillBody(g, x, y, w, h, hov, lit, litCol);
    }
    G::StringFormat sfB;
    sfB.SetAlignment(G::StringAlignmentCenter);
    sfB.SetLineAlignment(G::StringAlignmentCenter);
    sfB.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoClip);
    G::SolidBrush tb(lit || hov || strong ? kFTxt : kFTxt2);
    g.DrawString(label, -1, &f, G::RectF(x, y, w, h), &sfB, &tb);
    pressVeil(g, x, y, w, h, kRPill);
}

// Cuna (chevron) de la cadena de senal: vectorial, para no depender de fuentes
static void chevron(G::Graphics& g, G::REAL x, G::REAL y, G::REAL h, G::Color c) {
    G::Pen p(c, 1.6f);
    g.DrawLine(&p, x, y, x + h*0.40f, y + h*0.5f);
    g.DrawLine(&p, x + h*0.40f, y + h*0.5f, x, y + h);
}

// --- Familia de iconos ------------------------------------------------------
//  UN trazo (1,5), UNA caja (r=7, el ojo de un glifo de 16px) y el mismo remate
//  redondo. Los glifos de fuente (Segoe UI Symbol) traen su propio peso, su
//  propio ojo y hasta su propio descentrado -habia que corregirlo midiendo-, y
//  mantenian la interfaz a medio camino. Todo icono de la app sale de aqui.
static void icChev(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL w, G::Color c, int dir) {
    // dir: 0 abajo, 1 izquierda, 2 derecha. "w" es el ancho de la caja.
    G::Pen p(c, 1.5f);
    p.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    const G::REAL s = w*0.5f;
    G::PointF a, m, b;
    if (dir == 0) {
        a = G::PointF(cx - s*0.85f, cy - s*0.42f);
        m = G::PointF(cx,            cy + s*0.52f);
        b = G::PointF(cx + s*0.85f, cy - s*0.42f);
    } else if (dir == 1) {
        a = G::PointF(cx + s*0.42f, cy - s*0.85f);
        m = G::PointF(cx - s*0.52f, cy);
        b = G::PointF(cx + s*0.42f, cy + s*0.85f);
    } else {
        a = G::PointF(cx - s*0.42f, cy - s*0.85f);
        m = G::PointF(cx + s*0.52f, cy);
        b = G::PointF(cx - s*0.42f, cy + s*0.85f);
    }
    g.DrawLine(&p, a.X, a.Y, m.X, m.Y);
    g.DrawLine(&p, m.X, m.Y, b.X, b.Y);
}

// Flecha circular (RECARGAR): ~300 grados de circulo con el hueco arriba y una
// punta en el extremo, apuntando en el sentido de giro.
static void icRefresh(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c) {
    G::Pen p(c, 1.5f);
    p.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawArc(&p, cx - r, cy - r, r*2.0f, r*2.0f, 300.0f, 300.0f);
    const G::REAL a  = 240.0f*(G::REAL)M_PI/180.0f;      // extremo del arco
    const G::REAL ex = cx + r*std::cos(a), ey = cy + r*std::sin(a);
    const G::REAL tx = -std::sin(a), ty = std::cos(a);    // tangente (sentido de giro)
    const G::REAL nx = std::cos(a),  ny = std::sin(a);    // radio
    const G::REAL k = r*0.62f;
    G::PointF tri[3] = {
        { ex + tx*k,            ey + ty*k },
        { ex + nx*k*0.85f,      ey + ny*k*0.85f },
        { ex - nx*k*0.85f,      ey - ny*k*0.85f }
    };
    G::SolidBrush b(c);
    g.FillPolygon(&b, tri, 3);
}

static void icStar(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c, bool filled) {
    // Cinco puntas con tabla fija (sin trigonometria): 10 vertices alternando
    // radio r y r*0,42, empezando arriba.
    static const G::REAL ux[10] = {  0.000f,  0.247f,  0.951f,  0.399f,  0.588f,
                                     0.000f, -0.588f, -0.399f, -0.951f, -0.247f };
    static const G::REAL uy[10] = { -1.000f, -0.340f, -0.309f,  0.130f,  0.809f,
                                     0.420f,  0.809f,  0.130f, -0.309f, -0.340f };
    G::PointF pt[10];
    for (int i = 0; i < 10; ++i) pt[i] = G::PointF(cx + ux[i]*r, cy + uy[i]*r);
    if (filled) {
        G::SolidBrush b(c);
        g.FillPolygon(&b, pt, 10);
    } else {
        G::Pen p(c, 1.5f);
        p.SetLineJoin(G::LineJoinRound);
        g.DrawPolygon(&p, pt, 10);
    }
}

static void icHeart(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c, bool filled) {
    // Dos lobulos de radio R sobre la linea y=cy-centro y una V hasta la punta.
    const G::REAL R = r*0.52f, cyc = cy - R*0.20f;
    G::GraphicsPath hp;
    hp.AddArc(cx - 2.0f*R, cyc - R, 2.0f*R, 2.0f*R, 180.0f, 180.0f);    // lobulo izq
    hp.AddArc(cx,          cyc - R, 2.0f*R, 2.0f*R, 180.0f, -180.0f);   // lobulo dcho
    hp.AddLine(cx + 2.0f*R, cyc, cx, cy + 1.60f*R);
    hp.AddLine(cx, cy + 1.60f*R, cx - 2.0f*R, cyc);
    hp.CloseFigure();
    if (filled) {
        G::SolidBrush b(c);
        g.FillPath(&b, &hp);
    } else {
        G::Pen p(c, 1.5f);
        p.SetLineJoin(G::LineJoinRound);
        g.DrawPath(&p, &hp);
    }
}

static void icGear(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c) {
    // Dientes CORTOS y gruesos que nacen del aro, y el buje al centro: si los
    // dientes son largos y finos el icono se lee como un sol, no como un
    // engranaje.
    static const G::REAL dx[8] = { 1.000f, 0.707f, 0.000f, -0.707f, -1.000f, -0.707f, 0.000f, 0.707f };
    static const G::REAL dy[8] = { 0.000f, 0.707f, 1.000f,  0.707f,  0.000f, -0.707f, -1.000f, -0.707f };
    G::Pen pt(c, 2.6f);
    pt.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    for (int i = 0; i < 8; ++i)
        g.DrawLine(&pt, cx + dx[i]*r*0.62f, cy + dy[i]*r*0.62f,
                        cx + dx[i]*r*0.90f, cy + dy[i]*r*0.90f);
    G::Pen pr(c, 1.7f);
    g.DrawEllipse(&pr, cx - r*0.56f, cy - r*0.56f, r*1.12f, r*1.12f);
    G::SolidBrush b(c);
    g.FillEllipse(&b, cx - r*0.15f, cy - r*0.15f, r*0.30f, r*0.30f);
}

static void icPencil(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c) {
    // Silueta: cuerpo diagonal + punta. Eje de punta (abajo-izq) a culata
    // (arriba-dcha); la perpendicular es (0,707, 0,707).
    const G::REAL k = 0.707f, w = r*0.33f;
    const G::REAL tx = cx - r*0.80f, ty = cy + r*0.80f;
    const G::REAL bx = cx + r*0.78f, by = cy - r*0.78f;
    const G::REAL sx = tx + (bx - tx)*0.34f, sy = ty + (by - ty)*0.34f;
    G::PointF body[4] = { G::PointF(sx + k*w, sy + k*w), G::PointF(bx + k*w, by + k*w),
                          G::PointF(bx - k*w, by - k*w), G::PointF(sx - k*w, sy - k*w) };
    G::PointF tip[3]  = { G::PointF(tx, ty), G::PointF(sx + k*w, sy + k*w),
                          G::PointF(sx - k*w, sy - k*w) };
    G::SolidBrush b(c);
    g.FillPolygon(&b, body, 4);
    g.FillPolygon(&b, tip, 3);
}

static void icClose(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c) {
    G::Pen p(c, 1.5f);
    p.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawLine(&p, cx - r*0.62f, cy - r*0.62f, cx + r*0.62f, cy + r*0.62f);
    g.DrawLine(&p, cx + r*0.62f, cy - r*0.62f, cx - r*0.62f, cy + r*0.62f);
}

static void icPlus(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, G::Color c) {
    // El "+" de ANADIR: el opuesto exacto del aspa del pedal (uno suma una etapa,
    // el otro la quita) y el UNICO icono que se dibuja mayor que la caja de 16px:
    // es la diana de un zocalo vacio, no un mando. Para que siga siendo el mismo
    // trazo de la familia a ese tamano, el grosor escala con el radio (1,5 a r=7,
    // que es la medida de la caja): con el 1,5 fijo un "+" de 22px se lee como un
    // alambre y deja de parecer de la familia.
    G::Pen p(c, 1.5f*(r/7.0f));
    p.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawLine(&p, cx - r*0.62f, cy, cx + r*0.62f, cy);
    g.DrawLine(&p, cx, cy - r*0.62f, cx, cy + r*0.62f);
}

// Separador horizontal: una linea de 1px. El desvanecido en los extremos era
// una luz decorativa del lenguaje viejo (y ademas costaba dos pinceles).
static void hairSep(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, BYTE a) {
    G::Pen p(G::Color(a, 0xff, 0xff, 0xff), 1.0f);
    g.DrawLine(&p, x, y, x + w, y);
}

// (hash21 se fue con el tolex: no queda ninguna textura de grano en la app)

// Chasis del amp: UNA placa con su filete. Se van los pies de goma, la textura
// de tolex, la luz cenital, la sombra inferior, el marco biselado y los cuatro
// tornillos: era material del lenguaje viejo, y el mueble lo dibuja el ARTE del
// frontal, que no se toca. La placa solo tiene que sostener el frontal.
static void drawAmpBox(G::Graphics& g, G::REAL bx, G::REAL by, G::REAL bw, G::REAL bh) {
    G::SolidBrush body(kFSurf2);
    G::Pen bp(kFLine, 1.0f);
    roundRect(g, bx, by, bw, bh, 10, &bp, &body);
}

// Cuerpo del knob (estatico: cacheado en la escena). El tinte se conserva en la
// firma porque los sitios que llaman lo pasan, pero el cuerpo del mando ya es un
// disco liso: el color lo pone el arco de valor (drawKnobDetail).
static void drawKnobBody(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r,
                         G::Color tint = G::Color(0xff, 0x6e, 0x69, 0x63)) {
    // Disco plano: un tono y un filete. Ni aro cromado, ni domo, ni tapa
    // fresada: el mando del sistema plano es un disco, y el valor lo dice el
    // arco de drawKnobDetail. `tint` se conserva en la firma (los sitios que
    // llaman lo pasan) pero el lenguaje plano no tine el cuerpo del mando.
    (void)tint;
    G::SolidBrush bg(kFSurf2);
    G::Pen bp(kFLine, 1.0f);
    g.FillEllipse(&bg, cx - r, cy - r, r*2, r*2);
    g.DrawEllipse(&bp, cx - r, cy - r, r*2, r*2);
}

// Detalle del knob (dinamico): aro de escala, arco de valor, puntero, borde de estado
static void drawKnobDetail(G::Graphics& g, G::REAL cx, G::REAL cy, G::REAL r, float v01,
                           G::Color accent, bool hover, bool active) {
    // Aro de escala: pista tenue de 270 grados + arco de valor en el acento del
    // amp/pedal. Antes eran 11 ticks de 1.7px a plena saturacion mas un arco de
    // 2.8px: a radios pequenos se leia como una estrella de picos y ensuciaba
    // la perilla. Ahora la referencia es sutil y el valor manda.
    // En las perillas mini de los pedales (r=13) el aro y las marcas a
    // distancia fija quedaban desproporcionados: el grupo medÃ­a 41px dentro de
    // una tarjeta de 90 y chocaba con la caja del icono y con las etiquetas.
    // A radios pequenos se recorta el aro, se quitan las marcas y se afinan
    // los trazos, asi que la perilla se lee compacta y no invade nada.
    const bool mini = (r < 18.0f);   // (no usar 'small': es macro de rpcndr.h)
    const G::REAL ar = r + (mini ? 2.6f : 3.4f);    // radio del aro de valor
    {
        G::Pen track(kFSurf3, mini ? 2.4f : 3.0f);
        track.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
        g.DrawArc(&track, cx - ar, cy - ar, ar*2, ar*2, 135.0f, 270.0f);
        if (v01 > 0.002f) {
            G::REAL sweep = 270.0f*v01;
            if (sweep < 9.0f) sweep = 9.0f;  // el minimo sigue leyendose como arco
            // Sin halo: el arco es una linea de valor, no un tubo de neon. Es el
            // unico sitio del mando donde el acento aparece, y es porque el mando
            // ES lo que suena (o lo que se esta ajustando).
            G::Pen arc(accent, mini ? 2.4f : 3.0f);
            arc.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
            g.DrawArc(&arc, cx - ar, cy - ar, ar*2, ar*2, 135.0f, sweep);
        }
    }
    // Marcas de escala: diminutas y fuera del aro (referencia, no ruido)
    if (!mini) {
        G::Pen tick(G::Color(46, 0xe6, 0xe3, 0xdc), 1.0f);
        G::Pen tickOn(G::Color(140, accent.GetR(), accent.GetG(), accent.GetB()), 1.2f);
        for (int i = 0; i <= 10; ++i) {
            float t = i/10.0f;
            G::REAL ang = (-135.0f + 270.0f*t)*(float)M_PI/180.0f;
            G::REAL s = std::sin(ang), c = std::cos(ang);
            G::REAL r0 = r + 6.2f, r1 = r + 7.6f;
            G::Pen* p = (t <= v01 + 0.001f) ? &tickOn : &tick;
            g.DrawLine(p, cx + s*r0, cy - c*r0, cx + s*r1, cy - c*r1);
        }
    }
    // borde de estado: solo el acento cuando el mando esta ACTIVO (ajustandose).
    // El hover no se marca con un aro neutro: el mando ya tiene el arco.
    if (active) {
        G::Pen bp(accent, 1.5f);
        g.DrawEllipse(&bp, cx - r, cy - r, r*2, r*2);
    }
    // puntero (capsula clara, sin sombra proyectada)
    G::REAL ang = (-135.0f + 270.0f*v01)*(float)M_PI/180.0f;
    G::REAL s = std::sin(ang), c = std::cos(ang);
    G::Pen pp(kFTxt, 2.6f);
    pp.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawLine(&pp, cx + s*r*0.20f, cy - c*r*0.20f, cx + s*r*0.78f, cy - c*r*0.78f);
}

// Glifo vectorial de un efecto, por familia. Los simbolos Unicode abstractos
// que habia antes (â—Œ â•± â‰€ âœ± ...) no describian nada: se veian como un caracter
// suelto y daban aspecto de maqueta. Ahora cada familia tiene su dibujo:
// dinamica = nivel, saturacion = onda recortada, modulacion = onda, tiempo =
// ecos que decaen, EQ = tres bandas.
static void drawFxGlyph(G::Graphics& g, int type, G::REAL cx, G::REAL cy, G::REAL w, G::Color c) {
    G::Pen p(c, 1.6f);
    p.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    const G::REAL h = w*0.56f;
    int fam = 2;                                   // por defecto: modulacion
    switch (type) {
        case 0: case 1: case 16:                  fam = 0; break;
        case 2: case 10: case 11: case 12:        fam = 1; break;
        case 4: case 5: case 14: case 15:         fam = 3; break;
        case 9:                                   fam = 4; break;
        default: break;
    }
    switch (fam) {
        case 0: {   // nivel: barra de senal sobre una linea de umbral
            G::SolidBrush b(c);
            roundRect(g, cx - w*0.5f, cy - h*0.5f, w*0.62f, h*0.34f, h*0.17f, nullptr, &b);
            g.DrawLine(&p, cx - w*0.5f, cy + h*0.5f, cx + w*0.5f, cy + h*0.5f);
            break;
        }
        case 1: case 2: {   // onda (recortada en la saturacion)
            const int n = 25;
            G::PointF pts[25];
            for (int i = 0; i < n; ++i) {
                const G::REAL t = (G::REAL)i/(G::REAL)(n - 1);
                // dos ciclos completos: empieza y acaba en cero, si no el trazo
                // cortado en mitad de un semiciclo parecia una letra suelta
                G::REAL v = (G::REAL)std::sin((double)(t*2.0f*(G::REAL)M_PI*2.0f));
                if (fam == 1) { v *= 2.1f; if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f; }
                pts[i].X = cx - w*0.5f + t*w;
                pts[i].Y = cy - v*h*0.5f;
            }
            g.DrawLines(&p, pts, n);
            break;
        }
        case 3: {   // ecos que decaen
            for (int i = 0; i < 4; ++i) {
                const G::REAL bx = cx - w*0.5f + (G::REAL)i*(w/3.0f);
                const G::REAL bh = h*(1.0f - (G::REAL)i*0.22f);
                g.DrawLine(&p, bx, cy + h*0.5f, bx, cy + h*0.5f - bh);
            }
            break;
        }
        default: {  // EQ: tres bandas
            G::SolidBrush b(c);
            for (int i = 0; i < 3; ++i) {
                const G::REAL bx = cx - w*0.5f + (G::REAL)i*(w*0.5f);
                const G::REAL vy = cy + h*0.5f - h*(0.75f - (G::REAL)i*0.22f);
                g.DrawLine(&p, bx, cy - h*0.5f, bx, cy + h*0.5f);
                g.FillEllipse(&b, bx - 2.0f, vy - 2.0f, 4.0f, 4.0f);
            }
            break;
        }
    }
}

// Canal del medidor (estatico): etiqueta en versalitas + ranura hundida con
// escala. Ya no es una caja con marcas cruzadas: es una ranura de instrumento.
static void drawMeterTrack(G::Graphics& g, G::Font& fLbl, const WCHAR* label,
                           G::REAL x, G::REAL y, G::REAL w) {
    flatMeterTrack(g, fLbl, label, x, y, w);
}

// Relleno del medidor (dinamico): la rampa marfil -> ambar -> techo en un solo
// pincel. Se conserva porque es INFORMACION (donde empieza a recortar), no
// adorno: es el unico degradado que el lenguaje plano mantiene a proposito.
static void drawMeterFill(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, float v, float hold) {
    flatMeterFill(g, x, y, w, v, hold);
}

// --- Lectura de valor (pop): UNA receta para el mando y para el slider MEZCLA.
// Antes cada uno traia su fondo (uno azulado, otro neutro), su radio y sus
// margenes, y la del mando no limitaba arriba: con los mandos del cabezal la
// burbuja se salia de la ventana. Hueco con filete del acento, cifra ambar
// (la cifra activa) y caja SIEMPRE dentro de la ventana.
static void drawValuePop(G::Graphics& g, G::REAL bx, G::REAL by, G::REAL bw,
                         G::REAL bh, const WCHAR* txt, G::Color ac) {
    if (bx < 12.0f) bx = 12.0f;
    if (bx > (G::REAL)WIN_W - 12.0f - bw) bx = (G::REAL)WIN_W - 12.0f - bw;
    if (by < 62.0f) by = 62.0f;
    if (by > (G::REAL)WIN_H - 12.0f - bh) by = (G::REAL)WIN_H - 12.0f - bh;
    G::SolidBrush vb(kWellA);
    G::Pen vp(G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()), 1.5f);
    roundRect(g, bx, by, bw, bh, kRWell, &vp, &vb);
    G::SolidBrush vt(G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()));
    G::Font fVal(kFontTec, kTRotulo, G::FontStyleBold);
    G::StringFormat sfC;
    sfC.SetAlignment(G::StringAlignmentCenter);
    sfC.SetLineAlignment(G::StringAlignmentCenter);
    g.DrawString(txt, -1, &fVal, G::RectF(bx, by, bw, bh), &sfC, &vt);
}

// Vista frontal de la cabina: mueble, baffle hundido, tela, conos y micro.
// Se conserva toda la geometria (el radio de los conos derivado de la caja, el
// diametro en pulgadas y la posicion del micro), que es lo que hace que una
// 1x12, una 2x12 y una 4x12 se lean distintas. El material vuelve en la MEDIDA
// justa: tejido de la tela, luz de arriba y conos leidos a traves del tejido,
// porque la cabina es la firma del producto y como caja plana se leia vacia.
static void drawCabPreview(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                           int spk, int inches, int mic, int dist) {
    // Vista frontal del mueble, legible como objeto: cara superior con su
    // filete, cuerpo de tolex con canto claro, cuatro esquineras, baffle
    // enmarcado, conos DETRAS de la tela (no fantasmas) y micro sobre pie.
    const G::REAL legH = 11.0f;      // patas de goma
    const G::REAL topD = 11.0f;      // profundidad de la cara superior
    const G::REAL bx = x, by = y + topD, bw = w, bh = h - topD - legH;

    // (las patas de goma eran decoracion: el mueble se apoya en su filete)
    // --- cara superior: el grosor del mueble visto desde delante ---
    {
        const G::REAL ins = 7.0f;
        G::PointF tp[4] = {
            G::PointF(bx, by), G::PointF(bx + bw, by),
            G::PointF(bx + bw - ins, y), G::PointF(bx + ins, y),
        };
        G::SolidBrush tg(kFSurf3);
        g.FillPolygon(&tg, tp, 4);
        G::Pen thk(kFLine, 1.0f);
        g.DrawLine(&thk, tp[3].X + 5.0f, y + 0.5f, tp[2].X - 5.0f, y + 0.5f);
        // canto de luz de la tapa: la unica luz de la escena viene de arriba
        G::Pen thl(G::Color(70, 0xff, 0xff, 0xff), 1.0f);
        g.DrawLine(&thl, tp[3].X + 8.0f, y + 1.0f, tp[2].X - 8.0f, y + 1.0f);
        G::Pen tfr(G::Color(255, 0x14, 0x14, 0x17), 1.2f);
        g.DrawLine(&tfr, bx, by - 0.5f, bx + bw, by - 0.5f);
    }
    // --- cuerpo de tolex: un escalon por encima del panel, canto claro arriba ---
    {
        G::SolidBrush body(kFSurf2);
        G::Pen ed(kFLine, 1.0f);
        roundRect(g, bx, by, bw, bh, 7.0f, &ed, &body);
        G::Pen hl(G::Color(56, 0x3a, 0x3a, 0x42), 1.0f);
        g.DrawLine(&hl, bx + 9.0f, by + 1.2f, bx + bw - 9.0f, by + 1.2f);
    }
    // --- esquineras metalicas ---
    {
        G::SolidBrush mcb(G::Color(255, 0x3a, 0x3a, 0x42));
        G::Pen mcp(G::Color(255, 0x4a, 0x4a, 0x54), 1.0f);
        const G::REAL kc[4][2] = { { bx + 2.0f, by + 2.0f }, { bx + bw - 15.0f, by + 2.0f },
                                   { bx + 2.0f, by + bh - 15.0f }, { bx + bw - 15.0f, by + bh - 15.0f } };
        for (int i = 0; i < 4; ++i) {
            roundRect(g, kc[i][0], kc[i][1], 13.0f, 13.0f, 3.0f, &mcp, &mcb);
            // el canto de la esquina: sombra por dentro y filo de luz arriba
            G::Pen mci(G::Color(96, 0x0b, 0x0b, 0x0e), 1.0f);
            roundRect(g, kc[i][0] + 2.5f, kc[i][1] + 2.5f, 8.0f, 8.0f, 2.0f, &mci, nullptr);
            G::Pen mcl(G::Color(64, 0xff, 0xff, 0xff), 1.0f);
            g.DrawLine(&mcl, kc[i][0] + 3.0f, kc[i][1] + 1.4f, kc[i][0] + 10.0f, kc[i][1] + 1.4f);
        }
    }
    // --- baffle enmarcado ---
    const G::REAL fx = bx + 13.0f, fy = by + 13.0f, fw = bw - 26.0f, fh = bh - 26.0f;
    {
        G::SolidBrush fb(G::Color(255, 0x0c, 0x0c, 0x0f));
        G::Pen fp(G::Color(0xff, 0x38, 0x38, 0x40), 1.6f);
        roundRect(g, fx, fy, fw, fh, 5.0f, &fp, &fb);
        G::Pen fin(G::Color(120, 0, 0, 0), 1.0f);
        roundRect(g, fx + 1.5f, fy + 1.5f, fw - 3.0f, fh - 3.0f, 4.0f, &fin, nullptr);
        // hundido: sombra en el canto de arriba, filo de luz en el de abajo
        G::Pen bsh(G::Color(150, 0, 0, 0), 2.0f);
        g.DrawLine(&bsh, fx + 4.0f, fy + 2.0f, fx + fw - 4.0f, fy + 2.0f);
        G::Pen bli(G::Color(60, 0x3c, 0x3c, 0x44), 1.0f);
        g.DrawLine(&bli, fx + 4.0f, fy + fh - 1.5f, fx + fw - 4.0f, fy + fh - 1.5f);
    }
    // --- conos: se dibujan ANTES de la tela, con contraste suficiente para
    //     leerse a traves de ella (antes quedaban como manchas) ---
    {
        // Radio DERIVADO de la geometria, no de una fraccion de la altura: con
        // dos conos el radio salia 81px para centros separados 113px, asi que los
        // altavoces se solapaban y una 2x12 parecia dos altavoces gigantes.
        G::REAL spx[4], spy[4], spr = 0.0f;
        int n = spk;
        // El diametro del altavoz entra en el radio: un 2x10 lleva conos mas
        // pequenos que un 2x12 (nunca mayores: un 1x15 ya llena el baffle).
        const G::REAL inchF = (inches > 0 && inches < 12) ? (G::REAL)inches/12.0f : 1.0f;
        if (n == 1) {                       // 1x12 / 1x15: un cono centrado
            spx[0] = fx + fw*0.5f; spy[0] = fy + fh*0.5f;
            spr = (fw*0.34f < fh*0.40f) ? fw*0.34f : fh*0.40f;
        } else if (n == 2) {                // 2x12: dos conos al 45% del ancho
            spx[0] = fx + fw*0.27f; spy[0] = fy + fh*0.5f;
            spx[1] = fx + fw*0.73f; spy[1] = fy + fh*0.5f;
            spr = (fw*0.225f < fh*0.40f) ? fw*0.225f : fh*0.40f;
        } else {                            // 4x12: dos por dos
            n = 4;
            spx[0] = fx + fw*0.28f; spy[0] = fy + fh*0.30f;
            spx[1] = fx + fw*0.72f; spy[1] = fy + fh*0.30f;
            spx[2] = fx + fw*0.28f; spy[2] = fy + fh*0.70f;
            spx[3] = fx + fw*0.72f; spy[3] = fy + fh*0.70f;
            spr = (fw*0.20f < fh*0.20f) ? fw*0.20f : fh*0.20f;
        }
        // El diametro entra de VERDAD en el radio: hasta aqui inchF se calculaba y
        // nadie lo usaba, asi que un 2x10 dibujaba los MISMOS conos que un 2x12 y
        // el comentario de arriba prometia algo que el codigo no cumplia. Con el
        // factor el 2x10 (10") queda un 17% mas pequeno, y el 4x12 de dos pulgadas
        // sigue tocandose en vertical igual que antes (inchF=1).
        spr *= inchF;
        for (int i = 0; i < n; ++i) {
            // Cono: aro, garganta, campana y polvo. La tela de encima lo vela,
            // asi que el dibujo tiene que dar el contraste que el velo quita.
            G::SolidBrush ring(G::Color(255, 0x3c, 0x3c, 0x44));
            g.FillEllipse(&ring, spx[i] - spr, spy[i] - spr, spr*2.0f, spr*2.0f);
            G::Pen rp(G::Color(255, 0x6a, 0x6a, 0x76), 1.2f);
            g.DrawEllipse(&rp, spx[i] - spr, spy[i] - spr, spr*2.0f, spr*2.0f);
            // garganta: el surco entre el aro y la campana
            G::Pen gap(G::Color(230, 0x07, 0x07, 0x09), 3.2f);
            g.DrawEllipse(&gap, spx[i] - spr*0.82f, spy[i] - spr*0.82f, spr*1.64f, spr*1.64f);
            G::SolidBrush cone(G::Color(255, 0x2a, 0x2a, 0x31));
            g.FillEllipse(&cone, spx[i] - spr*0.80f, spy[i] - spr*0.80f, spr*1.60f, spr*1.60f);
            G::Pen cg(G::Color(255, 0x1d, 0x1d, 0x22), 1.0f);
            g.DrawEllipse(&cg, spx[i] - spr*0.80f, spy[i] - spr*0.80f, spr*1.60f, spr*1.60f);
            // polvo: el centro, un escalon mas claro y con su filo de luz
            G::SolidBrush cap(G::Color(255, 0x4a, 0x4a, 0x54));
            g.FillEllipse(&cap, spx[i] - spr*0.30f, spy[i] - spr*0.30f, spr*0.60f, spr*0.60f);
            G::Pen cpn(G::Color(255, 0x14, 0x14, 0x18), 1.0f);
            g.DrawEllipse(&cpn, spx[i] - spr*0.30f, spy[i] - spr*0.30f, spr*0.60f, spr*0.60f);
            G::Pen cpl(G::Color(90, 0xff, 0xff, 0xff), 1.0f);
            g.DrawArc(&cpl, spx[i] - spr*0.30f, spy[i] - spr*0.30f, spr*0.60f, spr*0.60f,
                      190.0f, 90.0f);
        }
    }
    // --- tela: base + tejido tenue + luz de arriba (sin velo pesado) ---
    {
        G::GraphicsPath cp;
        const G::REAL d = 10.0f;
        cp.AddArc(fx, fy, d, d, 180, 90);
        cp.AddArc(fx + fw - d, fy, d, d, 270, 90);
        cp.AddArc(fx + fw - d, fy + fh - d, d, d, 0, 90);
        cp.AddArc(fx, fy + fh - d, d, d, 90, 90);
        cp.CloseFigure();
        g.SetClip(&cp, G::CombineModeReplace);
        // Tela: vela los conos sin borrarlos, y lleva su propio tejido. Un tono
        // plano dejaba la cabina como una caja negra; el tejido de 3px es lo que
        // la hace leer como rejilla a tamaÃ±o real.
        G::SolidBrush cloth(G::Color(160, kFBg.GetR(), kFBg.GetG(), kFBg.GetB()));
        g.FillRectangle(&cloth, fx, fy, fw, fh);
        G::Pen wv(G::Color(9, 0x7a, 0x74, 0x68), 1.0f);
        for (G::REAL gx = fx + 2.0f; gx < fx + fw - 2.0f; gx += 3.0f)
            g.DrawLine(&wv, gx, fy + 2.0f, gx, fy + fh - 2.0f);
        G::Pen wh(G::Color(6, 0x7a, 0x74, 0x68), 1.0f);
        for (G::REAL gy = fy + 2.0f; gy < fy + fh - 2.0f; gy += 3.0f)
            g.DrawLine(&wh, fx + 2.0f, gy, fx + fw - 2.0f, gy);
        // Luz de arriba: la tela coge la misma luz que la tapa.
        G::LinearGradientBrush lg(G::RectF(fx, fy, fw, fh*0.55f),
                                  G::Color(10, 0xff, 0xee, 0xd0), G::Color(0, 0xff, 0xee, 0xd0),
                                  G::LinearGradientModeVertical);
        g.FillRectangle(&lg, fx, fy, fw, fh*0.55f);
        // Trama por encima de TODO: las mismas hebras cruzan los conos. Sin
        // esto los altavoces leian como discos PEGADOS sobre la tela en vez de
        // detras de ella, que es lo que son.
        G::Pen tv2(G::Color(15, 0x9a, 0x93, 0x84), 1.0f);
        for (G::REAL gx = fx + 2.0f; gx < fx + fw - 2.0f; gx += 6.0f)
            g.DrawLine(&tv2, gx, fy + 2.0f, gx, fy + fh - 2.0f);
        G::Pen th2(G::Color(10, 0x9a, 0x93, 0x84), 1.0f);
        for (G::REAL gy = fy + 2.0f; gy < fy + fh - 2.0f; gy += 6.0f)
            g.DrawLine(&th2, fx + 2.0f, gy, fx + fw - 2.0f, gy);
        g.ResetClip();
    }
    // --- micro sobre pie con brazo ---
    {
        const G::REAL mx = fx + fw*(0.50f + (G::REAL)dist*0.15f);
        const G::REAL my = fy + fh*0.56f;
        // Un micro delante de un cono se pierde en el cono: halo oscuro primero,
        // que le da contorno, y despues el cuerpo de acero.
        {
            G::SolidBrush halo(G::Color(210, 0x05, 0x05, 0x07));
            roundRect(g, mx - 9.5f, my - 29.5f, 19.0f, 33.0f, 8.0f, nullptr, &halo);
        }
        // brazo: entra desde el borde derecho del mueble. El pie de un micro
        // delante de la caja no se ve desde esta vista, asi que no se inventa
        // una base flotando en la tela.
        const G::REAL ax = bx + bw - 6.0f, ay = my + 26.0f;
        G::Pen pipe(G::Color(255, 0x4a, 0x4a, 0x55), 2.4f);
        g.DrawLine(&pipe, mx + 7.0f, my + 8.0f, ax, ay);
        G::SolidBrush knob(G::Color(255, 0x5a, 0x5a, 0x66));
        g.FillEllipse(&knob, ax - 3.0f, ay - 3.0f, 6.0f, 6.0f);
        // pinza
        G::SolidBrush clip(G::Color(255, 0x3a, 0x3a, 0x43));
        roundRect(g, mx - 5.0f, my, 14.0f, 6.0f, 2.0f, nullptr, &clip);
        // capsula: cuerpo de acero con rejilla oscura
        G::SolidBrush mbody(G::Color(255, 0x62, 0x62, 0x6e));
        roundRect(g, mx - 7.0f, my - 27.0f, 14.0f, 28.0f, 6.0f, nullptr, &mbody);
        G::Pen mpen(G::Color(255, 0x8e, 0x8e, 0x9a), 1.2f);
        roundRect(g, mx - 7.0f, my - 27.0f, 14.0f, 28.0f, 6.0f, &mpen, nullptr);
        G::Pen gril(G::Color(255, 0x12, 0x12, 0x16), 1.0f);
        for (int i = 0; i < 5; ++i)
            g.DrawLine(&gril, mx - 4.5f, my - 23.0f + i*3.4f, mx + 4.5f, my - 23.0f + i*3.4f);
        // canto de luz: el micro se separa de la tela
        G::Pen mhl(G::Color(120, 0xff, 0xff, 0xff), 1.0f);
        g.DrawLine(&mhl, mx - 5.4f, my - 24.0f, mx - 5.4f, my - 4.0f);
    }
}

// Fila de valor con FLECHAS: el control dice en que direccion cambia. Un pill
// que solo cicla esconde las otras opciones y obliga a probar a ciegas; con â€¹ â€º
// se ve de que lado queda cada valor y se puede volver. Las dos mitades son la
// misma zona (el clic decide el sentido) y la flecha bajo el raton se enciende
// en ambar: es lo unico vivo de la fila.
static void drawStepPill(G::Graphics& g, int id,
                         G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                         const WCHAR* label, const WCHAR* value, bool live) {
    label = tr(label); value = tr(value);
    const bool hov = zoneHit(id);
    const bool rightSide = hov && g_mouseX >= (int)(x + w*0.5f);
    const bool leftSide  = hov && !rightSide;
    flatPillBody(g, x, y, w, h, hov, false, kAcc);
    const G::REAL cy = y + h*0.5f;
    {
        G::Pen al(leftSide ? kAcc : kFTxt2, 2.0f);
        al.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
        g.DrawLine(&al, x + 14.0f, cy - 4.2f, x + 10.0f, cy);
        g.DrawLine(&al, x + 10.0f, cy, x + 14.0f, cy + 4.2f);
    }
    {
        G::Pen ar(rightSide ? kAcc : kFTxt2, 2.0f);
        ar.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
        g.DrawLine(&ar, x + w - 14.0f, cy - 4.2f, x + w - 10.0f, cy);
        g.DrawLine(&ar, x + w - 10.0f, cy, x + w - 14.0f, cy + 4.2f);
    }
    G::Font fLbl(kFontTec, kTPico, G::FontStyleBold);
    G::Font fVal(kFontTec, kTRotulo, G::FontStyleBold);
    G::StringFormat sf;
    sf.SetLineAlignment(G::StringAlignmentCenter);
    sf.SetFormatFlags(G::StringFormatFlagsNoWrap);
    sf.SetTrimming(G::StringTrimmingEllipsisCharacter);
    G::SolidBrush bl(kFTxt3);
    G::SolidBrush bv(live ? kAcc : kFTxt);
    g.DrawString(label, -1, &fLbl, G::RectF(x + 20.0f, y, 36.0f, h), &sf, &bl);
    g.DrawString(value, -1, &fVal, G::RectF(x + 50.0f, y, w - 70.0f, h), &sf, &bv);
}

// --- Piezas dinamicas de una tarjeta de pedal -------------------------------
// La plantilla cacheada trae el cuerpo, el glifo, el nombre y la pista del
// switch; aqui van las que cambian de estado (perilla del switch y perillas de
// los parametros). El fantasma de arrastre pinta exactamente las mismas.
// (aqui vivia mixSliderFrac, que nadie llamaba: el recorrido de la MEZCLA se
//  calculaba a mano en los dos sitios del raton. Ahora la formula es UNA y vive
//  junto a sus constantes, arriba.)

// Mando APARCADO: el hueco serigrafiado del frontal que no controla ningun
// parametro. Se lee como un mando del ampli (el arte lo pide) pero apagado y
// sin puntero: no invita a arrastrarlo porque no hay nada que mover. Aparcado y
// mudo es honesto; vivo y mudo es una mentira.
// HOY NO SE LLAMA: no existe ningun hueco sin modelo (el autotest los cuenta como
// fallo y da cero), asi que la funcion esta reservada para cuando aparezca uno.
// No es codigo muerto que se pueda borrar sin mas: es la regla de diseno hecha
// funcion.
static void drawParkedKnob(G::Graphics& g, G::Image* strip, G::REAL cx, G::REAL cy, G::REAL r) {
    if (strip) {
        // el filmstrip solo sabe dibujar en su tamano de hueco: se pinta el
        // fotograma central y encima un velo que lo apaga
        drawArtKnob(g, strip, cx, cy, r, 0.5f, G::Color(255, 0x88, 0x88, 0x88), false, false);
        G::SolidBrush veil(G::Color(120, 0x0a, 0x0a, 0x0c));
        g.FillEllipse(&veil, cx - r*0.92f, cy - r*0.92f, r*1.84f, r*1.84f);
    } else {
        G::SolidBrush b(G::Color(255, 0x1a, 0x1a, 0x1e));
        g.FillEllipse(&b, cx - r*0.8f, cy - r*0.8f, r*1.6f, r*1.6f);
    }
}

// Interruptor del modulo: el mando corre de izquierda (apagado) a derecha
// (encendido) DENTRO de la pista que trae la plantilla. Encendido se enciende
// el mando en ambar y el LED de estado del modulo.
static void drawPedalSwitchKnob(G::Graphics& g, int t, G::REAL sx, G::REAL sy) {
    const G::Color ac = colFx(t);
    const bool on = g_params.fxOn[t].load();
    const G::REAL tx = sx + PED_TRK_X, ty = sy + PED_TRK_Y, tw = PED_TRK_W, th = PED_TRK_H;
    // LED de estado, a la izquierda del nombre
    {
        // Sin resplandor detras del LED: el lenguaje plano enciende un punto.
        const G::Color lit(on ? G::Color(255,
            (BYTE)(ac.GetR() + 50 > 255 ? 255 : ac.GetR() + 50),
            (BYTE)(ac.GetG() + 50 > 255 ? 255 : ac.GetG() + 50),
            (BYTE)(ac.GetB() + 50 > 255 ? 255 : ac.GetB() + 50)) : G::Color(255, 0x2c, 0x2c, 0x31));
        G::SolidBrush led(lit);
        g.FillEllipse(&led, sx + 9.6f, sy + 16.6f, 4.8f, 4.8f);
    }
    // mando: 12x10 que viaja por la pista
    const G::REAL kx = on ? (tx + tw - 14.0f) : (tx + 2.0f);
    const G::REAL ky = ty + 2.0f;
    G::SolidBrush kb(on ? ac : G::Color(255, 0x76, 0x72, 0x6a));
    roundRect(g, kx, ky, 12.0f, 10.0f, 3.5f, nullptr, &kb);
}
// Valor normalizado de una perilla de pedal (el WAH va de -80 a -20)
static float pedalKnobV01(int t, int k) {
    const float vmin = (t == 0 && k == 0) ? -80.0f :   0.0f;
    const float vmax = (t == 0 && k == 0) ? -20.0f : 100.0f;
    float v = (g_params.fx[t][k].load() - vmin)/(vmax - vmin);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

// --- Plantillas de slot de pedal: una cacheada por tipo ---
// Cada plantilla (124x120, la tarjeta con 6px de margen) se renderiza UNA vez; por
// frame se dibuja el slot + los estados dinamicos encima.
//  Se dibuja con GDI+ (DrawImage) y NO con BitBlt: la ventana es redimensionable y
// todo el pintado va en coordenadas de diseno bajo una transformacion de GDI+, que
// un volcado de GDI se salta. Con BitBlt la tarjeta se quedaba a 1:1 y sin desplazar
// mientras el switch, el aspa de quitar y las perillas -que si van por GDI+-
// escalaban con el resto: al ampliar la ventana los pedales salian desencajados de
// sus propios mandos. El fallo no se veia a 1:1, donde la transformacion es la
// identidad, y por eso vivio escondido hasta que se miro una ventana ampliada.
struct SlotTmpl {
    HDC dc = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr;
    void* bits = nullptr; bool valid = false;
    G::Bitmap* gp = nullptr;      // vista GDI+ de esos mismos bits: es lo que se pinta
};
static SlotTmpl g_slotTmpl[18];

// --- Fantasma de arrastre: la copia translucida de la plantilla ---
// El fantasma lleva la tarjeta ENTERA (cuerpo, switch y las dos perillas) y va al
// 75% de alfa. Antes se volcaba con AlphaBlend, que se salta la transformacion
// igual que el BitBlt. Aqui se prepara UNA copia con el alfa ya multiplicado -que es
// lo que pide PixelFormat32bppPARGB y lo que hacia AC_SRC_ALPHA- y se dibuja con
// DrawImage: escala y se coloca con la misma transformacion que el resto. Se
// construye al empezar el arrastre y se reutiliza mientras dure.
static HBITMAP    g_ghostDib  = nullptr;
static void*      g_ghostBits = nullptr;
static G::Bitmap* g_ghostBmp  = nullptr;
static int        g_ghostType = -1;
static const int  kGhostAlpha = 190;   // el mismo valor que usaba AlphaBlend

static void freeGhostCard() {
    if (g_ghostBmp) { delete g_ghostBmp; g_ghostBmp = nullptr; }
    if (g_ghostDib) { DeleteObject(g_ghostDib); g_ghostDib = nullptr; }
    g_ghostBits = nullptr;
    g_ghostType = -1;
}
static void buildGhostCard(int t) {
    freeGhostCard();
    if (t < 0 || t >= 18 || !g_slotTmpl[t].bits) return;
    HDC scr = GetDC(nullptr);
    g_ghostDib = createSurfaceDib(scr, PED_TMPL_W, PED_TMPL_H, &g_ghostBits);
    ReleaseDC(nullptr, scr);
    if (!g_ghostDib || !g_ghostBits) { freeGhostCard(); return; }
    const BYTE* src = (const BYTE*)g_slotTmpl[t].bits;
    BYTE* dst = (BYTE*)g_ghostBits;
    const int n = PED_TMPL_W*PED_TMPL_H;
    for (int i = 0; i < n; ++i) {
        dst[i*4 + 0] = (BYTE)(src[i*4 + 0]*kGhostAlpha/255);
        dst[i*4 + 1] = (BYTE)(src[i*4 + 1]*kGhostAlpha/255);
        dst[i*4 + 2] = (BYTE)(src[i*4 + 2]*kGhostAlpha/255);
        dst[i*4 + 3] = (BYTE)kGhostAlpha;
    }
    g_ghostBmp = new G::Bitmap(PED_TMPL_W, PED_TMPL_H, PED_TMPL_W*4,
                               PixelFormat32bppPARGB, (BYTE*)g_ghostBits);
    g_ghostType = t;
}

static void freeSlotTemplates() {
    freeGhostCard();
    for (int t = 0; t < 18; ++t) {
        // GDI+ primero: nunca puede quedar un Bitmap vivo sobre los bits de un DIB
        // ya borrado.
        if (g_slotTmpl[t].gp) { delete g_slotTmpl[t].gp; g_slotTmpl[t].gp = nullptr; }
        if (g_slotTmpl[t].dc) {
            if (g_slotTmpl[t].old) SelectObject(g_slotTmpl[t].dc, g_slotTmpl[t].old);
            if (g_slotTmpl[t].bmp) DeleteObject(g_slotTmpl[t].bmp);
            DeleteDC(g_slotTmpl[t].dc);
        }
        g_slotTmpl[t] = SlotTmpl();
    }
}
static void buildSlotTemplates() {
    freeSlotTemplates();
    HDC scr = GetDC(nullptr);
    const G::REAL OX = (G::REAL)PED_TMPL_M, OY = (G::REAL)PED_TMPL_M;
    for (int t = 0; t < 18; ++t) {
        g_slotTmpl[t].dc = CreateCompatibleDC(scr);
        g_slotTmpl[t].bmp = createSurfaceDib(scr, PED_TMPL_W, PED_TMPL_H, &g_slotTmpl[t].bits);
        if (!g_slotTmpl[t].dc || !g_slotTmpl[t].bmp) continue;
        g_slotTmpl[t].old = SelectObject(g_slotTmpl[t].dc, g_slotTmpl[t].bmp);
        G::Graphics g(g_slotTmpl[t].dc);
        g.SetSmoothingMode(G::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
        const G::REAL x = OX, y = OY;
        const G::REAL w = (G::REAL)PED_SLOT_W, h = (G::REAL)PED_SLOT_H;
        const G::Color ac = colFx(t);

        // FONDO DE LA PLANTILLA = el tono del estante en la franja del modulo.
        //  La plantilla se cachea con 6px de margen y se blitea ENTERA sobre el
        //  estante: con el fondo en negro (el DIB nace a cero) cada pedal
        //  aparecia con un marco opaco que rompia el cristal de la unidad.
        {
            // El estante plano es UN tono: ya no hay banda que reproducir por
            // altura (antes se interpolaba el cristal del estante en la franja
            // que ocupa el modulo).
            G::SolidBrush bgFill(glassBandAt(0.0f));
            g.FillRectangle(&bgFill, 0.0f, 0.0f, (G::REAL)PED_TMPL_W, (G::REAL)PED_TMPL_H);
        }

        // MODULO: placa de cristal con su cinta de tipo. Todo lo que no sea
        // informacion (biseles de adorno, tornillos) se ha retirado: el color
        // del tipo va en la cinta superior y en el LED de estado, y nada mas.
        glassPanel(g, x, y, w, h, kRCard, false);
        {   // cinta del tipo: 2px pegados al canto superior
            G::SolidBrush rb(ac);
            roundRect(g, x + 8.0f, y + 2.0f, w - 16.0f, 2.0f, 1.0f, nullptr, &rb);
        }
        // nombre del tipo, medido para que quepa siempre
        {
            WCHAR wname[64];
            MultiByteToWideChar(CP_ACP, 0, kFxNames[t], -1, wname, 64);
            drawTxFit(g, wname, G::FontStyleBold, kFTxt,
                          x + 17.0f, y + 13.0f, 0.5f, w - 24.0f, TxPico);
        }
        // filete entre lo que el pedal ES y lo que se le toca: la tarjeta tiene
        // dos partes y el filete las separa sin meter un recuadro dentro
        {
            G::Pen hs(kFLine, 1.0f);
            g.DrawLine(&hs, x + 14.0f, y + 33.0f, x + w - 14.0f, y + 33.0f);
        }
        // cuerpo de las dos perillas (puntero: capa viva)
        drawKnobBody(g, x + PED_KB1_X, y + PED_KB_Y, PED_KB_R);
        drawKnobBody(g, x + PED_KB2_X, y + PED_KB_Y, PED_KB_R);
        // etiquetas cortas, centradas bajo cada perilla
        {
            WCHAR wl[16];
            MultiByteToWideChar(CP_ACP, 0, kFxShort[t][0], -1, wl, 16);
            drawTxFitC(g, wl, G::FontStyleRegular, kFTxt3,
                           x + PED_KB1_X, y + 76.0f, 0.3f, 34.0f, TxMicro);
            MultiByteToWideChar(CP_ACP, 0, kFxShort[t][1], -1, wl, 16);
            drawTxFitC(g, wl, G::FontStyleRegular, kFTxt3,
                           x + PED_KB2_X, y + 76.0f, 0.3f, 34.0f, TxMicro);
        }
        // pista del bypass: un interruptor de pedal, hundido en la chapa
        {
            G::SolidBrush tr(kWellA);
            G::Pen trp(kWellLn, 1.0f);
            roundRect(g, x + PED_TRK_X, y + PED_TRK_Y, PED_TRK_W, PED_TRK_H, 7.0f, &trp, &tr);
        }
        surfaceDibOpaque(g_slotTmpl[t].bits, PED_TMPL_W, PED_TMPL_H);
        // Vista GDI+ de los mismos bits: es lo que se pinta por frame (ver el
        // comentario del struct). Sin transparencia: el fondo de la plantilla es el
        // tono del estante, asi que cubre su rectangulo entero.
        g_slotTmpl[t].gp = new G::Bitmap(PED_TMPL_W, PED_TMPL_H, PED_TMPL_W*4,
                                         PixelFormat32bppPARGB, (BYTE*)g_slotTmpl[t].bits);
        g_slotTmpl[t].valid = true;
    }
    ReleaseDC(nullptr, scr);
}

// ---------------------------------------------------------------------------
//  Movimiento: una sola tabla de rampas, sin asignar nada por fotograma.
//  Todo se cuelga del timer de 16 ms que ya existia y de la compuerta de
//  invalidacion: cuando no hay ninguna rampa viva la app vuelve a costar 0% en
//  reposo, que es como estaba. Las rampas no son adorno: avisan de que algo
//  acaba de cambiar, asi que son cortas (80-140 ms) y desaparecen solas.
// ---------------------------------------------------------------------------
static const float kRampHover = 80.0f;    // ms: el raton entra en una fila
static const float kRampModal = 140.0f;   // ms: un modal se abre o se cierra

struct UAnim {
    float row[kMaxNavRows];   // rampa de hover de cada ranura de la lista
    float modal;        // 0..1: cuanto esta puesto el velo del modal
    float pulse;        // fase del latido de la cadena (radianes)
};
static UAnim g_anim;

static void animStep(float& v, float target, float ms) {
    if (ms < 1.0f) ms = 1.0f;
    const float d = (target - v)*(16.0f/ms);
    if (d > -0.004f && d < 0.004f) { v = target; return; }
    v += d;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
}
// Una rampa esta viva mientras no haya llegado a su destino.
static bool animActive() {
    if (g_anim.modal != 0.0f && g_anim.modal != 1.0f) return true;
    for (int i = 0; i < 64; ++i) {
        if (g_anim.row[i] != 0.0f && g_anim.row[i] != 1.0f) return true;
    }
    return false;
}
// Latido de la cadena: solo con audio. En silencio no late nada, porque el
// pulso significa "esto esta pasando ahora" y en silencio no pasa nada.
static float animPulse() {
    if (!g_params.audioActive.load()) return 0.0f;
    return 0.5f + 0.5f*(float)sin((double)g_anim.pulse);
}

// ---------------------------------------------------------------------------
//  Escena estatica cacheada + pintado principal (estilo GUITAR ONE)
// ---------------------------------------------------------------------------
static void buildStaticScene(int W, int H) {
    freeStaticScene();
    if (W <= 0 || H <= 0) return;
    HDC scr = GetDC(nullptr);
    g_scene.dc = CreateCompatibleDC(scr);
    g_scene.bmp = createSurfaceDib(scr, W, H);
    ReleaseDC(nullptr, scr);
    if (!g_scene.dc || !g_scene.bmp) { freeStaticScene(); return; }
    g_scene.old = SelectObject(g_scene.dc, g_scene.bmp);
    g_scene.w = W; g_scene.h = H;

    G::Graphics g(g_scene.dc);
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
    // Toda la superficie en color de lienzo ANTES de escalar: lo que sobra de
    // lienzo (escala perfecta en un eje) queda en el fondo, no en basura.
    { G::SolidBrush bgk(kBack); g.FillRectangle(&bgk, 0.0f, 0.0f, (G::REAL)W, (G::REAL)H); }
    uiApplyTransform(g, W, H);

    G::StringFormat sfC;
    sfC.SetAlignment(G::StringAlignmentCenter);
    sfC.SetLineAlignment(G::StringAlignmentCenter);

    G::Font fLogo(kFontTec, kTMarca, G::FontStyleBold);
    G::Font fH2(kFontTec, kTMarca, G::FontStyleBold);
    G::Font fBold(kFontUI, kTCuerpo, G::FontStyleBold);

    // ---- ENTORNO: el fondo es una sala de rack, no un escenario con focos ----
    //  Un tono plano y UNA lavada de luz de techo: sin charcos de luz, sin
    //  relleno de color, sin vineta. La escena la construyen las unidades (su
    //  sombra de contacto y su material), no el fondo: cuando el fondo hace
    //  de foco, todo lo que va encima parece una calcomania.
    {
        G::SolidBrush bg(kFBg);
        g.FillRectangle(&bg, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
    }

    // ---- CABECERA: una banda plana. La separacion la da el TONO, no la luz. ---
    {
        G::SolidBrush hb(kFSurf);
        g.FillRectangle(&hb, 0.0f, 0.0f, (G::REAL)WIN_W, 72.0f);
        G::Pen rule(kFLine, 1.0f);
        g.DrawLine(&rule, 0.0f, 71.5f, (G::REAL)WIN_W, 71.5f);
    }
    // ---- MARCA: wordmark en marfil, con el cuadro ambar de la casa ----------
    //  Fuera el metal degradado y el subrayado: la marca es una palabra y una
    //  marca de color. Cuanto menos hace el logotipo, mas caro se lee.
    {
        // Lockup: marca cuadrada + wordmark + linea de producto. El subtitulo
        // baja y se separa del wordmark (antes le tocaba la linea base).
        // MARCA: el logotipo metalico (recurso RCDATA). El logo YA lleva la
        // pua, asi que el cuadro ambar y el wordmark tipografico desaparecen;
        // si el recurso faltara se pintan los dos como antes.
        float subY = 47.0f;
        if (G::Bitmap* lg = brandLogo()) {
            const G::REAL lh = 46.0f;
            const G::REAL lw = lh * (G::REAL)lg->GetWidth() / (G::REAL)lg->GetHeight();
            const G::InterpolationMode im0 = g.GetInterpolationMode();
            g.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
            g.DrawImage(lg, G::RectF(22.0f, 6.0f, lw, lh), 0.0f, 0.0f,
                        (G::REAL)lg->GetWidth(), (G::REAL)lg->GetHeight(), G::UnitPixel);
            g.SetInterpolationMode(im0);
            subY = 53.0f;
        } else {
            G::SolidBrush mk(kAcc);
            roundRect(g, 24.0f, 25.0f, 11.0f, 11.0f, 2.0f, nullptr, &mk);
            G::SolidBrush mkb(kFTxt);
            drawSpacedB(g, L"GUITAR ONE", fLogo, &mkb, 44, 10, 4.0f);
        }
        G::SolidBrush eb(kFTxt3);
        G::Font fEd(kFontTec, kTPico, G::FontStyleBold);
        g.DrawString(tr(L"SIMULADOR DE AMPLIFICADORES"), -1, &fEd,
                     G::RectF(22.0f, subY, 320.0f, 11.0f), nullptr, &eb);
    }

    // Base de la caja de preset superior (texto y hover son dinamicos).
    // Es una PASTILLA, no un hueco: el sitio donde se lee que suena es una pieza
    // de cristal con su luz, y solo el cuerpo del display se hunde.
    {
        glassPanel(g, (G::REAL)TOP_BOX_X, (G::REAL)TOP_BOX_Y, (G::REAL)TOP_BOX_W,
                   (G::REAL)TOP_BOX_H, kRPill, true);
        // separador antes de las flechas (a 2px de la flecha, que empieza en 358):
        // UN valor para reposo y hover. Antes reposo=322 y hover=356, asi que el
        // divisor saltaba 34px al pasar el raton y la caja del preset (hasta 870)
        // rebasaba el reposo en 28px.
        G::Pen dv(G::Color(46, 0xff, 0xff, 0xff), 1.0f);
        g.DrawLine(&dv, (G::REAL)TOP_BOX_X + TOP_DIV_X, (G::REAL)(TOP_BOX_Y + 12),
                       (G::REAL)TOP_BOX_X + TOP_DIV_X, (G::REAL)(TOP_BOX_Y + TOP_BOX_H - 12));
    }

    // --- Panel izquierdo: busqueda + lista de amplificadores ---
    {
        glassPanel(g, (G::REAL)COL_L_X, (G::REAL)PANEL_Y, (G::REAL)COL_L_W,
                   (G::REAL)PANEL_H, kRPanel);
        // El separador va DEBAJO del buscador: con la banda de pestanas retirada
        // no hay pestana que separar, pero el filete sigue haciendo el mismo
        // trabajo (cerrar el buscador antes de la lista).
        hairSep(g, (G::REAL)(COL_L_X + 12), (G::REAL)(SEARCH_Y + SEARCH_H + 4), (G::REAL)(COL_L_W - 24), 40);
        // La estrella del filtro: ambar cuando filtra (esta PULSADA es la unica
        // lectura que admite el lenguaje) y contorno tenue en reposo.
        {
            const RECT sr = searchStarRect();
            icStar(g, (G::REAL)(sr.left + sr.right)/2.0f, (G::REAL)(sr.top + sr.bottom)/2.0f,
                   7.5f, g_favOnly ? kAcc : kFTxt3, g_favOnly);
        }
        // Leyenda de atajos: el hueco del pie estaba RESERVADO (LIST_H deja 36px y
        // HINT_Y apuntaba a el) pero nada lo pintaba. Los atajos existen y son
        // reales, asi que la leyenda cierra el panel como promete el diseno.
        {
            // Los cuatro atajos, de mas usado a menos: presets (flechas), la
            // lista de la izquierda, el audio y salir. Antes era una linea de
            // 7,5 pt en una columna de 320 que se leia "ESPACIO audio <-/->
            // preset ..." sin saber que significaba cada flecha ni en que
            // orden mirarlos. Ahora cada uno nombra lo que hace.
            drawTxFitC(g, L"\x2190\x2192 presets · \x2191\x2193 lista · ESPACIO audio · ESC salir",
                           G::FontStyleRegular, kFTxt3,
                           (G::REAL)(COL_L_X + COL_L_W/2), (G::REAL)HINT_Y, 0.3f,
                           (G::REAL)(COL_L_W - 24), TxCuerpo);
        }
        // El buscador escribe en vivo: el hueco va en la escena estatica y el
        // texto/foco se pintan en la capa dinamica (antes el texto tipeado no
        // se veia hasta cambiar de amp, porque vivia en la escena cacheada).
        glassWell(g, (G::REAL)SEARCH_X, (G::REAL)SEARCH_Y, (G::REAL)SEARCH_W, (G::REAL)SEARCH_H, kRWell);
    }

    // (retirado) Aqui se pintaban unas tabs AMP|CAB del escenario, pero caian
    // dentro del rectangulo del panel del amp y el fondo del panel las tapaba:
    // eran UI muerta que ademas se solapaba con el titulo del escenario.

    // --- TORRE DE SENAL: estante ANTES / cabezal / estante DESPUES ----------
    //  Tres unidades montadas una debajo de otra. El NOMBRE de cada unidad es
    //  serigrafia fija (aqui, en la escena estatica); el recuento de la derecha
    //  vive en la capa viva porque cambia con el preset.
    //
    //  El nombre del ampli NO va en esta banda: la banda dice que ESTANTE es.
    //  Escribir el nombre del ampli encima dejaba dos textos pisados en el mismo
    //  renglon (el nombre a 15pt sobre la serigrafia de la unidad) y un filete
    //  vertical cruzado: eso era el texto ilegible del estante ANTES.
    {
        const int m = g_params.model.load();
        G::Color ac = colAmp(m);

        rackUnit(g, (G::REAL)RACK_X, (G::REAL)PRE_SHELF_Y, (G::REAL)RACK_W, (G::REAL)PED_SHELF_H);
        rackUnit(g, (G::REAL)RACK_X, (G::REAL)AMP_UNIT_Y,  (G::REAL)RACK_W, (G::REAL)AMP_UNIT_H);
        rackUnit(g, (G::REAL)RACK_X, (G::REAL)POST_SHELF_Y, (G::REAL)RACK_W, (G::REAL)PED_SHELF_H);

        // --- Serigrafia de cada unidad: piloto + nombre ---
        {
            const int bandY[3] = { PRE_SHELF_Y, AMP_UNIT_Y, POST_SHELF_Y };
            const WCHAR* bandT[3] = { L"ANTES DEL AMPLIFICADOR", L"CABEZAL",
                                      L"DESPU\xC9S DEL AMPLIFICADOR" };
            for (int i = 0; i < 3; ++i) {
                const G::REAL by2 = (G::REAL)bandY[i] + 6.0f;
                // piloto de la etapa: solo lleva color de ampli el cabezal, que
                // es el unico tramo vivo del recorrido (regla del unico vivo)
                G::SolidBrush pd(i == 1 ? ac : kFTxt3);
                g.FillEllipse(&pd, (G::REAL)(RACK_X + 15), by2 + 4.5f, 7.0f, 7.0f);
                // El titulo manda: 11,5 pt frente a los 10,5 de antes, y la unidad
                // viva va en marfil mientras las otras dos se quedan en el gris de
                // cuerpo. La jerarquia la lleva el tamano y el valor, no un color.
                drawTxFit(g, bandT[i], G::FontStyleBold, i == 1 ? kFTxt : kFTxt2,
                              (G::REAL)(RACK_X + 30), by2, 1.2f, 330.0f, TxTitulo);
            }
        }

        // --- Ranuras de cada estante: las 6 plazas del rack, dibujadas ---
        //  Con dos modulos montados el estante quedaba medio vacio sin decir
        //  nada; ahora se ven las plazas libres, que es lo que hace un rack.
        for (int row = 0; row < 2; ++row) {
            // Solo se dibujan las plazas ocupadas mas la libre: seis chapas
            // vacias eran la mayor zona muerta de la pantalla y hacian que la
            // ranura de anadir quedase rodeada de plazas anonimas.
            const int used = (int)((row == 0) ? g_chainPre.size() : g_chainPost.size()) + 1;
            for (int i = 0; i < used && i < PED_MAX_ROW; ++i) {
                const G::REAL sx = pedSlotX(i), sy = (G::REAL)PED_ROW_Y[row];
                G::SolidBrush sb2(G::Color(255, 0x0f, 0x0f, 0x12));
                G::Pen sp2(G::Color(255, 0x21, 0x21, 0x27), 1.0f);
                roundRect(g, sx, sy, (G::REAL)PED_SLOT_W, (G::REAL)PED_SLOT_H, 6.0f, &sp2, &sb2);
                // canto alto hundido: la plaza libre lee como bahia vacia
                G::Pen sh2(G::Color(150, 0, 0, 0), 1.0f);
                g.DrawLine(&sh2, sx + 8.0f, sy + 1.0f, sx + PED_SLOT_W - 8.0f, sy + 1.0f);
                G::Pen hl2(G::Color(22, 0xff, 0xff, 0xff), 1.0f);
                g.DrawLine(&hl2, sx + 8.0f, sy + PED_SLOT_H - 1.0f,
                                 sx + PED_SLOT_W - 8.0f, sy + PED_SLOT_H - 1.0f);
            }
        }

        // --- Cabezal: el frontal real montado en su unidad ---
        if (g_artOk[m] && g_fondoImg[m]) {
            const G::REAL bx = (G::REAL)ART_X, by = (G::REAL)ART_Y;
            const G::REAL bw = (G::REAL)ART_W, bh = (G::REAL)ART_H;
            // El frontal del PERFIL mide NAM_FACE_H (177): su cara es SOLO el
            // panel de mandos, no las 324 del arte completo (media rejilla vacia
            // con la placa flotando). Todo el chrome de la cara sigue esta altura.
            const G::REAL fh = (m == 12) ? (G::REAL)NAM_FACE_H : bh;
            {   // cara superior: el grosor del mueble visto de frente.
                // Antes subia 15px sobre el arte (dy=12) e invadia la banda del
                // titulo: el canto pasaba por encima de "CABEZAL". Ahora el canto
                // vive dentro del hueco de 8px entre banda y arte (dy=4).
                const G::REAL dy = 4.0f, ins = 5.0f;
                G::PointF tp[4] = {
                    G::PointF(bx - 3.0f, by - 3.0f),
                    G::PointF(bx + bw + 3.0f, by - 3.0f),
                    G::PointF(bx + bw + 3.0f - ins, by - 3.0f - dy),
                    G::PointF(bx - 3.0f + ins, by - 3.0f - dy),
                };
                G::SolidBrush tg(kFSurf3);
                g.FillPolygon(&tg, tp, 4);
                G::Pen thk(kFLine, 1.0f);
                g.DrawLine(&thk, tp[3].X + 6.0f, tp[3].Y, tp[2].X - 6.0f, tp[2].Y);
            }
            softShadow(g, bx, by, bw, fh, 5.0f, 140);   // sombra de contacto
            {   // junta de montaje: hueco oscuro y un solo filo tibio
                G::SolidBrush bg2(G::Color(255, 0x0a, 0x0a, 0x0b));
                G::Pen bp2(G::Color(255, 0x22, 0x22, 0x28), 1.0f);
                roundRect(g, bx - 3.0f, by - 3.0f, bw + 6.0f, fh + 6.0f, 3.0f, &bp2, &bg2);
            }
            if (m == 12)
                g.DrawImage(g_fondoImg[m], G::RectF(bx, by, bw, fh),
                            0.0f, 0.0f, 1200.0f, (G::REAL)NAM_FACE_ART_H, G::UnitPixel);
            else
                g.DrawImage(g_fondoImg[m], bx, by, bw, bh);
            // --- Cadena de capturas: bloques como los de TONE3000 ------------
            if (m == 12) {
                NamChain snap;
                { std::lock_guard<std::mutex> lk(g_namMutex); snap = g_namUi; }
                // BAHIA de la cadena: la MISMA pieza que el estante de pedales
                // (hueco hundido del ancho de la torre). Antes esta fila se
                // dibujaba encima de la rejilla del frontal: cajas pegadas al
                // ampli. Ahora vive en su banda, debajo de la cara recortada.
                glassWell(g, (G::REAL)RACK_X, (G::REAL)NAM_BAY_Y,
                          (G::REAL)RACK_W, (G::REAL)NAM_BAY_H, kRWell);
                const G::REAL ry = (G::REAL)NAMROW_Y, rh = (G::REAL)NAMROW_H;
                // boton ANADIR: rotulo y leyenda, cada uno en su banda (antes se
                // tocaban: 15pt acababa en la misma fila que el pie de 11pt)
                // Los dos botones son la MISMA pieza (placa con filete, texto marfil
                // al pasar por encima): antes AÃ‘ADIR llevaba marco ambar permanente
                // y T3K un marco marfil, dos tratos distintos pegados uno al otro.
                // El ambar queda para el hover, como en el resto del sistema.
                {
                    const G::REAL ax = (G::REAL)NAM_ADD_X, aw = (G::REAL)NAM_ADD_W;
                    const bool hova = mouseIn((int)ax, (int)ry, (int)(ax + aw), (int)(ry + rh));
                    glassPillBody(g, ax, ry, aw, rh, hova, false, kAcc, true);
                    if (hova) {   // el hover de estos dos botones es lo unico ambar
                        G::Pen ap(accA(170), 1.2f);
                        roundRect(g, ax, ry, aw, rh, kRPill, &ap, nullptr);
                    }
                    drawTxFitC(g, L"+ A\xD1" L"ADIR", G::FontStyleBold,
                                   hova ? kFTxt : kFTxt2,
                                   ax + aw*0.5f, ry + (G::REAL)NAMROW_H*0.30f, 1.2f, aw - 16.0f, TxRotulo);
                    drawTxFitC(g, L"perfil .nam", G::FontStyleRegular, kFTxt3,
                                   ax + aw*0.5f, ry + (G::REAL)NAMROW_H*0.56f, 0.6f, aw - 16.0f, TxPico);
                }
                // boton TONE3000: explorar la biblioteca (marca corta T3K). Misma
                // ALTURA que el boton de al lado y que los zocalos: medÃ­a 26px en
                // una fila de 56 y desequilibraba toda la banda.
                {
                    const G::REAL tx = (G::REAL)NAM_T3K_X;
                    const G::REAL ty = ry;
                    const bool hovt = mouseIn((int)tx, (int)ty, (int)(tx + NAM_T3K_W), (int)(ty + rh));
                    glassPillBody(g, tx, ty, (G::REAL)NAM_T3K_W, rh, hovt, false, kAcc, true);
                    if (hovt) {
                        G::Pen tp(accA(170), 1.2f);
                        roundRect(g, tx, ty, (G::REAL)NAM_T3K_W, rh, kRPill, &tp, nullptr);
                    }
                    drawTxFitC(g, L"T3K", G::FontStyleBold, hovt ? kFTxt : kFTxt2,
                                   tx + (G::REAL)NAM_T3K_W*0.5f, ry + (G::REAL)NAMROW_H*0.41f, 1.4f,
                                   (G::REAL)NAM_T3K_W - 14.0f, TxRotulo);
                }
                // CELDAS: se pintan TODAS las que caben, no solo las ocupadas. Una
                // celda vacia es el "+" que se puede pulsar y donde caera lo que
                // elijas (asi se elige DONDE va cada elemento, como en TONE3000).
                // Todas las plazas, con la MISMA pieza que el slot fantasma del
                // pedalboard: filete a RAYAS y "+" grande. Lo que hacia ilegible la
                // version anterior era un contorno solido apagado (invisible sobre
                // la tela) y lo que la hacia ruidosa era repetir un rotulo en cada
                // caja; las rayas y el "+" grande, sin texto, se leen como plazas
                // libres y llenan la fila hasta el borde.
                const int vis = namBlkVis();
                for (int i = 0; i < vis; ++i) {
                    const G::REAL bx = (G::REAL)namBlkX(i);
                    if (i >= (int)snap.size()) {
                        const bool hovc = mouseIn((int)bx, (int)ry, (int)(bx + NAM_BLK_W), (int)(ry + rh));
                        // HUECO LIBRE = un zocalo vacio, con el mismo peso visual que
                        // los dos botones: cuerpo hundido, filete a rayas visible y
                        // un "+" DIBUJADO con dos trazos. Antes el "+" era un glifo
                        // de 20pt y el signo de una fuente ocupa media caja: salia
                        // como un punto y las celdas no se veian.
                        G::SolidBrush sb2(G::Color(0xff, 0x0d, 0x0d, 0x10));
                        G::Pen dp(hovc ? kAcc : G::Color(0xff, 0x4a, 0x47, 0x44), 1.6f);
                        dp.SetDashStyle(G::DashStyleDash);
                        roundRect(g, bx, ry, (G::REAL)NAM_BLK_W, rh, 6.0f, &dp, &sb2);
                        const G::REAL pcx = bx + (G::REAL)NAM_BLK_W*0.5f;
                        const G::REAL pcy = ry + rh*0.5f;
                        const G::REAL pr2 = 11.0f;
                        G::Pen pp2(hovc ? kAcc : G::Color(0xff, 0x9c, 0x98, 0x90), 2.6f);
                        g.DrawLine(&pp2, pcx - pr2, pcy, pcx + pr2, pcy);
                        g.DrawLine(&pp2, pcx, pcy - pr2, pcx, pcy + pr2);
                        pressVeil(g, bx, ry, (G::REAL)NAM_BLK_W, rh, 6.0f);
                        continue;
                    }
                    const bool sel = (g_namSel == i);
                    const bool on = snap[i]->on.load();
                    G::SolidBrush bb(sel ? G::Color(255, 0x20, 0x2a, 0x2c) : G::Color(255, 0x16, 0x16, 0x1a));
                    G::Pen bp(sel ? kAcc : G::Color(255, 0x3a, 0x3a, 0x44), sel ? 1.6f : 1.0f);
                    roundRect(g, bx, ry, (G::REAL)NAM_BLK_W, rh, 6.0f, &bp, &bb);
                    // ---- celda de captura, con el alto que ahora tiene la fila ----
                    // Antes todo cabia en 56px: LED, badge, nombre y tres controles
                    // apretados, con los simbolos como glifos diminutos que se leian
                    // como cajas vacias. Ahora: estado arriba, nombre en su banda y
                    // los tres botones abajo, dibujados a trazo (nitidos a ese tamano).
                    G::SolidBrush lb2(on ? kAcc : G::Color(255, 0x44, 0x44, 0x4a));
                    g.FillEllipse(&lb2, bx + 8.0f, ry + 9.0f, 10.0f, 10.0f);
                    // Sin badge de tipo: la celda respira. El tipo (AMP/NAM) ya lo
                    // dice la fila de chips de la pantalla del PERFIL y lo implica el
                    // nombre; aqui solo estorbanda. sm vive para la linea de datos.
                    nam::Model* sm = snap[i]->model ? snap[i]->model.get() : nullptr;
                    // nombre del fichero: una linea medida + una segunda con el
                    // material y el autor, que es lo que identifica la captura.
                    drawTxFit(g, snap[i]->name.c_str(), G::FontStyleBold,
                                  on ? G::Color(255, 0xdc, 0xd8, 0xd0) : G::Color(130, 0x9a, 0x96, 0x92),
                                  bx + 8.0f, ry + 30.0f, 0.3f, (G::REAL)NAM_BLK_W - 16.0f, TxPico);
                    if (sm) {
                        std::wstring sub;
                        if (!sm->gearMake.empty() || !sm->gearModel.empty()) {
                            sub = std::wstring(sm->gearMake.begin(), sm->gearMake.end());
                            const std::wstring gd(sm->gearModel.begin(), sm->gearModel.end());
                            if (!gd.empty()) sub += (sub.empty() ? L"" : L" ") + gd;
                        } else if (!sm->author.empty())
                            sub = std::wstring(sm->author.begin(), sm->author.end());
                        if (!sub.empty())
                            drawTxFit(g, sub.c_str(), G::FontStyleRegular, G::Color(0x9c, 0x82, 0x7d, 0x76),
                                          bx + 8.0f, ry + 45.0f, 0.2f, (G::REAL)NAM_BLK_W - 16.0f, TxPico);
                    }
                    // controles: tres botones de 28x26, simbolos a TRAZO
                    {
                        const G::REAL qw = 28.0f, qh = 26.0f, qgap = 6.0f;
                        const G::REAL q0x = bx + ((G::REAL)NAM_BLK_W - (3*qw + 2*qgap))*0.5f;
                        const G::REAL qy = ry + rh - qh - 8.0f;
                        for (int k = 0; k < 3; ++k) {
                            const G::REAL qx = q0x + (G::REAL)k*(qw + qgap);
                            const bool armado = (k == 2 && g_namDelArm == i);
                            G::SolidBrush qb(armado ? G::Color(255, 0x2a, 0x14, 0x16) : G::Color(255, 0x22, 0x22, 0x28));
                            G::Pen qp(armado ? kHot : G::Color(255, 0x3c, 0x3c, 0x46), armado ? 1.4f : 1.0f);
                            roundRect(g, qx, qy, qw, qh, 5.0f, &qp, &qb);
                            const G::REAL cxq = qx + qw*0.5f, cyq = qy + qh*0.5f;
                            G::Pen gs(armado ? kHot : G::Color(220, 0xc8, 0xc4, 0xbd), 1.8f);
                            if (k == 0) {                       // <
                                g.DrawLine(&gs, cxq + 3.0f, cyq - 5.5f, cxq - 3.0f, cyq);
                                g.DrawLine(&gs, cxq - 3.0f, cyq, cxq + 3.0f, cyq + 5.5f);
                            } else if (k == 1) {                // >
                                g.DrawLine(&gs, cxq - 3.0f, cyq - 5.5f, cxq + 3.0f, cyq);
                                g.DrawLine(&gs, cxq + 3.0f, cyq, cxq - 3.0f, cyq + 5.5f);
                            } else {                            // x
                                g.DrawLine(&gs, cxq - 4.0f, cyq - 4.0f, cxq + 4.0f, cyq + 4.0f);
                                g.DrawLine(&gs, cxq + 4.0f, cyq - 4.0f, cxq - 4.0f, cyq + 4.0f);
                            }
                        }
                    }
                }
                // La leyenda "Sin capturas: pulsa una celda vacia..." se ha quitado:
                // se pintaba ENCIMA de las celdas (se leia cortada, pisando sus "+")
                // y ya era redundante: las celdas vacias dicen donde se pulsa y los
                // botones + ANADIR / T3K dicen de donde sale la captura.
            }
            // --- PERFIL (captura NAM): pantalla + boton de carga -------------
            //  Coordenadas del arte (1200x540): pantalla en (170,88,430,102),
            //  boton impreso en (170,212,240,32). El arte trae el bisel y el
            //  rotulo; la app pone el texto y el estado del boton.
            if (m == 12) {
                // La pantalla, en las coordenadas del ARTE (medidas en
                // 13_profile_fondo.png: vidrio de x175..600, y99..198). El codigo
                // dibujaba en y=88 con alto 102: el texto salia 8px por encima del
                // vidrio, sobre el bisel.
                const G::REAL sx = bx + 170.0f*bw/1200.0f, sy = by + 96.0f*bh/540.0f;
                const G::REAL sw = 430.0f*bw/1200.0f, sh = 104.0f*bh/540.0f;
                std::shared_ptr<const NamChain> ch = std::atomic_load(&g_namLive);
                nam::Model* nm = nullptr;
                if (ch && !ch->empty()) {
                    const int si = (g_namSel >= 0 && g_namSel < (int)ch->size()) ? g_namSel : 0;
                    nm = (*ch)[si]->model.get();
                }
                const G::REAL fs1 = sh*0.22f;      // tamano del nombre (cabe en el display)
                const G::REAL fs2 = sh*0.165f;     // tamano de la linea de datos
                // El arte YA NO monta potes sobre el vidrio: se movieron a x>=680
                // del arte (amp_frontales_data.h), asi que la pantalla puede usar
                // su ancho completo. Antes el pote de GAIN caia justo en el borde
                // del video (x=600) y lo tapaba.
                const G::REAL suW = sw;
                const G::REAL suCx = sx + suW*0.5f;   // centro del vidrio
                if (nm) {
                    std::wstring wn(nm->name.begin(), nm->name.end());
                    std::wstring sub;
                    if (!nm->gearMake.empty() || !nm->gearModel.empty()) {
                        std::wstring gm(nm->gearMake.begin(), nm->gearMake.end());
                        std::wstring gd(nm->gearModel.begin(), nm->gearModel.end());
                        sub = gm + (gm.empty() || gd.empty() ? L"" : L" ") + gd;
                    }
                    if (!nm->author.empty() && sub.size() < 34)
                        sub += (sub.empty() ? L"" : L" \xB7 ") +
                               std::wstring(nm->author.begin(), nm->author.end());
                    // FILA DE CHIPS (lenguaje TONE3000): tipo del equipo + motor,
                    // medidos de izquierda a derecha; la frecuencia, a la derecha.
                    G::Font fChip(kFontTec, kTPico, G::FontStyleBold);
                    G::Pen pc(kWellLn, 1.0f);
                    const G::REAL chipY = sy + 3.0f, chipH = 12.0f;
                    G::REAL chipX = sx + 8.0f;
                    // La frecuencia que se muestra es la DEL MODELO. Puede no ser
                    // la del motor, y ya no es un aviso rojo: el motor la convierte
                    // (NamRateConv) antes de entrar en la captura.
                    {
                        std::wstring chips;
                        if (!nm->gearType.empty()) chips += std::wstring(nm->gearType.begin(), nm->gearType.end());
                        chips += (chips.empty() ? std::wstring() : std::wstring(L" ")) +
                                 (nm->a2 ? std::wstring(L"A2") : std::wstring(L"NAM"));
                        std::transform(chips.begin(), chips.end(), chips.begin(), ::towupper);
                        size_t p0 = 0;
                        while (p0 < chips.size()) {
                            const size_t pf = chips.find(L' ', p0);
                            const std::wstring tok = chips.substr(p0, pf == std::wstring::npos ? pf : pf - p0);
                            const G::REAL tw = measureSpaced(g, tok.c_str(), fChip, 0.4f) + 12.0f;
                            if (chipX + tw > sx + sw*0.62f) break;
                            G::SolidBrush cb2(G::Color(255, 0x10, 0x10, 0x12));
                            roundRect(g, chipX, chipY, tw, chipH, 6.0f, &pc, &cb2);
                            drawSpaced(g, tok.c_str(), fChip,
                                       kFTxt2,
                                       chipX + 6.0f, chipY + 1.0f, 0.4f);
                            chipX += tw + 5.0f;
                            if (pf == std::wstring::npos) break;
                            p0 = pf + 1;
                        }
                        char buf[96];
                        std::snprintf(buf, sizeof buf, "%.0f kHz", nm->sampleRate/1000.0);
                        WCHAR wb[96]; MultiByteToWideChar(CP_ACP, 0, buf, -1, wb, 96);
                        // La frecuencia vive en una caja al final del vidrio, no en
                        // la fila de chips. OJO: drawSpacedFitC CENTRA en cx, asi que
                        // hay que pasarle el centro de su caja (0.87), no el borde
                        // derecho: con el borde como centro, la mitad del "48 kHz"
                        // se salia del vidrio. maxW 0.24 <= ancho de esa caja.
                        drawTxFitC(g, wb, G::FontStyleRegular, kFTxt3,
                                       sx + suW*0.87f, sy + sh*0.66f, 0.4f, suW*0.24f,
                                       TxPico);
                    }
                    // --- Caja de texto del vidrio ---------------------------------
                    // El titulo y la linea de datos comparten fondo con el "48 kHz",
                    // que va pegado al borde derecho. Antes cada uno se encajaba a un
                    // % del ancho COMPLETO del vidrio (0.86 y 0.74): la linea de datos
                    // se montaba sobre la frecuencia ("desmondstuâ€¦ 48 kHz"). Con un %
                    // no hay forma de cuadrar, porque el texto y la frecuencia no
                    // crecen igual al cambiar de perfil.
                    //
                    // Ahora la frecuencia tiene hueco propio y fijo; lo que sobra a su
                    // izquierda es la unica caja del texto, centrada ahi. Por mucho
                    // que cambie el nombre, el texto nunca lo pisa.
                    const G::REAL srBox = suW*0.24f;      // hueco del 48 kHz
                    const G::REAL srGap = suW*0.04f;      // aire obligatorio entre ambos
                    const G::REAL txtW  = suW - srBox - srGap;
                    const G::REAL txtCx = sx + txtW*0.5f;
                    // El ptMin escala con la ventana: a resoluciones pequenas el
                    // vidrio entero se encoge, y un suelo fijo de 9 pt hacia que
                    // los nombres largos se cortasen con "â€¦" antes de poderse
                    // reducir. Con el suelo escalado cabe el nombre en el minimo.
                    //
                    // El espaciado del nombre tambien baja: con tracking 2, un
                    // nombre de 30+ caracteres -"NEW VERSION | Fender DRRI |
                    // CIRCA 1965"- media 496 pt en el vidrio de 183, de modo que
                    // se cortaba a "Fender D..." sin haber podido encogerse. Con
                    // 1.0 y suelo 7 cabe entero; el sub va a 0.6 por lo mismo.
                    const G::REAL pm1 = 7.0f * g_uiS, pm2 = 6.5f * g_uiS;
                    drawSpacedFitC(g, wn.c_str(), G::FontStyleBold, G::Color(0xe6, 0x82, 0x7d, 0x76),
                                   txtCx, sy + sh*0.30f, 1.0f, txtW, fs1, pm1);
                    if (!sub.empty())
                        drawSpacedFitC(g, sub.c_str(), G::FontStyleRegular, G::Color(0xbe, 0x82, 0x7d, 0x76),
                                       txtCx, sy + sh*0.66f, 0.6f, txtW, fs2, pm2);
                } else {
                    drawSpacedFitC(g, L"SIN PERFIL", G::FontStyleBold, G::Color(0xc8, 0x82, 0x7d, 0x76),
                                   suCx, sy + sh*0.40f, 2.0f, suW*0.92f, fs1, 9.0f, kFontTec);
                    // (las medidas de estas dos lineas salen del tamano de la
                    //  ficha del perfil, no de la escala: van con su familia.)
                    WCHAR nlib[64];
                    swprintf_s(nlib, tr(L"PULSA CARGAR PERFIL (.NAM) \xB7 %d EN TU LIBRERIA"), g_t3kLibCount);
                    drawSpacedFitC(g, nlib, G::FontStyleRegular,
                                   G::Color(0x96, 0x6e, 0x69, 0x63),
                                   suCx, sy + sh*0.74f, 0.8f, suW*0.94f, fs2, 7.5f, kFontTec);
                }
                // boton: hover y pulsacion (el rotulo es del arte). La caja sale
                // de namBtnBox, la MISMA que mira el clic.
                G::REAL bx2, by2, bw2, bh2;
                namBtnBox(bx2, by2, bw2, bh2);
                // El hover usa el raton en vivo (g_mouseX/Y). g_lastPt se declaro
                // para las filas y NADIE la escribe: estaba siempre en (-1,-1), asi
                // que el velo de hover era inalcanzable y el boton parecia muerto.
                const bool hot = mouseIn((int)bx2, (int)by2, (int)(bx2 + bw2), (int)(by2 + bh2));
                if (hot) {
                    G::SolidBrush hb(G::Color(46, 0x82, 0x7d, 0x76));
                    G::Pen hp(G::Color(0xc8, 0x82, 0x7d, 0x76), 1.2f);
                    roundRect(g, bx2, by2, bw2, bh2, 4.0f, &hp, &hb);
                }
            }

        } else {
            // Respaldo si faltase el arte (no deberia pasar: el arte va con la app)
            drawAmpBox(g, (G::REAL)ART_X, (G::REAL)ART_Y, (G::REAL)ART_W, (G::REAL)ART_H);
            drawTxFitC(g, kAmpNames[m], G::FontStyleBold, G::Color(255, 0xb8, 0xb5, 0xae),
                           (G::REAL)ART_X + (G::REAL)ART_W*0.5f, (G::REAL)ART_Y + 70.0f, 2.0f,
                           (G::REAL)ART_W - 60.0f, TxMarca);
        }
        // banda de la cadena de senal: el hueco que aloja los chips del
        // recorrido del sonido y el boton AFINADOR
        glassWell(g, (G::REAL)ART_X, (G::REAL)CHAIN_Y, (G::REAL)ART_W, (G::REAL)CHAIN_H, kRWell);
        // base del boton AFINADOR: pastilla plana. El diapason, el texto y el
        // hover/estado viven en la capa dinamica (el icono se enciende en verde
        // cuando el modal esta abierto).
        {
            G::SolidBrush bg(kFSurf2);
            G::Pen tp3(kFLine, 1.0f);
            roundRect(g, (G::REAL)TUNER_BTN_X, (G::REAL)TUNER_BTN_Y,
                      (G::REAL)TUNER_BTN_W, (G::REAL)TUNER_BTN_H, 8, &tp3, &bg);
        }
    }

    // --- Panel derecho: CABINA (preview + acciones + micro/dist + mezcla) ---
    {
        glassPanel(g, (G::REAL)COL_R_X, (G::REAL)PANEL_Y, (G::REAL)COL_R_W,
                   (G::REAL)PANEL_H, kRPanel, true);
        // tab CABINA (activo): EXACTAMENTE el mismo trato que las pestanas del
        //  navegador â€” texto + filete ambar de 64x2 centrado. Llevaba una caja de
        //  324x30 con borde ambar y un subrayado VERDE-AMARILLO (#c8f02a) de
        //  296px: dos piezas que no son del sistema, y entre las dos se comian
        //  mas presupuesto de acento que todo el resto de la pantalla junto.
        {
            G::SolidBrush ub(kAcc);
            roundRect(g, (G::REAL)(COL_R_X + (COL_R_W - 64)/2), (G::REAL)(CAB_TAB_Y + TAB_H - 3),
                      64.0f, 2.0f, 1.0f, nullptr, &ub);
            drawTxFitC(g, L"CABINA", G::FontStyleBold, kFTxt,
                           (G::REAL)(COL_R_X + COL_R_W/2), (G::REAL)(CAB_TAB_Y + 3), 1.1f,
                           (G::REAL)(COL_R_W - 24), TxRotulo);
        }
        // preview de cabina: caja + conos + micro colocado
        {
            const int cmv = g_params.cabModel.load();
            int spk = 2;
            if (cmv >= 0 && cmv < 6) {
                const char c0 = kCabNames[cmv][0];
                spk = (c0 == '1') ? 1 : (c0 == '4') ? 4 : 2;
            } else {
                spk = 1;   // IR de usuario: una sola caja 1x12
            }
            const int inch = (cmv == 4) ? 15 : (cmv == 5) ? 10 : 12;   // 1x15 / 2x10 / 12"
            drawCabPreview(g, (G::REAL)CAB_PV_X, (G::REAL)CAB_PV_Y,
                           (G::REAL)CAB_PV_W, (G::REAL)CAB_PV_H,
                           spk, inch, g_params.cabMic.load(), g_params.cabDist.load());
        }
        hairSep(g, (G::REAL)(COL_R_X + 12), (G::REAL)CAB_SEP_Y,
                (G::REAL)(COL_R_W - 24), 22);
        // bases de botones/pills (texto, hover y estado en capa dinamica)
        {
            glassPillBody(g, (G::REAL)CAB_BTN_X, (G::REAL)CAB_BTN_Y, (G::REAL)CAB_BTN_W, (G::REAL)CAB_BTN_H, false, false, kAcc);
            glassPillBody(g, (G::REAL)CAB_SEL_X, (G::REAL)CAB_SEL_Y, (G::REAL)CAB_SEL_W, (G::REAL)CAB_SEL_H, false, false, kAcc);
            glassPillBody(g, (G::REAL)CAB_MIC_X, (G::REAL)CAB_MIC_Y, (G::REAL)CAB_MIC_W, (G::REAL)CAB_MIC_H, false, false, kAcc);
            glassPillBody(g, (G::REAL)CAB_DST_X, (G::REAL)CAB_DST_Y, (G::REAL)CAB_DST_W, (G::REAL)CAB_DST_H, false, false, kAcc);
        }
        // --- MEZCLA: rotulo + pista + extremos (el relleno, el mango y el valor
        //     los pinta la capa viva) ---
        {
            G::Font fMixLbl(kFontTec, kTRotulo, G::FontStyleBold);
            G::SolidBrush ltb(kFTxt3);
            g.DrawString(tr(L"MEZCLA"), -1, &fMixLbl,
                         G::RectF((G::REAL)MIX_SL_X, (G::REAL)MIX_LBL_Y, 160.0f, (G::REAL)MIX_LBL_H),
                         nullptr, &ltb);
            // Los dos extremos: que mezcla esto. Sin ellos "MEZCLA" es un numero
            // sin direccion (la barra sube hacia la cabina, no hacia "mas").
            {
                G::Font fEnd(kFontTec, kTPico);
                G::StringFormat sfR; sfR.SetAlignment(G::StringAlignmentFar);
                g.DrawString(tr(L"DIRECTO"), -1, &fEnd,
                             G::RectF((G::REAL)MIX_SL_X, (G::REAL)MIX_LEG_Y, 110.0f, 14.0f),
                             nullptr, &ltb);
                g.DrawString(tr(L"CABINA"), -1, &fEnd,
                             G::RectF((G::REAL)(MIX_SL_X + MIX_SL_W - 110), (G::REAL)MIX_LEG_Y,
                                      110.0f, 14.0f), &sfR, &ltb);
            }
            // la pista del fader es un HUECO del sistema, no una pieza azul:
            // el azul marino no existe en la paleta (la app no tiene 2do acento)
            G::SolidBrush tr(kWellA);
            G::Pen trp(kWellLn, 1.0f);
            roundRect(g, (G::REAL)MIX_SL_X, (G::REAL)MIX_SL_Y, (G::REAL)MIX_SL_W,
                      (G::REAL)MIX_SL_H, (G::REAL)MIX_SL_H*0.5f, &trp, &tr);
        }
        // scope SALIDA: fondo con degradado, rejilla tenue y linea base
        {
            const G::RectF sr((G::REAL)SCOPE_X, (G::REAL)SCOPE_Y,
                              (G::REAL)SCOPE_W, (G::REAL)SCOPE_H);
            // Visualizador = hueco del sistema (dos tonos casi negros). Llevaba un
            //  degradado AZUL MARINO y rejilla azul-blanca: 18k pixeles fuera de
            //  paleta (1,2% de la ventana) y un segundo acento implicito.
            glassWell(g, (G::REAL)SCOPE_X, (G::REAL)SCOPE_Y,
                      (G::REAL)SCOPE_W, (G::REAL)SCOPE_H, kRWell);
            g.SetClip(sr, G::CombineModeReplace);
            G::Pen gl(G::Color(64, kWellLn.GetR(), kWellLn.GetG(), kWellLn.GetB()), 1.0f);
            for (int i = 1; i < 4; ++i) {
                if (i == 2) continue;                       // el centro lo marca la base
                const G::REAL gx = sr.X + sr.Width*i/4.0f;
                g.DrawLine(&gl, gx, sr.Y + 7.0f, gx, sr.GetBottom() - 7.0f);
                const G::REAL gy = sr.Y + sr.Height*i/4.0f;
                g.DrawLine(&gl, sr.X + 7.0f, gy, sr.GetRight() - 7.0f, gy);
            }
            G::Pen bl(G::Color(96, kWellLn.GetR(), kWellLn.GetG(), kWellLn.GetB()), 1.0f);
            g.DrawLine(&bl, sr.X + 7.0f, sr.Y + sr.Height*0.5f,
                             sr.GetRight() - 7.0f, sr.Y + sr.Height*0.5f);
            g.ResetClip();
        }
    }

    // --- Footer del diseno: panel + preset + medidores + CLIP + MASTER ---
    {
        glassPanel(g, 24.0f, (G::REAL)FOOT_Y, (G::REAL)(WIN_W - 48), (G::REAL)FOOT_H, kRPanel, true);
        // UN solo estilo de rotulo en todo el pie: kFootLblFam/kFootLblPt (papel
        // de rotulo: mayusculas cortas), tono medio, centrado sobre su control o
        // sobre el arranque de su pista, y todos en la linea comun (BOT_LBL_Y)
        // dentro de una caja que da cabida a la linea entera (BOT_LBL_H). Los
        // medidores rotulaban con el papel de CUERPO y su caja les recortaba la
        // fila de abajo: el pie tenia, sin querer, dos rotulos distintos.
        G::Font fFootLbl(kFootLblFam, kFootLblPt);
        G::SolidBrush tb(kFTxt3);
        G::StringFormat sfMC; sfMC.SetAlignment(G::StringAlignmentCenter);
        drawMeterTrack(g, fFootLbl, L"INPUT",  (G::REAL)MTR_IN_X,  (G::REAL)BOT_MTR_Y, (G::REAL)MTR_IN_W);
        drawMeterTrack(g, fFootLbl, L"OUTPUT", (G::REAL)MTR_OUT_X, (G::REAL)BOT_MTR_Y, (G::REAL)MTR_OUT_W);
        drawMeterTrack(g, fFootLbl, L"GATE",   (G::REAL)MTR_GT_X,  (G::REAL)BOT_MTR_Y, (G::REAL)MTR_GT_W);
        // etiqueta CLIP (el LED vivo lo pinta la capa dinamica)
        {
            // CLIP: rotulo centrado sobre su LED y en LA MISMA linea que los
            // demas del pie (antes iba a 8,5pt y a otra altura, pegado al GATE).
            G::StringFormat sfCC; sfCC.SetAlignment(G::StringAlignmentCenter);
            g.DrawString(L"CLIP", -1, &fFootLbl,
                         G::RectF((G::REAL)CLIP_LED_X - 35.0f, (G::REAL)BOT_LBL_Y, 80.0f,
                                  (G::REAL)BOT_LBL_H),
                         &sfCC, &tb);
        }
        drawKnobBody(g, (G::REAL)MASTER_X, (G::REAL)MASTER_Y, (G::REAL)MASTER_R);
        g.DrawString(L"MASTER", -1, &fFootLbl,
                     G::RectF((G::REAL)(MASTER_X - 60), (G::REAL)BOT_LBL_Y, 120.0f,
                              (G::REAL)BOT_LBL_H),
                     &sfMC, &tb);
        // Perilla de TRIM de entrada: cuerpo en la escena estatica, puntero y
        // aro los pinta la capa dinamica como al resto. Se llama TRIM y no INPUT
        // porque INPUT ya es el nombre del medidor de al lado: dos rotulos
        // iguales a 5cm uno de otro no decian cual era cual.
        drawKnobBody(g, (G::REAL)IN_TRIM_X, (G::REAL)IN_TRIM_Y, (G::REAL)IN_TRIM_R);
        {
            G::StringFormat sfT; sfT.SetAlignment(G::StringAlignmentCenter);
            g.DrawString(L"TRIM", -1, &fFootLbl,
                         G::RectF((G::REAL)(IN_TRIM_X - 45), (G::REAL)BOT_LBL_Y, 90.0f,
                                  (G::REAL)BOT_LBL_H),
                         &sfT, &tb);
        }
    }

    g_scene.valid = true;
}

// --- Lista del panel izquierdo: indices filtrados (pintado + input + rueda) ---
static WCHAR lowerW(WCHAR c) { return (c >= L'A' && c <= L'Z') ? (WCHAR)(c - L'A' + L'a') : c; }
// El ORDEN de la lista de presets: por amplificador. Los seis lotes de fabrica
// de un ampli y, detras, los presets del usuario montados en ese mismo ampli
// (los que la lista numera 7, 8...); los presets sin ampli van al final, juntos.
// Antes el orden era el crudo del banco y los del usuario caian al final de
// todo, lejos de su ampli: para saber con que ampli sonaba un preset habia que
// leer un numero global (31) que no era una posicion de nada.
static int presetOrder(int* out) {
    int n = 0;
    for (int g = 0; g < kAmpCount; ++g) {
        for (int s = 0; s < kPresetsPerAmp; ++s) out[n++] = g*kPresetsPerAmp + s;
        for (int i = 0; i < (int)g_userPresets.size(); ++i)
            if (g_userPresets[i].model == g) out[n++] = kNumPresets + i;
        // Los presets de TONE3000 van PEGADOS a su cabezal (el PERFIL), que es
        // donde suenan: en el orden curado se leen como "los del neural".
        if (g == 12)
            for (int i = 0; i < t3kPresetCount() && i < kMaxT3kPresets; ++i)
                out[n++] = presetTotal() + i;
    }
    for (int i = 0; i < (int)g_userPresets.size(); ++i)
        if (g_userPresets[i].model < 0 || g_userPresets[i].model >= kAmpCount)
            out[n++] = kNumPresets + i;
    return n;
}

// --- El navegador de presets (paso 18): filtros y orden ----------------------
// El estado de la ventana superpuesta. Los filtros se COMBINAN (AND entre
// grupos, OR dentro de cada uno), como los Filter tags de Guitar Rig; -1 / 0
// significan "cualquiera".
static int   g_brwSort   = 0;      // 0 curado 1 A-Z 2 Z-A 3 ampli 4 recientes
static int   g_brwColor  = -1;     // -1 cualquiera, 0..4 color de favorito
static int   g_brwOrigin = -1;     // -1 cualquiera, 0 fabrica, 1 mio
static unsigned int g_brwChar = 0; // mascara de caracter; 0 = cualquiera
static unsigned int g_brwAmp  = 0; // mascara de ampli;    0 = cualquiera
static WCHAR g_brwText[64] = L"";  // el buscador (nombre + ampli + etiquetas + autor)
// El nombre en minusculas, para el buscador y el orden alfabetico.
static void presetNameLow(int idx, WCHAR* out, int n) {
    if (!out || n <= 0) return;
    WCHAR pn[128]; presetNameW(idx, pn, 128);
    int j = 0;
    for (; j + 1 < n && pn[j]; ++j) out[j] = lowerW(pn[j]);
    out[j] = 0;
}
// Anade una cadena en minusculas a un pajar, separada por un espacio.
static void catLow(WCHAR* dst, const WCHAR* src, size_t cap) {
    size_t n = wcslen(dst);
    if (n + 1 >= cap) return;
    if (n) { dst[n++] = L' '; dst[n] = 0; }
    for (size_t i = 0; src[i] && n + 1 < cap; ++i) dst[n++] = lowerW(src[i]);
    dst[n] = 0;
}
// Pasa el preset por todos los filtros del navegador.
// Lo que busca el buscador: lo mismo que ENSENA el navegador (nombre, ampli,
// caracteres y autor), en minusculas y separado por espacios.
static void presetHaystack(int idx, WCHAR* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    WCHAR nm[128] = L"";
    presetNameLow(idx, nm, 128);
    wcsncpy_s(out, cap, nm, _TRUNCATE);
    const int g = presetGroup(idx);
    if (g >= 0) catLow(out, tr(kAmpNames[g]), cap);
    const unsigned int tg = presetTags(idx);
    for (int c = 0; c < kCharCount; ++c)
        if (tg & (1u << c)) catLow(out, tr(kCharNames[c]), cap);
    catLow(out, presetAuthor(idx), cap);
}

static bool presetMatches(int idx) {
    if (g_brwColor >= 0 && presetFavColor(idx) != g_brwColor) return false;
    // Origen: -1 cualquiera, 0 fabrica, 1 mio, 2 TONE3000
    if (g_brwOrigin == 0 && (isUserPreset(idx) || isT3kPreset(idx))) return false;
    if (g_brwOrigin == 1 && !isUserPreset(idx)) return false;
    if (g_brwOrigin == 2 && !isT3kPreset(idx)) return false;
    if (g_brwChar && !(presetTags(idx) & g_brwChar)) return false;
    if (g_brwAmp) {
        const int g = presetGroup(idx);
        if (g < 0 || !(g_brwAmp & (1u << g))) return false;
    }
    if (g_brwText[0]) {
        // El buscador mira lo que el navegador ENSENA: nombre, ampli, caracteres
        // y autor (como el de Guitar Rig, que tambien busca en los metadatos).
        WCHAR hay[320] = L"";
        presetHaystack(idx, hay, 320);
        WCHAR needle[64]; int j = 0;
        for (; j + 1 < 64 && g_brwText[j]; ++j) needle[j] = lowerW(g_brwText[j]);
        needle[j] = 0;
        if (!wcsstr(hay, needle)) return false;
    }
    return true;
}
// El comparador del orden. Devuelve false en los empates para que el orden
// curado (el de partida) se conserve (stable_sort).
static bool browseLess(int a, int b) {
    switch (g_brwSort) {
        case 1: case 2: {
            WCHAR na[128], nb[128];
            presetNameLow(a, na, 128); presetNameLow(b, nb, 128);
            const int c = wcscmp(na, nb);
            if (c == 0) return false;
            return (g_brwSort == 1) ? (c < 0) : (c > 0);
        }
        case 3: {
            const int ga = presetGroup(a), gb = presetGroup(b);
            if (ga != gb) return ga < gb;
            const int ia = presetInGroup(a), ib = presetInGroup(b);
            return ia < ib;
        }
        case 4: {
            const long long ua = presetUsed(a), ub = presetUsed(b);
            if (ua != ub) return ua > ub;
            const long long ca = presetCreated(a), cb = presetCreated(b);
            return ca > cb;
        }
        default: return false;
    }
}
// Los indices que el navegador ensena: filtrados y ordenados. Es el unico
// sitio que decide que sale y en que orden (pintado, clic y teclado leen esto).
static int browseOrder(int* out) {
    int ord[kMaxListAll];
    const int tot = presetOrder(ord);
    int n = 0;
    for (int k = 0; k < tot; ++k) if (presetMatches(ord[k])) out[n++] = ord[k];
    if (g_brwSort != 0 && n > 1) std::stable_sort(out, out + n, browseLess);
    return n;
}

// Filtra el catalogo por el texto del buscador de la cabecera.
static void topFilter() {
    int ord[kMaxListAll];
    const int tot = presetOrder(ord);
    WCHAR q[64]; int j = 0;
    for (; j + 1 < 64 && g_topQuery[j]; ++j) q[j] = lowerW(g_topQuery[j]);
    q[j] = 0;
    int n = 0;
    for (int k = 0; k < tot && n < (int)(sizeof(g_topList)/sizeof(g_topList[0])); ++k) {
        if (!q[0]) { g_topList[n++] = ord[k]; continue; }
        WCHAR hay[320];
        presetHaystack(ord[k], hay, 320);
        if (wcsstr(hay, q)) g_topList[n++] = ord[k];
    }
    g_topCount = n;
    if (g_topSel >= n) g_topSel = n - 1;
    if (g_topSel < -1) g_topSel = -1;
    const int maxS = (n > 8) ? n - 8 : 0;
    if (g_topScroll > maxS) g_topScroll = maxS;
    if (g_topScroll < 0) g_topScroll = 0;
}

static const int kTopRowH = 30, kTopMaxRows = 8, kTopPad = 6;

// Rect del despliegue (bajo la caja del preset).
static RECT topDropRect() {
    const int rows = (g_topCount <= 0) ? 1 : (g_topCount > kTopMaxRows ? kTopMaxRows : g_topCount);
    RECT r = { TOP_BOX_X - 4, TOP_BOX_Y + TOP_BOX_H + 6,
               TOP_BOX_X + TOP_BOX_W + 4, TOP_BOX_Y + TOP_BOX_H + 6 + kTopPad*2 + rows*kTopRowH };
    return r;
}

// Carga el preset elegido en el buscador (mismo camino que el navegador).
static void topApply(int idx) {
    if (idx < 0 || idx >= presetTotal()) return;
    if (!isT3kPreset(idx)) g_presetIdx = idx;
    presetApplyFromBrowser(idx);
    g_topOpen = false;
    g_topQuery[0] = 0;
    g_topSel = -1;
    g_topScroll = 0;
    g_sceneDirty.store(true);
}

// Las entradas de la lista del panel izquierdo: los 13 amplis, filtrados por el
// buscador (nombre Y subtitulo) y por el "solo mis favoritos". La rama de PRESETS
// se retiro con la pestana (paso 19): el catalogo de presets es del navegador.
static void listFilterIdx(int* out, int* pCount) {
    int n = 0;
    WCHAR lowF[40]; int fi = 0;
    for (; fi < 39 && g_ampFilter[fi]; ++fi) lowF[fi] = lowerW(g_ampFilter[fi]);
    lowF[fi] = 0;
    // 13 amplis, no 12: con `i < 12` el cabezal PERFIL no aparecia NUNCA en
    // la lista (quedaba inalcanzable desde la interfaz, que es justo la mitad
    // del producto que se anuncia).
    for (int i = 0; i < kAmpCount; ++i) {
        if (g_favOnly && !ampFav(i)) continue;
        if (fi == 0) { out[n++] = i; continue; }
        WCHAR all[96];
        swprintf_s(all, L"%s %s", tr(kAmpNames[i]), tr(kAmpSubs[i]));
        WCHAR lowAll[96];
        size_t j = 0;
        for (; j + 1 < 96 && all[j]; ++j) lowAll[j] = lowerW(all[j]);
        lowAll[j] = 0;
        if (wcsstr(lowAll, lowF)) out[n++] = i;
    }
    *pCount = n;
}
static int listVisibleRows() { return LIST_H / LIST_ROW_H; }
// La fila del navegador: UNA definicion para pintura, hover y clic. Antes la
// pintura usaba 4px menos de alto que la banda de pulsacion y los dos numeros
// vivian en sitios distintos; aqui quedan juntos y el aire de abajo es
// explicito: es visual, no de pulsacion (las filas se pulsan pegadas).
static RECT navRowRect(int vi) {
    const int rx = COL_L_X + 8, ry = LIST_Y + vi*LIST_ROW_H;
    RECT r = { rx, ry, rx + (COL_L_W - 16), ry + LIST_ROW_H };
    return r;
}
static RECT navRowPaintRect(int vi) {
    RECT r = navRowRect(vi);
    r.bottom -= 4;             // el aire entre filas es solo visual
    return r;
}
// La caja de la ESTRELLA de favoritos de una fila: la unica chapa del canto
// derecho. Antes eran tres (estrella, lapiz y aspa) porque la lista tambien
// llevaba presets del usuario; con la pestana PRESETS retirada (paso 19) queda la
// estrella, que es lo unico que ofrece la fila de un ampli. Una sola definicion
// para el pintado y para el clic, que es lo que impide que vuelvan a separarse.
static RECT navStarRect(int vi) {
    RECT r = navRowRect(vi);
    r.left = r.right - 28; r.right = r.right - 2;
    r.top += 4; r.bottom -= 8;
    return r;
}
static bool rectHas(const RECT& r, int x, int y) {
    POINT p = { x, y };
    return PtInRect(&r, p) != 0;
}
// La caja de un slot del pedalboard. La misma suma (sx + PED_SLOT_W...) estaba
// escrita en la pintura, en hoverKey y en el clic: ahora sale de aqui.
static RECT pedSlotBox(int sx, int sy) {
    RECT r = { sx, sy, sx + PED_SLOT_W, sy + PED_SLOT_H };
    return r;
}
// El rotulo del lote en la banda del cabezal: QUE lote del ampli suena. Antes
// era el indice global ("PRESET 31"), que no es una posicion que nadie pueda
// buscar en ninguna parte; ahora es el mismo numero que ensena el navegador.
static void presetTagW(int idx, WCHAR* out, int n) {
    if (!out || n <= 0) return;
    const int g  = presetGroup(idx);
    const int ig = presetInGroup(idx);
    if (g < 0 || ig <= 0) { wcsncpy_s(out, (size_t)n, L"SIN AMPLIFICADOR", _TRUNCATE); return; }
    swprintf_s(out, (size_t)n, L"%s \x00B7 %d", tr(kAmpNames[g]), ig);
}
// El texto del hueco del buscador. El panel izquierdo lista AMPLIFICADORES (la
// pestana PRESETS se retiro en el paso 19), asi que el hueco lo dice y no puede
// prometer otra cosa: la busqueda de presets vive en el navegador.
static const WCHAR* searchHint() {
    return L"Buscar amplificador...";
}
// Las RANURAS de la lista, en orden de pintado: una por entrada de listFilterIdx
// (que es lo que seleccionan el clic y las flechas). La lista es PLANA (paso
// 18.9, y tras el paso 19 es la unica que hay): un ampli por fila, sin
// encabezados intercalados, y el subtitulo de cada fila dice su nombre.
static int navRows(int* out) {
    int idx[kMaxListEntries], cnt = 0;
    listFilterIdx(idx, &cnt);
    for (int i = 0; i < cnt; ++i) out[i] = idx[i];
    return cnt;
}
static int listScrollMax();
// Deja la fila `a` a la vista (la lista la seguiria con el raton; con el teclado
// tiene que seguirla sola o el movimiento no se ve y no parece que pase nada)
static void listFollow(int a) {
    int rows[kMaxNavRows];
    const int n = navRows(rows);
    int pos = -1;
    for (int i = 0; i < n; ++i) if (rows[i] == a) { pos = i; break; }
    if (pos < 0) return;
    const int vis = listVisibleRows();
    if (pos < g_listScroll)                    g_listScroll = pos;
    if (pos >= g_listScroll + vis)             g_listScroll = pos - vis + 1;
    if (g_listScroll > listScrollMax())        g_listScroll = listScrollMax();
    if (g_listScroll < 0)                      g_listScroll = 0;
}
// Tope del scroll: en RANURAS, no en entradas (los encabezados ocupan ranura y
// el final de la lista se quedaba 13 ranuras fuera de alcance cuando esto
// contaba solo presets).
static int listScrollMax() {
    int rows[kMaxNavRows];
    const int n = navRows(rows);
    const int maxS = n - listVisibleRows();
    return (maxS > 0) ? maxS : 0;
}

// --- GA_PROF: medidor temporal del coste de pintado a _perf.log -------------
// --- GA_PROF: reparto por secciones del frame (temporal) -------------------
#define PF_MAXSEC 40
static const char* g_pfLabel[PF_MAXSEC];
static long long   g_pfSecAcc[PF_MAXSEC];
static int         g_pfSecN = 0;
static LARGE_INTEGER g_pfSecT0;
// GA_PROF=1 habilita la medicion (y el volcado a _perf.log). Sin la variable
// el instrumento esta apagado: el app no escribe ningun fichero de metricas.
static bool profOn() {
    static int on = -1;
    if (on < 0) {
        char b[4] = {};
        on = (GetEnvironmentVariableA("GA_PROF", b, sizeof(b)) > 0 && b[0] == '1') ? 1 : 0;
    }
    return on == 1;
}
static void profSectionsReset() { g_pfSecN = 0; if (profOn()) QueryPerformanceCounter(&g_pfSecT0); }
static void profMark(const char* label) {                 //PFMARK
    if (!profOn() || g_pfSecN >= PF_MAXSEC) return;        //PFMARK
    LARGE_INTEGER t; QueryPerformanceCounter(&t);         //PFMARK
    g_pfSecAcc[g_pfSecN] = t.QuadPart - g_pfSecT0.QuadPart;
    g_pfLabel[g_pfSecN] = label;                          //PFMARK
    ++g_pfSecN; g_pfSecT0 = t;                            //PFMARK
}                                                          //PFMARK

// --- GA_PROF: origen del repaint (0=otro, 1=timer, 2=evento de entrada) -----
static int g_pfSrc = 0;
// FILETIME no expone dwLowDateTime sin el union del SDK: se interpreta como
// ULONGLONG directamente (mismo layout, little-endian)
static ULONGLONG ftToU64(const FILETIME& ft) {
    ULONGLONG v; memcpy(&v, &ft, sizeof(v)); return v;
}

static void profPaint(long long t, long long tScene, long long tDyn, long long tBlit, bool isFull) {
    if (!profOn()) return;   // solo con GA_PROF=1 se mide y se escribe _perf.log
    static LARGE_INTEGER freq = {};
    static long long sum[4] = { 0, 0, 0, 0 }, t0 = 0;
    static long long sumSrc[3] = { 0, 0, 0 };
    static int n = 0, nFull = 0, nSrc[3] = { 0, 0, 0 };
    static ULONGLONG cpu0 = 0;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    if (n == 0) {
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1); t0 = t1.QuadPart;
        FILETIME fc, fe, fk, fu; cpu0 = 0;
        if (GetProcessTimes(GetCurrentProcess(), &fc, &fe, &fk, &fu))
            cpu0 = ftToU64(fk) + ftToU64(fu);   // FILETIME es little-endian de 64 bits
    }
    int src = g_pfSrc; if (src < 0 || src > 2) src = 0; g_pfSrc = 0;
    sum[0] += t; sum[1] += tScene; sum[2] += tDyn; sum[3] += tBlit; ++n;
    sumSrc[src] += t; ++nSrc[src];
    if (isFull) ++nFull;
    if (n >= 20) {   // bloques de 20 frames: escenarios con pocos paints tambien log
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        const double spanMs = (double)(t1.QuadPart - t0)*1000.0/(double)freq.QuadPart;
        const double k  = 1000.0/(double)freq.QuadPart/(double)n;
        const double ks = 1000.0/(double)freq.QuadPart;
        // CPU del proceso en el bloque: FILETIME va en unidades de 100ns,
        // asi que ms*100 == unidades de 100ns por milisegundo
        double cpuPct = 0.0;
        if (cpu0 && spanMs > 0.0) {
            FILETIME fc, fe, fk, fu;
            if (GetProcessTimes(GetCurrentProcess(), &fc, &fe, &fk, &fu)) {
                const ULONGLONG cpu1 = ftToU64(fk) + ftToU64(fu);
                if (cpu1 > cpu0) cpuPct = (double)(cpu1 - cpu0)/((double)spanMs*100.0);
            }
        }
        // salud del hilo de audio (aqui se vacian los atomics compartidos)
        const long long xrU = g_xrunUnderrun.exchange(0);
        const long long xrL = g_xrunLate.exchange(0);
        const long long xrG = g_xrunMaxGap.exchange(0);
        FILE* f = nullptr;
        if (fopen_s(&f, "_perf.log", "a") == 0 && f) {
            for (int s = 0; s < g_pfSecN; ++s)
                fprintf(f, "   sec[%s]=%.2f\n", g_pfLabel[s],
                        (double)g_pfSecAcc[s]/(double)freq.QuadPart*1000.0);
            fprintf(f, "frames=%d paint=%.2f escena=%.2f dinamica=%.2f volcado=%.2f span=%.0fms fps=%.1f full=%d/%d\n",
                    n, sum[0]*k, sum[1]*k, sum[2]*k, sum[3]*k,
                    spanMs, (double)n*1000.0/spanMs, nFull, n);
            fprintf(f, "   origen timer=%d real=%d otro=%d paintTimer=%.2f paintReal=%.2f cpu=%.1f%% audio xrun=%lld late=%lld gapmax=%lldms\n",
                    nSrc[1], nSrc[2], nSrc[0],
                    nSrc[1] ? sumSrc[1]*ks/(double)nSrc[1] : 0.0,
                    nSrc[2] ? sumSrc[2]*ks/(double)nSrc[2] : 0.0,
                    cpuPct, xrU, xrL, xrG);
            fclose(f);
        }
        n = 0; for (int i = 0; i < 4; ++i) sum[i] = 0; nFull = 0;
        for (int i = 0; i < 3; ++i) { sumSrc[i] = 0; nSrc[i] = 0; }
    }
}

// Ultimo punto del raton: lo usan el hover de filas y los botones del panel.

// ---------------------------------------------------------------------------
//  Panel del explorador TONE3000 (modal)
// ---------------------------------------------------------------------------
//  La jerarquia es la de su interfaz de referencia: marca, buscador, los siete
//  chips de tipo de equipo, rejilla de tarjetas (foto, titulo, tipo, formato,
//  descargas, autor) y paginacion. Sin clave se ve lo publico (tendencias);
//  con clave se busca y el .nam baja a la cadena del cabezal PERFIL.
static G::Color t3kAccent(const WCHAR* gear) {
    struct C { const WCHAR* g; DWORD c; };
    static const C kMap[7] = {
        { L"Amp + Cab",    0x00f2a33c }, { L"Amp Head", 0x00e8664a }, { L"Cabinet", 0x007ad0a0 },
        { L"Pedal",        0x0043e18d }, { L"Outboard", 0x004d86ff }, { L"Spaces",  0x00b58cf0 },
        { L"Experimental", 0x00e05a8a },
    };
    for (int i = 0; i < 7; ++i) if (wcscmp(gear, kMap[i].g) == 0)
        return G::Color(255, GetRValue(kMap[i].c), GetGValue(kMap[i].c), GetBValue(kMap[i].c));
    return G::Color(0xff, 0x82, 0x7d, 0x76);
}
static void t3kButton(G::Graphics& g, G::REAL x, G::REAL y, G::REAL w, G::REAL h,
                      const WCHAR* label, bool hov, G::Color ac, bool solid) {
    G::Color bg = solid ? G::Color(255, (BYTE)(ac.GetR()/4), (BYTE)(ac.GetG()/4), (BYTE)(ac.GetB()/4))
                        : (hov ? G::Color(0xff, 0x17, 0x16, 0x18) : G::Color(255, 0x12, 0x17, 0x1d));
    G::SolidBrush bb(bg);
    G::Pen bp((hov || solid) ? G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()) : G::Color(0xff, 0x27, 0x25, 0x27), 1.2f);
    roundRect(g, x, y, w, h, 7.0f, &bp, &bb);
    drawTxFitC(g, label, G::FontStyleBold,
                   (hov || solid) ? G::Color(255, 0xf2, 0xf6, 0xfa) : G::Color(0xdc, 0x9c, 0x98, 0x90),
                   x + w*0.5f, y + h*0.5f - 7.0f, 0.8f, w - 14.0f, TxRotulo);
    pressVeil(g, x, y, w, h, 7.0f);
}
// --- La UI recoge el trabajo terminado (siempre en el hilo de la ventana) --
// --- Buscar: sin caja = tendencias; con caja, buscar (necesita clave) ------
// Los chips y el "solo verificados" se aplican sobre la ultima lista recibida.
// Con la lista publica filtran los 10 tonos; en la busqueda filtran la pagina
// que ya viene filtrada por el servidor (por eso el estado lo dice).
static void t3kApplyGearFilter()
{
    g_t3kItems.clear();
    for (const T3kItem& it : g_t3kPageAll) {
        if (g_t3kGear >= 0 && wcscmp(it.gear.c_str(), kT3kGears[g_t3kGear].label) != 0) continue;
        if (g_t3kVerif && !it.verified) continue;
        g_t3kItems.push_back(it);
    }
    const int n = (int)g_t3kItems.size(), all = (int)g_t3kPageAll.size();
    WCHAR b[160];
    const WCHAR* filtro = (g_t3kGear >= 0) ? kT3kGears[g_t3kGear].label
                         : (g_t3kVerif ? L"VERIFICADOS" : L"TODOS");
    if (g_t3kIsTrend) swprintf_s(b, tr(L"TENDENCIAS (%s): %d de %d"), filtro, n, all);
    else             swprintf_s(b, tr(L"RESULTADOS (%s en la pagina): %d de %d"), filtro, n, all);
    t3kStatusW(b);
}
static void t3kDoSearch()
{
    if (g_t3kQuery[0] == 0) {
        g_t3kPage = 1;
        if (!g_t3kBusy) { g_t3kBusy = true; t3kStartTrend(); }
    } else if (!g_t3kConnected) {
        g_t3kConnOpen = true;
        t3kStatusW(L"LA BUSQUEDA NECESITA CONECTAR CON TONE3000");
    } else {
        g_t3kPage = 1; g_t3kBusy = true; t3kStartSearch();
    }
}
//  Los chips filtran en local la lista publica (que se trae de una vez); con
//  clave la busqueda se filtra en el servidor, que es donde toca.
static void t3kApplyResult()
{
    std::string kind, body, err;
    int status = 0;
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        if (g_t3kQueue.empty()) return;
        kind = g_t3kQueue.front().kind; body = g_t3kQueue.front().body;
        err  = g_t3kQueue.front().err;  status = g_t3kQueue.front().status;
        g_t3kQueue.erase(g_t3kQueue.begin());
    }
    // Cualquier trabajo terminado libera el "consultando": antes solo lo hacia
    // una rama concreta y un resultado perdido dejaba el panel colgado.
    g_t3kBusy = false;
    // Si un worker renovo la sesion (401 -> refresh), el .ini lo guarda la UI: un
    // solo hilo escribe el fichero.
    if (g_t3kDirtySave.exchange(false)) saveIni();

    if (kind == "trend" || kind == "search") {
        g_t3kLocalMode = false;   // lo que llega de la API ya no es la biblioteca local
        const bool trend = (kind == "trend");
        g_t3kBusy = false;
        if (status == 200) {
            std::vector<T3kItem> items; long long total = 0;
            if (t3kParseList(body, items, total) == 0) {
                g_t3kPageAll = items;
                g_t3kTotal   = total;
                g_t3kIsTrend = trend;
                g_t3kSel     = -1;
                t3kApplyGearFilter();      // chips + verificados, en local
                t3kStatusW(tr(trend ? L"TENDENCIAS DE TONE3000 (publico, sin clave)"
                                    : L"RESULTADOS DE LA BUSQUEDA"));
                t3kStartImages();
            } else {
                t3kStatusW(L"LA RESPUESTA NO SE PUDO LEER");
            }
        } else {
            std::wstring m = u8w(err);
            if (status == 401 || status == 403) {
                // 401 no es siempre "capa gratuita": con un token pegado lo normal
                // es que haya caducado o sea de otra cuenta. Antes se culpaba a los
                // terminos y el usuario no entendia nada.
                if (kind == "search" && t3kTokenSnap().empty())
                    m = tr(L"LA BUSQUEDA CON FILTROS NECESITA TU TOKEN: PULSA CONECTAR O USA \"VER TONE3000\"");
                else
                    m = tr(L"TU TOKEN DE TONE3000 NO VALE O HA CADUCADO: VUELVE A PEGARLO");
                if (kind == "search") m = tr(L"TONES/SEARCH ESTA FUERA DE LA CAPA GRATUITA: USA \"VER TONE3000\"");
                g_t3kConnected = false;
            } else if (status == 0) {
                m = tr(L"SIN CONEXION CON TONE3000");
            } else {
                WCHAR b[96]; swprintf_s(b, tr(L"EL SERVIDOR DEVOLVIO %d"), status); m = b;
            }
            t3kStatusW(m);
            if (trend) { g_t3kItems.clear(); g_t3kTotal = 0; }
        }
        return;
    }
    if (kind == "preset") {
        const std::wstring m = body.empty() ? std::wstring(tr(L"PRESET CARGADO")) : u8w(body);
        t3kStatusW(m);
        toast(m.c_str());
        g_sceneDirty.store(true);   // las celdas y la escena cambian
        return;
    }
    if (kind == "device") {
        // El codigo queda COPIADO en cuanto aparece: asi un Ctrl+V en la pagina
        // basta, y no depende de que el navegador se abra solo.
        std::string code;
        { std::lock_guard<std::mutex> lk(g_t3kMutex); code = g_t3kUserCode; }
        const std::wstring wcode(code.begin(), code.end());
        t3kStatusW(copyToClipboard(wcode.c_str())
                       ? L"CONFIRMA EL CODIGO EN EL NAVEGADOR (YA LO TIENES COPIADO)"
                       : L"CONFIRMA EL CODIGO EN EL NAVEGADOR");
        return;
    }
    if (kind == "connect") {
        // invalid_client = lo que se pego no es una clave publicable (t3k_pub_...).
        const std::wstring e = u8w(err);
        if (e.find(L"invalid_client") != std::wstring::npos) {
            // Se VACIA el campo: si no, cada CONECTAR repetiria el mismo error y el
            // camino comodo (campo vacio = sesion instalada) no llegaria nunca.
            g_t3kKey[0] = 0;
            saveIni();
            t3kStatusW(tr(L"ESA NO ES UNA CLAVE PUBLICABLE (t3k_pub_...). YA HE VACIADO EL CAMPO:"
                       L" PULSA CONECTAR OTRA VEZ Y LO HAGO SOLO"));
        } else
            t3kStatusW(tr(L"NO SE PUDO CONECTAR: ") + e);
        return;
    }
    if (kind == "token") {
        t3k::Jv j; std::string at, rt;
        if (t3k::jParse(body, j)) { at = j.str("access_token"); rt = j.str("refresh_token"); }
        if (at.empty()) { t3kStatusW(L"EL SERVIDOR NO DEVOLVIO TOKEN"); return; }
        {
            std::lock_guard<std::mutex> lk(g_t3kMutex);
            g_t3kToken = at;
            if (!rt.empty()) g_t3kRefresh = rt;
            g_t3kUserCode.clear(); g_t3kVerifyUri.clear();
        }
        g_t3kConnected = true;
        saveIni();
        t3kStatusW(L"CONECTADO CON TONE3000");
        t3kStartSearch();
        return;
    }
    if (kind == "tokencheck") {
        if (status == 200) {
            bool prestada = false;
            {
                std::lock_guard<std::mutex> lk(g_t3kMutex);
                g_t3kToken = g_t3kPending;
                g_t3kUserCode.clear(); g_t3kVerifyUri.clear();
                prestada = g_t3kFromInstalled;
            }
            g_t3kConnected = true;
            // La credencial prestada NO se persiste: caduca en minutos y su app la
            // renueva sola, asi que guardarla dejaria "conectado" en falso.
            if (!prestada) saveIni();
            t3kStatusW(prestada ? L"CONECTADO CON LA SESION DE TU TONE3000 INSTALADO"
                                : L"CONECTADO CON TONE3000");
            t3kStartSearch();
        } else {
            bool prestada = false;
            {
                std::lock_guard<std::mutex> lk(g_t3kMutex);
                prestada = g_t3kFromInstalled;
                g_t3kFromInstalled = false;
            }
            if (prestada) {
                // El worker ya se ha ido al flujo de dispositivo (no se prueba la
                // prestada como clave): aqui solo se cuenta lo que pasa.
                t3kStatusW(L"LA SESION INSTALADA YA NO VALE: TE DOY UN CODIGO PARA CONFIRMAR");
                return;
            }
            // Lo que se pega puede no ser una credencial valida (una clave ya
            // revocada, un id de usuario, o la clave "legacy", que su API v1 no
            // acepta como Bearer). En vez de dejar al usuario adivinando, se le
            // manda al camino que SI funciona: el flujo de dispositivo con la clave
            // publicable, que la app lleva dentro.
            g_t3kKey[0] = 0;        // que no vuelva a atascarse el campo
            saveIni();
            t3kStatusW(tr(L"ESA CLAVE O TOKEN NO VALE (") + u8w(err) +
                       tr(L"). YA HE VACIADO EL CAMPO: PULSA CONECTAR OTRA VEZ Y NECESITARE UN CODIGO"));
        }
        return;
    }
    if (kind == "grab") {
        std::wstring path, title;
        {
            std::lock_guard<std::mutex> lk(g_t3kMutex);
            path = g_t3kSavedPath; title = g_t3kSavedTitle;
            g_t3kSavedPath.clear(); g_t3kSavedTitle.clear();
        }
        if (status == 200 && !path.empty()) {
            std::wstring msg;
            // Cae en la CELDA que el usuario eligio (la que pulso vacia); si no
            // eligio ninguna, al final de la fila.
            const int idx = namChainAddAt(path.c_str(), g_namDropIdx, msg);
            g_namDropIdx = -1;
            if (idx >= 0) {
                if (g_params.model.load() != 12) {   // quien suena es el cabezal PERFIL
                    g_params.model.store(12);
                    g_sceneDirty.store(true);
                    rebuildKnobs();
                }
                g_sceneDirty.store(true);            // las celdas viven en la escena
                t3kStatusW(tr(L"EN LA CELDA ") + std::to_wstring(idx + 1) + L": " + title);
                toast(L"CAPTURA PUESTA EN LA CELDA");
            } else {
                t3kStatusW(tr(L"EL FICHERO NO VALE: ") + msg);
            }
        } else if (status == 0) {
            t3kStatusW(tr(L"NO SE PUDO BAJAR: ") + u8w(err));
        } else {
            t3kStatusW(tr(L"NO SE PUDO BAJAR (HTTP ") + std::to_wstring(status) + L"): " + u8w(err));
        }
        return;
    }
}
// Titulo de la tarjeta en dos lineas (como la lista de referencia): si no
// cabe, la segunda se corta con puntos suspensivos en vez de salirse.
static void t3kWrapTitle(G::Graphics& g, const std::wstring& s, G::REAL x, G::REAL y, G::REAL maxW)
{
    G::Font f(kFontTec, kTRotulo, G::FontStyleBold);
    const G::Color c(255, 0xee, 0xf2, 0xf8);
    size_t cut = 0;
    for (size_t i = 1; i <= s.size(); ++i) {
        if (i < s.size() && s[i] != L' ') continue;
        const std::wstring part = s.substr(0, i);
        if (measureSpaced(g, part.c_str(), f, 0.2f) <= maxW) cut = i;
        else break;
    }
    if (cut == 0) cut = s.size();
    const std::wstring l1 = s.substr(0, cut);
    if (cut >= s.size()) {
        drawSpaced(g, l1.c_str(), f, c, x, y, 0.2f);
        return;
    }
    std::wstring rest = s.substr(cut);
    while (!rest.empty() && rest.front() == L' ') rest.erase(rest.begin());
    size_t cut2 = 0;
    for (size_t i = 1; i <= rest.size(); ++i) {
        if (i < rest.size() && rest[i] != L' ') continue;
        const std::wstring part = rest.substr(0, i) + L"\x2026";
        if (measureSpaced(g, part.c_str(), f, 0.2f) <= maxW) cut2 = i;
        else break;
    }
    if (cut2 == 0) cut2 = rest.size();
    std::wstring l2 = rest.substr(0, cut2);
    if (cut2 < rest.size()) l2 += L"\x2026";
    drawSpaced(g, l1.c_str(), f, c, x, y, 0.2f);
    drawSpaced(g, l2.c_str(), f, c, x, y + 14.0f, 0.2f);
}
static void t3kDrawSheet(G::Graphics& g)
{
    const G::REAL sx = (G::REAL)t3kSheetX(), sy = (G::REAL)t3kSheetY();
    // El panel de detras se APAGA: la hoja es modal y tiene que ser lo unico vivo.
    // El panel ya apaga la ventana entera (kVeil), pero el suyo propio se quedaba a
    // plena luz: en la captura del usuario las tarjetas competian con la hoja.
    {
        G::SolidBrush v(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
        g.FillRectangle(&v, (G::REAL)T3K_X, (G::REAL)T3K_Y, (G::REAL)T3K_W, (G::REAL)T3K_H);
    }
    // La hoja es cristal como todo lo demas: se separa por la sombra y por su
    // canto vivo, no por un filete ambar (el ambar sigue siendo solo estado).
    glassPanel(g, sx, sy, (G::REAL)T3K_SHEET_W, (G::REAL)T3K_SHEET_H, kRModal, true);
    drawTxFit(g, L"CONECTAR CON TONE3000", G::FontStyleBold, G::Color(0xff, 0xb7, 0xb1, 0xa7),
                  sx + 24.0f, sy + 18.0f, 1.6f, 420.0f, TxMarca);

    // Estado del flujo de dispositivo
    std::string code, uri;
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        code = g_t3kUserCode; uri = g_t3kVerifyUri;
    }
    if (!code.empty()) {
        G::Font fHint(kFontUI, kTCuerpo, G::FontStyleRegular);
        G::SolidBrush hb(G::Color(0xd2, 0x82, 0x7d, 0x76));
        g.DrawString(tr(L"Abre la pagina, entra en tu cuenta y confirma este codigo:"), -1, &fHint,
                     G::RectF(sx + 24.0f, sy + 50.0f, 700.0f, 20.0f), nullptr, &hb);
        // El hueco se mide COMO EL CODIGO (antes era una caja fija de 360 para nueve
        // caracteres: sobraba medio hueco) y el codigo se centra dentro.
        const std::wstring wcode = u8w(code);
        G::REAL cpt = 30.0f, csp = 6.0f;
        fitSpacedFont(g, wcode.c_str(), G::FontStyleBold, 6.0f, 500.0f, 30.0f, 20.0f, cpt, csp, kFontMono);
        G::Font cf(kFontMono, cpt, G::FontStyleBold);
        const G::REAL cwv = measureSpaced(g, wcode.c_str(), cf, csp);
        {
            const G::REAL ww = cwv + 56.0f, wx = sx + ((G::REAL)T3K_SHEET_W - ww)*0.5f;
            G::SolidBrush wb(kWellA);
            G::Pen wp(kWellLn, 1.0f);
            roundRect(g, wx, sy + 80.0f, ww, 60.0f, 8.0f, &wp, &wb);
            drawSpaced(g, wcode.c_str(), cf, G::Color(255, 0xf2, 0xf6, 0xfa),
                       sx + ((G::REAL)T3K_SHEET_W - cwv)*0.5f, sy + 68.0f, csp);
        }
        // La direccion va en pequeno (el boton ya abre esa direccion) y debajo el
        // plazo, que es un dato del servidor. Antes el rotulo del boton llevaba la
        // URL entera, ~55 caracteres en 300px: se salia de su caja sobre CANCELAR.
        if (!uri.empty())
            drawTxFitC(g, u8w(uri).c_str(), G::FontStyleRegular, G::Color(0x9c, 0x82, 0x7d, 0x76),
                           sx + (G::REAL)T3K_SHEET_W*0.5f, sy + 148.0f, 0.4f, 700.0f, TxPico);
        if (g_t3kExpMin > 0) {
            WCHAR wexp[64];
            swprintf_s(wexp, tr(L"El codigo caduca en %d minutos"), g_t3kExpMin);
            drawTxFitC(g, wexp, G::FontStyleRegular, G::Color(0x8c, 0x82, 0x7d, 0x76),
                           sx + (G::REAL)T3K_SHEET_W*0.5f, sy + 162.0f, 0.4f, 700.0f, TxPico);
        }
        const bool hov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_KEYOPEN;
        t3kButton(g, sx + t3kBtnGoX(), sy + T3K_BTN_Y, T3K_BTN_GO_W, T3K_BTN_H,
                  L"ABRIR TONE3000.COM/ACTIVATE", hov, kAcc, true);
    } else {
        G::Font fHint(kFontUI, kTCuerpo, G::FontStyleRegular);
        G::SolidBrush hb(G::Color(0xd2, 0x82, 0x7d, 0x76));
        g.DrawString(tr(L"Pulsa CONECTAR y me busco la vida: uso tu TONE3000 instalado, o te doy un codigo."),
                     -1, &fHint, G::RectF(sx + 24.0f, sy + 42.0f, 700.0f, 15.0f), nullptr, &hb);
        g.DrawString(tr(L"  Ctrl+V solo si quieres pegar una clave secreta o un token."),
                     -1, &fHint, G::RectF(sx + 24.0f, sy + 57.0f, 700.0f, 15.0f), nullptr, &hb);
        const bool foc = (g_t3kFocus == 2);
        G::SolidBrush fb(G::Color(255, 0x0a, 0x0d, 0x11));
        G::Pen fp(foc ? kAcc : G::Color(0xff, 0x21, 0x1f, 0x21), foc ? 1.6f : 1.2f);
        roundRect(g, sx + 24.0f, sy + 78.0f, (G::REAL)T3K_SHEET_W - 48.0f, 34.0f, 8.0f, &fp, &fb);
        G::Font fk(kFontMono, kTValor, g_t3kKey[0] ? G::FontStyleRegular : G::FontStyleItalic);
        G::SolidBrush kb(g_t3kKey[0] ? G::Color(255, 0xe8, 0xee, 0xf4) : G::Color(0x96, 0x6e, 0x69, 0x63));
        // El campo se recorta: un token JWT mide 800-2000 caracteres y el texto se
        // dibujaba entero, sin clip, en una caja de 688: se salia de la hoja y se
        // montaba sobre la ayuda de abajo, y el cursor se iba a 1393px. Se pinta la
        // COLA que cabe (de un token lo que importa es el final) y el cursor se
        // queda dentro del campo.
        const G::REAL fx = sx + 36.0f, fw = (G::REAL)T3K_SHEET_W - 72.0f;
        G::StringFormat nmf; nmf.SetFormatFlags(G::StringFormatFlagsNoWrap);
        const G::RectF mRect(0, 0, 4000, 50);
        G::RectF mOut;
        auto txtW = [&](const std::wstring& s) -> G::REAL {   // devuelve el ANCHO
            g.MeasureString(s.c_str(), -1, &fk, mRect, &nmf, &mOut);
            return mOut.Width;
        };
        std::wstring show = g_t3kKey[0] ? std::wstring(g_t3kKey) : std::wstring(tr(L"clave o token"));
        if (g_t3kKey[0]) {
            const G::REAL fullW = txtW(show);
            if (fullW > fw) {
                size_t skip = (size_t)((fullW - fw)/7.2f) + 1;   // ~7,2px por glifo a 11pt
                if (skip < show.size()) show = show.substr(skip);
                while (show.size() > 1 && txtW(show) > fw) show.erase(show.begin());
            }
        }
        g.DrawString(show.c_str(), -1, &fk, G::RectF(fx, sy + 86.0f, fw, 20.0f), &nmf, &kb);
        if (foc) {   // cursor de escritura: pegado al final del texto VISIBLE
            const G::REAL tw = txtW(show);
            const G::REAL cxp = fx + tw + 2.0f;
            if (cxp < fx + fw - 2.0f) {
                G::SolidBrush cb2(G::Color(220, 0xe8, 0xee, 0xf4));
                g.FillRectangle(&cb2, cxp, sy + 86.0f, 1.5f, 18.0f);
            }
        }
        // Que va a pasar en cada caso. Es lo que quita el "invalid token" a ciegas:
        // en vacio funciona siempre (la app lleva su clave publicable); pegando, solo
        // vale una clave secreta viva o un token.
        G::Font f2(kFontUI, kTCuerpo, G::FontStyleRegular);
        G::SolidBrush b2(G::Color(0x8c, 0x82, 0x7d, 0x76));
        g.DrawString(tr(L"Vacio: cojo la sesion de tu TONE3000 instalado (y si no, te doy"), -1, &f2,
                     G::RectF(sx + 24.0f, sy + 126.0f, 700.0f, 18.0f), nullptr, &b2);
        g.DrawString(tr(L"un codigo). Con clave secreta o token pegados: entra directo."), -1, &f2,
                     G::RectF(sx + 24.0f, sy + 142.0f, 700.0f, 18.0f), nullptr, &b2);
        const bool hov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_KEYGO;
        t3kButton(g, sx + t3kBtnGoX(), sy + T3K_BTN_Y, T3K_BTN_GO_W, T3K_BTN_H,
                  L"CONECTAR", hov, kAcc, true);
    }
    const bool hovC = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_KEYCANCEL;
    t3kButton(g, sx + t3kBtnCancelX(), sy + T3K_BTN_Y, T3K_BTN_CANCEL_W, T3K_BTN_H, L"CANCELAR", hovC,
              G::Color(0xff, 0x82, 0x7d, 0x76), false);
}
static void t3kDraw(G::Graphics& g)
{
    const G::REAL px = T3K_X, py = T3K_Y, pw = T3K_W, ph = T3K_H;
    G::SolidBrush dim(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
    g.FillRectangle(&dim, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
    glassPanel(g, px, py, pw, ph, kRModal, true);

    // --- Marca COMPLETA de TONE3000 (su requisito de diseno) ---------------
    {
        G::Font fB(kFontTec, kTMarca, G::FontStyleBold);
        const G::REAL x1 = drawSpaced(g, L"TONE", fB, kAcc, px + T3K_PAD, py + 8.0f, 1.4f);
        drawSpaced(g, L"3000", fB, kAcc, x1, py + 8.0f, 1.4f);
        G::Font fS(kFontTec, kTPico, G::FontStyleRegular);
        drawSpaced(g, L"PERFILES NEURALES \x00B7 CAPTURAS .NAM", fS, G::Color(0xaa, 0x82, 0x7d, 0x76),
                   px + T3K_PAD + 2.0f, py + 41.0f, 0.9f);   // el lema respira bajo la marca
    }
    // Cerrar
    {
        const G::REAL bx = px + pw - 46.0f, by = py + 13.0f;
        const bool hov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_CLOSE;
        G::SolidBrush cb(hov ? G::Color(0xff, 0x17, 0x16, 0x18) : G::Color(255, 0x18, 0x1d, 0x23));
        G::Pen cp(hov ? kHot : G::Color(0xff, 0x2e, 0x2c, 0x2c), 1.2f);
        roundRect(g, bx, by, 32.0f, 32.0f, 8.0f, &cp, &cb);
        G::Font fx(kFontUI, kTCuerpo, G::FontStyleBold);
        G::SolidBrush tb(hov ? G::Color(0xff, 0x82, 0x7d, 0x76) : G::Color(0xd2, 0x9c, 0x98, 0x90));
        G::StringFormat sfx;
        sfx.SetAlignment(G::StringAlignmentCenter); sfx.SetLineAlignment(G::StringAlignmentCenter);
        g.DrawString(L"\x2715", -1, &fx, G::RectF(bx, by, 32.0f, 32.0f), &sfx, &tb);
        pressVeil(g, bx, by, 32.0f, 32.0f, 8.0f);
    }

    // --- Buscador + acciones de la derecha ---------------------------------
    std::wstring status;
    { std::lock_guard<std::mutex> lk(g_t3kMutex); status = g_t3kStatus; }
    {
        const G::REAL sx = (G::REAL)t3kSearchX(), sy = py + T3K_SEARCH_Y;
        const bool foc = (g_t3kFocus == 1);
        G::SolidBrush fb(G::Color(255, 0x0a, 0x0d, 0x11));
        G::Pen fp(foc ? kAcc : G::Color(0xff, 0x21, 0x1f, 0x21), foc ? 1.6f : 1.2f);
        roundRect(g, sx, sy, 372.0f, 34.0f, 8.0f, &fp, &fb);
        G::Font fq(kFontUI, kTCuerpo, g_t3kQuery[0] ? G::FontStyleRegular : G::FontStyleItalic);
        G::SolidBrush qb(g_t3kQuery[0] ? G::Color(255, 0xe8, 0xee, 0xf4) : G::Color(0x96, 0x6e, 0x69, 0x63));
        const WCHAR* ph2 = g_t3kConnected ? L"buscar (fender, jcm800, sd-1...)" : L"conectar para buscar";
        g.DrawString(tr(g_t3kQuery[0] ? g_t3kQuery : ph2), -1, &fq,
                     G::RectF(sx + 12.0f, sy + 8.0f, 340.0f, 20.0f), nullptr, &qb);
        if (foc) {
            G::SolidBrush cb2(G::Color(220, 0xe8, 0xee, 0xf4));
            g.FillRectangle(&cb2, sx + 13.0f + (G::REAL)wcslen(g_t3kQuery)*7.2f, sy + 8.0f, 1.5f, 18.0f);
        }
        const bool goHov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_GO;
        t3kButton(g, sx + 380.0f, sy, 96.0f, 34.0f, L"BUSCAR", goHov, kAcc, false);
        // BIBLIOTECA: los .t3kpreset que el usuario ya tiene en su TONE3000. Se
        // leen del disco, asi que funciona sin sesion y sin internet.
        const bool libHov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_LIB;
        t3kButton(g, sx + 480.0f, sy, 90.0f, 34.0f, g_t3kLocalMode ? L"BIBLIOTECA \x2713" : L"BIBLIOTECA",
                  libHov, kAcc, g_t3kLocalMode);
        // estado de la conexion: CONECTAR o el usuario de la sesion
        const G::REAL cx2 = px + pw - T3K_PAD - 150 - 8 - 120;
        if (g_t3kConnected) {
            G::Font fu(kFontTec, kTRotulo, G::FontStyleBold);
            G::SolidBrush ub(kFTxt2);
            std::wstring who = g_t3kUser.empty() ? std::wstring(tr(L"CONECTADO")) : (L"\x2713 " + u8w(g_t3kUser));
            g.DrawString(who.c_str(), -1, &fu, G::RectF(cx2, sy + 10.0f, 120.0f, 20.0f), nullptr, &ub);
        } else {
            const bool cHov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_CONN;
            t3kButton(g, cx2, sy, 120.0f, 34.0f, L"CONECTAR", cHov, kAcc, false);
        }
        const bool vHov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_VER;
        t3kButton(g, px + pw - T3K_PAD - 150.0f, sy, 150.0f, 34.0f, L"VER TONE3000", vHov,
                  kAcc, false);
    }

    // --- Los siete chips de tipo de equipo (+ TODOS) ------------------------
    for (int i = 0; i < 8; ++i) {
        const G::REAL cx = (G::REAL)t3kChipX(i), cy = py + T3K_CHIPS_Y;
        const bool on = (i == 0) ? (g_t3kGear < 0) : (g_t3kGear == i - 1);
        const bool hov = t3kHit(g_mouseX, g_mouseY) == (T3K_HIT_CHIP0 + i);
        const WCHAR* lbl = (i == 0) ? L"Todos" : kT3kGears[i - 1].label;
        const G::Color ac = (i == 0) ? G::Color(0xff, 0x82, 0x7d, 0x76) : t3kAccent(lbl);
        G::SolidBrush cb(on ? G::Color(255, (BYTE)(ac.GetR()/5), (BYTE)(ac.GetG()/5), (BYTE)(ac.GetB()/5))
                            : G::Color(255, 0x12, 0x17, 0x1d));
        G::Pen cp(on ? G::Color(255, ac.GetR(), ac.GetG(), ac.GetB())
                     : (hov ? G::Color(0xff, 0x2e, 0x2c, 0x2c) : G::Color(0xff, 0x1c, 0x1b, 0x1d)), on ? 1.6f : 1.2f);
        roundRect(g, cx, cy, (G::REAL)T3K_CHIP_W, (G::REAL)T3K_CHIP_H, 15.0f, &cp, &cb);
        drawTxFitC(g, lbl, G::FontStyleBold, on ? G::Color(255, 0xf2, 0xf6, 0xfa) : G::Color(0xc8, 0x82, 0x7d, 0x76),
                       cx + T3K_CHIP_W*0.5f, cy + 8.0f, 0.4f, T3K_CHIP_W - 18.0f, TxRotulo);
        pressVeil(g, cx, cy, (G::REAL)T3K_CHIP_W, (G::REAL)T3K_CHIP_H, 15.0f);
    }

    // --- Tarjetas ----------------------------------------------------------
    const int nCards = t3kCardCount();
    if (nCards == 0) {
        G::Font fn(kFontUI, kTValor, G::FontStyleRegular);
        G::SolidBrush nb(G::Color(0xbe, 0x82, 0x7d, 0x76));
        G::StringFormat sfc;
        sfc.SetAlignment(G::StringAlignmentCenter);
        const WCHAR* msg = g_t3kBusy ? L"CONSULTANDO TONE3000..."
                                     : L"sin resultados: prueba una lista o conecta para buscar";
        g.DrawString(tr(msg), -1, &fn, G::RectF(px + 40.0f, py + T3K_CARDS_Y + 120.0f, pw - 80.0f, 24.0f), &sfc, &nb);
    }
    for (int i = 0; i < nCards; ++i) {
        int cxi, cyi; t3kCardXY(i, cxi, cyi);
        const G::REAL cx = (G::REAL)cxi, cy = (G::REAL)cyi;
        const T3kItem& it = g_t3kItems[i];
        const bool hov = t3kHit(g_mouseX, g_mouseY) == (T3K_HIT_CARD0 + i);
        const bool sel = (g_t3kSel == i);
        const G::Color ac = t3kAccent(it.gear.c_str());
        G::SolidBrush cb(hov ? kFSurf3 : kFSurf2);
        G::Pen cp((hov || sel) ? G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()) : kFLine,
                  (hov || sel) ? 1.6f : 1.0f);
        roundRect(g, cx, cy, (G::REAL)T3K_CARD_W, (G::REAL)T3K_CARD_H, 10.0f, &cp, &cb);
        // foto (recorte cuadrado centrado)
        G::SolidBrush phb(G::Color(255, 0x18, 0x1c, 0x22));
        roundRect(g, cx + 10.0f, cy + 10.0f, 96.0f, 96.0f, 7.0f, nullptr, &phb);
        if (G::Bitmap* bm = t3kImgGet(it.id)) {
            const G::REAL iw = (G::REAL)bm->GetWidth(), ih = (G::REAL)bm->GetHeight();
            const G::REAL s = (iw < ih ? iw : ih);
            g.DrawImage(bm, G::RectF(cx + 10.0f, cy + 10.0f, 96.0f, 96.0f),
                        (iw - s)*0.5f, (ih - s)*0.5f, s, s, G::UnitPixel);
        } else {
            drawTxFitC(g, L"T3K", G::FontStyleBold, G::Color(90, 0x6e, 0x69, 0x63),
                           cx + 58.0f, cy + 46.0f, 2.0f, 80.0f, TxTitulo);
        }
        const G::REAL tx = cx + 116.0f;
        t3kWrapTitle(g, it.title, tx, cy + 9.0f, 148.0f);
        // tipo de equipo + formato
        {
            const G::REAL wch = 96.0f, hch = 19.0f;
            G::SolidBrush gb(G::Color(255, (BYTE)(ac.GetR()/6), (BYTE)(ac.GetG()/6), (BYTE)(ac.GetB()/6)));
            G::Pen gp(G::Color(200, ac.GetR(), ac.GetG(), ac.GetB()), 1.0f);
            roundRect(g, tx, cy + 42.0f, wch, hch, 4.0f, &gp, &gb);
            drawTxFitC(g, it.gear.c_str(), G::FontStyleBold, G::Color(255, 0xe4, 0xea, 0xf0),
                           tx + wch*0.5f, cy + 45.0f, 0.2f, wch - 10.0f, TxPico);
            const G::REAL bx2 = tx + wch + 6.0f;
            G::SolidBrush nb(G::Color(0xff, 0x1c, 0x1b, 0x1d));
            G::Pen np(G::Color(0xff, 0x2e, 0x2c, 0x2c), 1.0f);
            roundRect(g, bx2, cy + 42.0f, 46.0f, hch, 4.0f, &np, &nb);
            drawTxFitC(g, L"NAM", G::FontStyleBold, G::Color(0xdc, 0x82, 0x7d, 0x76),
                           bx2 + 23.0f, cy + 45.0f, 0.8f, 40.0f, TxPico);
        }
        // autor (con su marca de verificado) + antiguedad + descargas
        {
            std::wstring who = it.user.empty() ? std::wstring(L"-") : it.user;
            if (it.verified) who = L"\x2713 " + who;
            const std::wstring age = t3kAge(it.ageDays);
            if (!age.empty()) who += L" \x00B7 " + age;      // como la web: "Â· 1 sem"
            drawTxFit(g, who.c_str(), G::FontStyleBold, G::Color(0xd2, 0x9c, 0x98, 0x90),
                          tx, cy + 66.0f, 0.2f, 148.0f, TxPico);
            G::Font fd(kFontUI, kTCuerpo, G::FontStyleRegular);
            G::SolidBrush db(G::Color(0xbe, 0x82, 0x7d, 0x76));
            // Una tarjeta local no tiene descargas ni arquitecturas A1/A2 (son de
            // la API): dice lo que SI es, cuantos bloques trae y de donde sale.
            if (g_t3kLocalMode) {
                WCHAR bl[64];
                swprintf_s(bl, tr(L"%d bloque%s  \xB7  .t3kpreset"), it.models, it.models == 1 ? L"" : L"s");
                g.DrawString(bl, -1, &fd, G::RectF(tx, cy + 84.0f, 148.0f, 16.0f), nullptr, &db);
                drawTxFit(g, L"carga las capturas de su cadena", G::FontStyleRegular,
                              G::Color(0xa0, 0x82, 0x7d, 0x76), tx, cy + 103.0f, 0.1f, 148.0f, TxPico);
            } else {
                std::wstring dl = L"\x2193 " + t3kCount(it.downloads);
                g.DrawString(dl.c_str(), -1, &fd, G::RectF(tx, cy + 84.0f, 148.0f, 16.0f), nullptr, &db);
                // modelos que trae (solo tiene sentido para los tonos de la API)
                WCHAR ml[64];
                swprintf_s(ml, tr(L"%d modelo%s  A1 %d / A2 %d"), it.models, it.models == 1 ? L"" : L"s", it.a1, it.a2);
                drawTxFit(g, ml, G::FontStyleRegular, G::Color(0xa0, 0x82, 0x7d, 0x76),
                              tx, cy + 103.0f, 0.1f, 148.0f, TxPico);
            }
        }
        // llamada a la accion
        {
            const WCHAR* act = hov ? (g_t3kLocalMode ? L"CLIC: CARGAR EN LA CADENA" : L"CLIC: BAJAR Y A\xD1" L"ADIR A LA CADENA")
                                   : (g_t3kLocalMode ? L"preset .t3kpreset" : L"captura .nam");
            drawTxFitC(g, act, G::FontStyleBold,
                           hov ? G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()) : G::Color(0x8c, 0x6e, 0x69, 0x63),
                           cx + T3K_CARD_W*0.5f, cy + 138.0f, 0.6f, T3K_CARD_W - 30.0f, TxRotulo);
        }
    }

    // --- Paginacion + total -------------------------------------------------
    {
        const G::REAL pgX = px + T3K_PAD, pgY = py + T3K_PAGER_Y;
        const int np = t3kPageCount() < 6 ? t3kPageCount() : 6;
        const bool prevHov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_PREV;
        t3kButton(g, pgX, pgY, 28.0f, 28.0f, L"<", prevHov, G::Color(0xff, 0x82, 0x7d, 0x76), false);
        for (int k = 0; k < np; ++k) {
            const G::REAL bx2 = pgX + 34.0f + k*30.0f;
            const bool on = (g_t3kPage == k + 1);
            const bool hov = t3kHit(g_mouseX, g_mouseY) == (T3K_HIT_PAGE0 + k);
            G::SolidBrush pb(on ? G::Color(0xff, 0x21, 0x1f, 0x21) : (hov ? G::Color(0xff, 0x17, 0x16, 0x18) : G::Color(255, 0x11, 0x15, 0x1a)));
            G::Pen pp(on ? kAcc : G::Color(0xff, 0x21, 0x1f, 0x21), 1.2f);
            roundRect(g, bx2, pgY, 26.0f, 28.0f, 6.0f, &pp, &pb);
            WCHAR num[8]; swprintf_s(num, L"%d", k + 1);
            drawTxFitC(g, num, G::FontStyleBold, on ? G::Color(255, 0xf2, 0xf6, 0xfa) : G::Color(0xc8, 0x82, 0x7d, 0x76),
                           bx2 + 13.0f, pgY + 7.0f, 0.0f, 22.0f, TxPico);
        }
        const bool nextHov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_NEXT;
        t3kButton(g, pgX + 34.0f + np*30.0f + 4.0f, pgY, 28.0f, 28.0f, L">", nextHov, G::Color(0xff, 0x82, 0x7d, 0x76), false);
        // filtro de autores verificados (el check de su barra de filtros)
        {
            const G::REAL vx = (G::REAL)t3kVerifX();
            const bool vhov = t3kHit(g_mouseX, g_mouseY) == T3K_HIT_VERIF;
            const G::Color ac(255, 0x5c, 0xe0, 0xb8);
            G::SolidBrush vb(g_t3kVerif ? G::Color(0xff, 0x1c, 0x1b, 0x1d)
                                        : (vhov ? G::Color(0xff, 0x17, 0x16, 0x18) : G::Color(255, 0x11, 0x15, 0x1a)));
            G::Pen vp((g_t3kVerif || vhov) ? G::Color(255, ac.GetR(), ac.GetG(), ac.GetB())
                                           : G::Color(0xff, 0x21, 0x1f, 0x21), 1.2f);
            roundRect(g, vx, pgY, 168.0f, 28.0f, 6.0f, &vp, &vb);
            drawTxFitC(g, L"\x2713 AUTORES VERIFICADOS", G::FontStyleBold,
                           g_t3kVerif ? G::Color(0xff, 0xb7, 0xb1, 0xa7) : G::Color(0xc8, 0x82, 0x7d, 0x76),
                           vx + 84.0f, pgY + 7.0f, 0.4f, 156.0f, TxRotulo);
            pressVeil(g, vx, pgY, 168.0f, 28.0f, 6.0f);
        }
        // Aqui estaba el recuento ("tendencias Â· sin clave", "N tonos"). Entre el
        // boton de autores verificados (acaba en +442) y las pestanyas (empiezan en
        // +474) solo hay 32px: el rotulo se dibujaba ENCIMA del boton. Se ha movido a
        // la linea de estado, que tiene ancho de sobra.
        // Pestanyas de lista: tendencias / nuevos / favoritos / descargados
        const G::Color acT(255, 0x5c, 0xe0, 0xb8);   // el mismo acento del panel
        for (int i = 0; i < 4; ++i) {
            const int lx = t3kListX(i);
            const bool act = (g_t3kList == i);
            const bool hov = (t3kHit(g_mouseX, g_mouseY) == T3K_HIT_LIST0 + i);
            G::SolidBrush lb(act ? G::Color(255, 0x1c, 0x1b, 0x1d)
                                 : (hov ? G::Color(255, 0x17, 0x16, 0x18) : G::Color(255, 0x11, 0x15, 0x1a)));
            G::Pen lp(act ? G::Color(255, acT.GetR(), acT.GetG(), acT.GetB())
                          : G::Color(0xff, 0x21, 0x1f, 0x21), 1.2f);
            roundRect(g, (G::REAL)lx, (G::REAL)pgY, (G::REAL)T3K_LIST_W, (G::REAL)T3K_LIST_H, 6.0f, &lp, &lb);
            drawTxFitC(g, kT3kListNames[i], G::FontStyleBold,
                           act ? G::Color(0xff, 0xb7, 0xb1, 0xa7) : G::Color(0xc8, 0x82, 0x7d, 0x76),
                           lx + T3K_LIST_W*0.5f, pgY + 7.0f, 0.4f, (G::REAL)T3K_LIST_W - 10.0f, TxRotulo);
            pressVeil(g, (G::REAL)lx, (G::REAL)pgY, (G::REAL)T3K_LIST_W, (G::REAL)T3K_LIST_H, 6.0f);
        }
    }

    // --- Linea de estado ----------------------------------------------------
    {
        // El recuento va aqui, en su propio hueco RESERVADO a la derecha del estado:
        // asi el estado no puede llegar hasta el (se ajusta dentro de su mitad) y el
        // recuento no puede quedar fuera. Vivia en la fila de paginacion, donde el
        // hueco entre el boton de verificados y las pestanyas es de 32px y el rotulo
        // se montaba encima del boton.
        WCHAR tot[48]; tot[0] = 0;
        if (!g_t3kItems.empty()) swprintf_s(tot, tr(L"%d tonos"), (int)g_t3kItems.size());
        const G::REAL stW = pw - T3K_PAD*2 - T3K_DISC_RESERVE - (tot[0] ? 88.0f : 0.0f);
        drawTxFit(g, status.empty() ? L"LISTO" : status.c_str(), G::FontStyleBold, G::Color(0xd2, 0x82, 0x7d, 0x76),
                      px + T3K_PAD, py + 530.0f, 0.8f, stW, TxRotulo);
        if (tot[0])
            drawTxFit(g, tot, G::FontStyleRegular, G::Color(0xb4, 0x82, 0x7d, 0x76),
                          px + T3K_PAD + stW + 14.0f, py + 531.0f, 0.4f, 74.0f, TxValor);
        G::Font fh(kFontUI, kTPico, G::FontStyleRegular);
        G::SolidBrush hb(G::Color(0x96, 0x6e, 0x69, 0x63));
        G::StringFormat sfr;
        sfr.SetAlignment(G::StringAlignmentFar);
        g.DrawString(tr(L"los perfiles son de sus autores: se descargan, no se incluyen"), -1, &fh,
                     G::RectF(px + pw - T3K_PAD - 420.0f, py + 532.0f, 420.0f, 18.0f), &sfr, &hb);
    }
    if (g_t3kConnOpen) t3kDrawSheet(g);
}

// ============================================================================
//  Navegador de presets (paso 18): la ventana superpuesta
//  Un solo sitio decide la geometria (brwLayout) y la leen el pintado y el clic,
//  para que no puedan desalinearse. El estado de filtros y orden vive arriba,
//  junto a browseOrder, con el resto del modelo del navegador.
// ============================================================================
static int  g_brwSel    = -1;      // preset resaltado en la lista (indice global)
static int  g_brwScroll = 0;
static bool g_brwFocus  = false;   // el buscador tiene el foco
// La edicion de metadatos del Info pane (paso 18.7): que campo se esta
// escribiendo (0 ninguno) y su borrador. Los tres campos del preset mio son el
// nombre, el autor y el comentario; las etiquetas y el color se pulsan, no se
// escriben.
static int   g_brwEdit    = 0;     // 0 ninguno, 1 nombre, 2 autor, 3 comentario
static WCHAR g_brwEditBuf[192] = L"";

static const int BRW_W = 1200, BRW_H = 720;
static const int BRW_X = (WIN_W - BRW_W) / 2;
static const int BRW_Y = (WIN_H - BRW_H) / 2;
static const int BRW_ROW_H = 44;
static const int BRW_FILT_W = 210;
static const int BRW_LIST_X = BRW_X + 24 + BRW_FILT_W + 20;
static const int BRW_LIST_W = 600;
static const int BRW_INFO_X = BRW_LIST_X + BRW_LIST_W + 20;
static const int BRW_INFO_W = BRW_W - 24 - (BRW_INFO_X - BRW_X);

static const WCHAR* kBrwSortNames[5] = { L"CURADO", L"A-Z", L"Z-A", L"AMPLI", L"RECIENTES" };
static const WCHAR* kBrwFavNames[kFavColors] = { L"SIN COLOR", L"AMBAR", L"ROJO", L"AZUL", L"VERDE" };
// Los cuatro origenes del catalogo. TONE3000 son los .t3kpreset que el usuario
// ya tiene en su cuenta (se leen del disco al abrir el navegador).
static const int    kBrwOrgCount = 4;
static const WCHAR* kBrwOrgNames[kBrwOrgCount] = { L"TODOS", L"FABRICA", L"MIOS", L"TONE3000" };

struct BrwBox {
    RECT fav[kFavColors], org[kBrwOrgCount], chr[kCharCount], amp[kAmpCount], ord[5];
    RECT search, hdr, list, close, info;
    // El Info pane (paso 18.7): las cajas que se PULSAN. Se calculan aqui, con
    // el resto de la geometria, y las leen el pintado y el clic.
    RECT iName, iAuthor, iComment, iTags[kCharCount], iFav;
    // Las fechas (solo en un preset mio) y el boton BORRAR, que bajo del aspa de
    // la fila del sidebar al navegador (paso 19).
    RECT iWhen, iDel;
    int  lblFav, lblOrg, lblChr, lblAmp;
};
static BrwBox g_brw;

// El nombre del caracter que el navegador ENSENA para un preset: el suyo, y si
// no tiene etiqueta (un preset del usuario recien guardado) lo dice asi.
static const WCHAR* presetCharLabel(int idx) {
    if (isT3kPreset(idx)) {
        // En la fila, el segundo dato de un preset de TONE3000 es lo que trae.
        static WCHAR buf[48];
        const t3k::LocalPreset* p = t3kPresetAt(idx);
        const int nb = p ? (int)(p->left.size() + p->right.size()) : 0;
        swprintf_s(buf, tr(L"%d CAPTURA%s"), nb, nb == 1 ? L"" : L"S");
        return buf;
    }
    const unsigned int t = presetTags(idx);
    for (int c = 0; c < kCharCount; ++c) if (t & (1u << c)) return kCharNames[c];
    return L"SIN ETIQUETA";
}

// La fecha de un preset mio, en un formato que se lee de un vistazo
// (AAAA-MM-DD). El 0 es "sin fecha": los presets de fabrica no llevan.
static void brwDateW(long long t, WCHAR* out, int n) {
    if (!out || n <= 0) return;
    if (t <= 0) { wcsncpy_s(out, (size_t)n, L"\x2014", _TRUNCATE); return; }
    __time64_t tt = (__time64_t)t;
    tm lt = {};
    if (_localtime64_s(&lt, &tt) != 0) { wcsncpy_s(out, (size_t)n, L"\x2014", _TRUNCATE); return; }
    swprintf_s(out, (size_t)n, L"%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

static void brwLayout() {
    const int x0 = BRW_X + 24;
    int y = BRW_Y + 76;
    const int cwid = (BRW_FILT_W - 8) / 2;
    g_brw.lblFav = y; y += 20;
    for (int i = 0; i < kFavColors; ++i) { g_brw.fav[i] = RECT{ x0, y, x0 + BRW_FILT_W, y + 26 }; y += 26; }
    y += 6;
    // ORIGEN en DOS columnas: con cuatro orígenes (TODOS/FABRICA/MIOS/TONE3000) en
    // una sola columna la lista se salia del panel por abajo (lo caza la prueba T6).
    g_brw.lblOrg = y; y += 20;
    for (int i = 0; i < kBrwOrgCount; ++i) {
        const int col = i % 2, row = i / 2;
        const int bx = x0 + col*(cwid + 8), by = y + row*28;
        g_brw.org[i] = RECT{ bx, by, bx + cwid, by + 26 };
    }
    y += ((kBrwOrgCount + 1)/2)*28 + 6;
    g_brw.lblChr = y; y += 20;
    for (int i = 0; i < kCharCount; ++i) {
        const int col = i % 2, row = i / 2;
        const int bx = x0 + col*(cwid + 8), by = y + row*28;
        g_brw.chr[i] = RECT{ bx, by, bx + cwid, by + 26 };
    }
    y += ((kCharCount + 1)/2)*28 + 6;
    g_brw.lblAmp = y; y += 20;
    for (int i = 0; i < kAmpCount; ++i) {
        const int col = i % 2, row = i / 2;
        const int bx = x0 + col*(cwid + 8), by = y + row*28;
        g_brw.amp[i] = RECT{ bx, by, bx + cwid, by + 26 };
    }
    const int cx = BRW_LIST_X, cw = BRW_LIST_W;
    g_brw.search = RECT{ cx, BRW_Y + 78, cx + cw, BRW_Y + 78 + 34 };
    const int sy = BRW_Y + 124;
    for (int i = 0; i < 5; ++i) {
        const int sw = 96;
        g_brw.ord[i] = RECT{ cx + i*(sw + 6), sy, cx + i*(sw + 6) + sw, sy + 26 };
    }
    const int hy = sy + 36;
    g_brw.hdr  = RECT{ cx, hy, cx + cw, hy + 22 };
    g_brw.list = RECT{ cx, hy + 26, cx + cw, BRW_Y + BRW_H - 52 };
    g_brw.close = RECT{ BRW_X + BRW_W - 52, BRW_Y + 14, BRW_X + BRW_W - 20, BRW_Y + 46 };
    g_brw.info  = RECT{ BRW_INFO_X, BRW_Y + 78, BRW_INFO_X + BRW_INFO_W, BRW_Y + BRW_H - 52 };
    // El Info pane, de arriba abajo, en el mismo orden en que se pinta: el
    // rotulo, el nombre, la linea de ampli/caracter/origen, el autor, el
    // comentario, las etiquetas y el color de favorito. Las cajas que se pulsan
    // salen de esta misma cuenta.
    {
        const int il = g_brw.info.left, ir = g_brw.info.right;
        int y = g_brw.info.top + 22;                       // debajo de "INFORMACION"
        g_brw.iName    = RECT{ il, y, ir, y + 26 }; y += 32;
        y += 26;                                           // la linea de ampli/caracter/origen
        y += 14;                                           // el filete
        y += 20;                                           // "AUTOR"
        g_brw.iAuthor  = RECT{ il, y, ir, y + 24 }; y += 32;
        y += 20;                                           // "COMENTARIO"
        g_brw.iComment = RECT{ il, y, ir, y + 66 }; y += 82;
        y += 14;                                           // el filete
        y += 22;                                           // "ETIQUETAS"
        const int tw = (ir - il - 6) / 2;
        for (int c = 0; c < kCharCount; ++c) {
            const int col = c % 2, row = c / 2;
            const int bx = il + col*(tw + 6), by = y + row*30;
            g_brw.iTags[c] = RECT{ bx, by, bx + tw, by + 26 };
        }
        y += ((kCharCount + 1)/2)*30 + 6;
        y += 22;                                           // "FAVORITO"
        g_brw.iFav = RECT{ il, y, ir, y + 24 };
        y += 32;
        g_brw.iWhen = RECT{ il, y, ir, y + 18 };           // "CREADO ... USADO ..."
        // BORRAR: al pie del Info pane, pegado al canto inferior. Es una accion de
        // la FICHA (y solo la tienen los presets mios), asi que vive con ella y no
        // en una fila del catalogo: era el aspa de la fila del sidebar, que se
        // retiro en el paso 19. Se pulsa dos veces.
        g_brw.iDel = RECT{ il, g_brw.info.bottom - 34, il + 150, g_brw.info.bottom - 6 };
    }
}
static RECT brwRowRect(int vi) {
    RECT r = g_brw.list;
    r.top += vi*BRW_ROW_H; r.bottom = r.top + BRW_ROW_H;
    return r;
}
static int brwVisRows() { return (g_brw.list.bottom - g_brw.list.top) / BRW_ROW_H; }
// La cajita del punto de favorito de una fila: ahi el clic cicla el COLOR en vez
// de cargar el preset (es el gesto de las colecciones de Guitar Rig).
static RECT brwRowDot(const RECT& r) {
    return RECT{ r.left + 6, r.top + 10, r.left + 30, r.top + 34 };
}

// Texto de una caja, centrado en vertical por la medida de su papel.
static void brwTextIn(G::Graphics& g, const RECT& r, const WCHAR* s, G::FontStyle st,
                      G::Color c, TxRole role, G::REAL sp = 0.6f, G::REAL pad = 8.0f) {
    const G::REAL y = (G::REAL)r.top + ((G::REAL)(r.bottom - r.top) - kTx[role].pt) * 0.5f;
    drawTxFit(g, s, st, c, (G::REAL)r.left + pad, y, sp, (G::REAL)(r.right - r.left) - 2.0f*pad, role);
}
// Una pastilla de filtro: encendida = ambar (esta pulsada), apagada = tono liso.
static void brwChip(G::Graphics& g, const RECT& r, const WCHAR* label, bool on) {
    const G::REAL w = (G::REAL)(r.right - r.left), h = (G::REAL)(r.bottom - r.top);
    const bool hov = rectHas(r, g_mouseX, g_mouseY);
    if (on || hov) {
        flatPillBody(g, (G::REAL)r.left, (G::REAL)r.top, w, h, hov && !on, on, kAcc);
    } else {
        G::SolidBrush bg(kFSurf2); G::Pen bp(kFLine, 1.0f);
        roundRect(g, (G::REAL)r.left, (G::REAL)r.top, w, h, kRPill, &bp, &bg);
    }
    brwTextIn(g, r, label, G::FontStyleBold, (on || hov) ? kFTxt : kFTxt2, TxRotulo, 0.4f, 4.0f);
}
// Una fila de la columna de filtros (con punto de color opcional).
static void brwFilterRow(G::Graphics& g, const RECT& r, const WCHAR* label, bool on, int dot) {
    const G::REAL w = (G::REAL)(r.right - r.left), h = (G::REAL)(r.bottom - r.top);
    const bool hov = rectHas(r, g_mouseX, g_mouseY);
    if (on || hov) {
        G::SolidBrush bg(on ? kFSurf3 : kFSurf2);
        G::Pen bp(kFLine, 1.0f);
        roundRect(g, (G::REAL)r.left, (G::REAL)r.top, w, h, 6.0f, on ? &bp : nullptr, &bg);
    }
    G::REAL tx = (G::REAL)(r.left + 10);
    if (dot >= 0) {
        const G::REAL cy = (G::REAL)(r.top + (r.bottom - r.top)/2);
        if (dot == 0) {                       // sin color: circulo hueco
            G::Pen dp(kFTxt3, 1.2f);
            g.DrawEllipse(&dp, (G::REAL)(r.left + 9), cy - 4.5f, 9.0f, 9.0f);
        } else {
            G::SolidBrush db(favColorAt(dot));
            g.FillEllipse(&db, (G::REAL)(r.left + 9), cy - 4.5f, 9.0f, 9.0f);
        }
        tx = (G::REAL)(r.left + 26);
    }
    brwTextIn(g, r, label, G::FontStyleBold, on ? kFTxt : kFTxt2, TxRotulo, 0.6f, tx - r.left);
}

// Un campo de texto del Info pane. Si se esta escribiendo (g_brwEdit == que) se
// pinta el borrador sobre un tono liso con su cursor; si no, el valor, y con el
// raton encima un tono mas. Solo los presets del usuario tienen ficha donde
// escribirlo: en los de fabrica el campo esta de lectura.
static void brwField(G::Graphics& g, const RECT& r, const WCHAR* val, int que, bool user) {
    const bool editing = (g_brwEdit == que);
    const bool hov = user && rectHas(r, g_mouseX, g_mouseY);
    if (editing) {
        G::SolidBrush bg(kFBg); G::Pen bp(kAcc, 1.0f);
        roundRect(g, (G::REAL)r.left, (G::REAL)r.top, (G::REAL)(r.right - r.left),
                  (G::REAL)(r.bottom - r.top), 6.0f, &bp, &bg);
    } else if (hov) {
        G::SolidBrush hb(kFSurf2);
        roundRect(g, (G::REAL)r.left, (G::REAL)r.top, (G::REAL)(r.right - r.left),
                  (G::REAL)(r.bottom - r.top), 6.0f, nullptr, &hb);
    }
    const bool has = (val && val[0]);
    const WCHAR* show = editing ? g_brwEditBuf : (has ? val : L"\x2014");
    const G::Color c = editing ? kFTxt : (has ? kFTxt2 : kFTxt3);
    const G::REAL y = (G::REAL)r.top + ((G::REAL)(r.bottom - r.top) - kTx[TxCuerpo].pt)*0.5f;
    const G::REAL end = drawTxFit(g, show, G::FontStyleRegular, c, (G::REAL)r.left + 6.0f, y, 0.5f,
                                  (G::REAL)(r.right - r.left) - 14.0f, TxCuerpo);
    if (editing) {
        G::Pen cp(kAcc, 1.2f);
        g.DrawLine(&cp, end + 2.0f, (G::REAL)r.top + 5.0f, end + 2.0f, (G::REAL)r.bottom - 5.0f);
    }
}

static void brwPaint(G::Graphics& g) {
    brwLayout();
    int ord[kMaxListAll];
    const int n = browseOrder(ord);
    const int vis = brwVisRows();
    if (g_brwScroll > n - vis) g_brwScroll = (n > vis) ? n - vis : 0;
    if (g_brwScroll < 0) g_brwScroll = 0;

    G::SolidBrush dim(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
    g.FillRectangle(&dim, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
    flatPanel(g, (G::REAL)BRW_X, (G::REAL)BRW_Y, (G::REAL)BRW_W, (G::REAL)BRW_H, kRModal, true);

    // --- cabecera ---
    drawTxFit(g, L"PRESETS", G::FontStyleBold, kFTxt, (G::REAL)(BRW_X + 30), (G::REAL)(BRW_Y + 16), 1.4f, 220.0f, TxMarca);
    {
        WCHAR sub[80];
        swprintf_s(sub, tr(L"NAVEGADOR  \x00b7  %d DE %d"), n, presetTotalAll());
        drawTxFit(g, sub, G::FontStyleRegular, kFTxt3, (G::REAL)(BRW_X + 30 + 148), (G::REAL)(BRW_Y + 23), 0.8f, 420.0f, TxRotulo);
    }
    { G::Pen ul(kFLine, 1.0f); g.DrawLine(&ul, (G::REAL)(BRW_X + 30), (G::REAL)(BRW_Y + 56), (G::REAL)(BRW_X + BRW_W - 30), (G::REAL)(BRW_Y + 56)); }
    {
        const bool hov = rectHas(g_brw.close, g_mouseX, g_mouseY);
        icClose(g, (G::REAL)(g_brw.close.left + 16), (G::REAL)(g_brw.close.top + 16), 8.0f, hov ? kFTxt : kFTxt3);
    }

    // --- columna FILTROS ---
    drawTxFit(g, L"FAVORITO", G::FontStyleBold, kFTxt3, (G::REAL)(BRW_X + 24), (G::REAL)g_brw.lblFav, 1.0f, 200.0f, TxRotulo);
    for (int i = 0; i < kFavColors; ++i)
        brwFilterRow(g, g_brw.fav[i], kBrwFavNames[i], g_brwColor == (i == 0 ? -1 : i), i);
    drawTxFit(g, L"ORIGEN", G::FontStyleBold, kFTxt3, (G::REAL)(BRW_X + 24), (G::REAL)g_brw.lblOrg, 1.0f, 200.0f, TxRotulo);
    for (int i = 0; i < kBrwOrgCount; ++i)
        brwFilterRow(g, g_brw.org[i], kBrwOrgNames[i], g_brwOrigin == i - 1, -1);
    drawTxFit(g, L"CARACTER", G::FontStyleBold, kFTxt3, (G::REAL)(BRW_X + 24), (G::REAL)g_brw.lblChr, 1.0f, 200.0f, TxRotulo);
    for (int i = 0; i < kCharCount; ++i)
        brwChip(g, g_brw.chr[i], kCharNames[i], (g_brwChar & (1u << i)) != 0);
    drawTxFit(g, L"AMPLIFICADOR", G::FontStyleBold, kFTxt3, (G::REAL)(BRW_X + 24), (G::REAL)g_brw.lblAmp, 1.0f, 200.0f, TxRotulo);
    for (int i = 0; i < kAmpCount; ++i)
        brwChip(g, g_brw.amp[i], kAmpNames[i], (g_brwAmp & (1u << i)) != 0);

    // --- columna RESULTADOS ---
    {
        const RECT& sr = g_brw.search;
        G::SolidBrush bg(kFBg);
        G::Pen bp(g_brwFocus ? kAcc : kFLine, 1.0f);
        roundRect(g, (G::REAL)sr.left, (G::REAL)sr.top, (G::REAL)(sr.right - sr.left), (G::REAL)(sr.bottom - sr.top), 8.0f, &bp, &bg);
        const WCHAR* ph = g_brwText[0] ? g_brwText : L"Buscar por nombre, ampli, etiqueta o autor";
        drawTxFit(g, ph, G::FontStyleRegular, g_brwText[0] ? kFTxt : kFTxt3,
                  (G::REAL)sr.left + 12, (G::REAL)sr.top + 8, 0.5f, (G::REAL)(sr.right - sr.left) - 44.0f, TxCuerpo);
        if (g_brwFocus) {   // cursor
            G::REAL pt, sp;
            WCHAR t[64]; wcsncpy_s(t, ph, _TRUNCATE);
            fitSpacedFont(g, t, G::FontStyleRegular, 0.5f, 100000.0f, kTx[TxCuerpo].pt, kTx[TxCuerpo].min, pt, sp, kTx[TxCuerpo].fam);
            G::Font ff(kTx[TxCuerpo].fam, pt, G::FontStyleRegular);
            const G::REAL cxp = (G::REAL)sr.left + 12.0f + measureSpaced(g, t, ff, sp) + 1.0f;
            G::Pen cp(kAcc, 1.2f);
            g.DrawLine(&cp, cxp, (G::REAL)sr.top + 8.0f, cxp, (G::REAL)(sr.bottom) - 8.0f);
        }
    }
    for (int i = 0; i < 5; ++i) brwChip(g, g_brw.ord[i], kBrwSortNames[i], g_brwSort == i);
    {
        // cabecera de columnas
        const RECT& hr = g_brw.hdr;
        G::Pen hp(kFLine, 1.0f);
        g.DrawLine(&hp, (G::REAL)hr.left, (G::REAL)(hr.bottom - 1), (G::REAL)hr.right, (G::REAL)(hr.bottom - 1));
        brwTextIn(g, hr, L"PRESET", G::FontStyleBold, kFTxt3, TxRotulo, 0.8f, 30.0f);
        {
            G::REAL y = (G::REAL)hr.top + ((G::REAL)(hr.bottom - hr.top) - kTx[TxRotulo].pt) * 0.5f;
            drawTxFit(g, L"AMPLIFICADOR", G::FontStyleBold, kFTxt3, (G::REAL)hr.right - 260.0f, y, 0.8f, 160.0f, TxRotulo);
            drawTxFit(g, L"ORIGEN", G::FontStyleBold, kFTxt3, (G::REAL)hr.right - 90.0f, y, 0.8f, 80.0f, TxRotulo);
        }
    }
    for (int vi = 0; vi < vis; ++vi) {
        const int k = g_brwScroll + vi;
        if (k >= n) break;
        const int idx = ord[k];
        const RECT r = brwRowRect(vi);
        const bool sel = (idx == g_brwSel);
        const bool hov = rectHas(r, g_mouseX, g_mouseY);
        const bool user = isUserPreset(idx);
        const bool t3kRow = isT3kPreset(idx);
        if (sel || hov) {
            G::SolidBrush bg(kFSurf2);
            g.FillRectangle(&bg, (G::REAL)r.left, (G::REAL)r.top, (G::REAL)(r.right - r.left), (G::REAL)BRW_ROW_H);
        }
        if (sel) {
            G::SolidBrush ab(kAcc);
            g.FillRectangle(&ab, (G::REAL)r.left, (G::REAL)r.top, 2.0f, (G::REAL)BRW_ROW_H);
        }
        { G::Pen sp(kFLine, 1.0f); g.DrawLine(&sp, (G::REAL)r.left, (G::REAL)(r.bottom - 1), (G::REAL)r.right, (G::REAL)(r.bottom - 1)); }
        // punto de favorito (el clic aqui cicla el color)
        {
            const int fc = presetFavColor(idx);
            const G::REAL dx = (G::REAL)r.left + 12, dy = (G::REAL)r.top + 17;
            if (fc) { G::SolidBrush db(favColorAt(fc)); g.FillEllipse(&db, dx, dy, 10.0f, 10.0f); }
            else    { G::Pen dp(G::Color(88, 0x8b, 0x87, 0x81), 1.2f); g.DrawEllipse(&dp, dx, dy, 10.0f, 10.0f); }
        }
        WCHAR nm[128]; presetNameW(idx, nm, 128);
        drawTxFit(g, nm, G::FontStyleBold, kFTxt, (G::REAL)r.left + 30, (G::REAL)r.top + 5, 0.6f, (G::REAL)(r.right - r.left) - 320.0f, TxNombre);
        WCHAR sub[160];
        const int gp = presetGroup(idx);
        swprintf_s(sub, L"%s  \x00b7  %s", (gp >= 0) ? tr(kAmpNames[gp]) : tr(L"SIN AMPLI"),
                       tr(presetCharLabel(idx)));
        drawTxFit(g, sub, G::FontStyleRegular, kFTxt3, (G::REAL)r.left + 30, (G::REAL)r.top + 25, 0.4f, (G::REAL)(r.right - r.left) - 320.0f, TxRotulo);
        {
            G::REAL y = (G::REAL)r.top + ((G::REAL)BRW_ROW_H - kTx[TxRotulo].pt) * 0.5f;
            drawTxFit(g, (gp >= 0) ? kAmpNames[gp] : L"\x2014", G::FontStyleRegular, kFTxt2, (G::REAL)r.right - 260.0f, y, 0.4f, 160.0f, TxRotulo);
        }
        // pastilla de origen
        {
            RECT br = { r.right - 74, r.top + 12, r.right - 12, r.top + 32 };
            const WCHAR* lbl = t3kRow ? L"T3K" : (user ? L"MIO" : L"FABRICA");
            G::SolidBrush bg(kFSurf3); G::Pen bp(kFLine, 1.0f);
            roundRect(g, (G::REAL)br.left, (G::REAL)br.top, (G::REAL)(br.right - br.left), (G::REAL)(br.bottom - br.top), 5.0f, &bp, &bg);
            brwTextIn(g, br, lbl, G::FontStyleBold, kFTxt2, TxRotulo, 0.6f, 0.0f);
        }
    }
    if (n == 0) {
        brwTextIn(g, g_brw.list, L"NINGUN PRESET CUMPLE LOS FILTROS", G::FontStyleBold, kFTxt3, TxCuerpo, 0.6f, 12.0f);
    }

    // --- columna INFORMACION ---
    {
        const RECT& ir = g_brw.info;
        const int idx = (g_brwSel >= 0) ? g_brwSel : g_presetIdx;
        const bool user = isUserPreset(idx);
        drawTxFit(g, L"INFORMACION", G::FontStyleBold, kFTxt3, (G::REAL)ir.left, (G::REAL)ir.top, 1.0f, 200.0f, TxRotulo);
        // el nombre (editable)
        {
            WCHAR nm[128]; presetNameW(idx, nm, 128);
            if (g_brwEdit == 1) brwField(g, g_brw.iName, nm, 1, user);
            else {
                const bool hov = user && rectHas(g_brw.iName, g_mouseX, g_mouseY);
                if (hov) {
                    G::SolidBrush hb(kFSurf2);
                    roundRect(g, (G::REAL)g_brw.iName.left, (G::REAL)g_brw.iName.top,
                              (G::REAL)(g_brw.iName.right - g_brw.iName.left),
                              (G::REAL)(g_brw.iName.bottom - g_brw.iName.top), 6.0f, nullptr, &hb);
                }
                drawTxFit(g, nm, G::FontStyleBold, kFTxt, (G::REAL)ir.left + 6, (G::REAL)g_brw.iName.top + 3, 0.8f,
                          (G::REAL)(ir.right - ir.left) - 14.0f, TxNombre);
            }
        }
        // ampli · caracter · origen
        {
            WCHAR sub[160];
            const int gp = presetGroup(idx);
            swprintf_s(sub, L"%s  \x00b7  %s  \x00b7  %s",
                       (gp >= 0) ? tr(kAmpNames[gp]) : tr(L"SIN AMPLI"), tr(presetCharLabel(idx)),
                       isT3kPreset(idx) ? L"TONE3000" : (user ? tr(L"PRESET MIO") : tr(L"DE FABRICA")));
            drawTxFit(g, sub, G::FontStyleRegular, kFTxt3, (G::REAL)ir.left, (G::REAL)g_brw.iName.bottom + 4.0f, 0.4f,
                      (G::REAL)(ir.right - ir.left), TxRotulo);
        }
        {
            G::REAL sy = (G::REAL)g_brw.iAuthor.top - 8.0f;
            G::Pen sp(kFLine, 1.0f); g.DrawLine(&sp, (G::REAL)ir.left, sy, (G::REAL)ir.right, sy);
        }
        // autor (editable). Un preset de TONE3000 ENSENA su autor y su ficha, pero
        // no se edita: la ficha es del fichero, no del .ini.
        const bool meta = user || isT3kPreset(idx);
        drawTxFit(g, L"AUTOR", G::FontStyleBold, kFTxt3, (G::REAL)ir.left, (G::REAL)g_brw.iAuthor.top - 19.0f, 1.0f, 120.0f, TxRotulo);
        brwField(g, g_brw.iAuthor, meta ? presetAuthor(idx) : L"", 2, user);
        // comentario (editable)
        drawTxFit(g, L"COMENTARIO", G::FontStyleBold, kFTxt3, (G::REAL)ir.left, (G::REAL)g_brw.iComment.top - 19.0f, 1.0f, 160.0f, TxRotulo);
        {
            const RECT& cr = g_brw.iComment;
            const WCHAR* cm = meta ? presetComment(idx) : L"";
            if (g_brwEdit == 3) {
                brwField(g, cr, cm, 3, user);
            } else {
                const bool hov = user && rectHas(cr, g_mouseX, g_mouseY);
                if (hov) {
                    G::SolidBrush hb(kFSurf2);
                    roundRect(g, (G::REAL)cr.left, (G::REAL)cr.top, (G::REAL)(cr.right - cr.left),
                              (G::REAL)(cr.bottom - cr.top), 6.0f, nullptr, &hb);
                }
                G::Font fc(kFontUI, kTCuerpo);
                G::SolidBrush cb(cm[0] ? kFTxt2 : kFTxt3);
                G::StringFormat sfw;
                g.DrawString(cm[0] ? cm : L"\x2014", -1, &fc,
                             G::RectF((G::REAL)cr.left + 6, (G::REAL)cr.top + 2, (G::REAL)(cr.right - cr.left) - 12, (G::REAL)(cr.bottom - cr.top) - 4),
                             &sfw, &cb);
            }
        }
        {
            G::REAL sy = (G::REAL)g_brw.iTags[0].top - 8.0f;
            G::Pen sp(kFLine, 1.0f); g.DrawLine(&sp, (G::REAL)ir.left, sy, (G::REAL)ir.right, sy);
        }
        // etiquetas: las seis del vocabulario, encendidas las que lleva. En un
        // preset mio se PULSAN (un clic enciende o apaga esa etiqueta).
        drawTxFit(g, L"ETIQUETAS", G::FontStyleBold, kFTxt3, (G::REAL)ir.left, (G::REAL)g_brw.iTags[0].top - 19.0f, 1.0f, 160.0f, TxRotulo);
        {
            const unsigned int t = presetTags(idx);
            for (int c = 0; c < kCharCount; ++c) {
                const bool on = (t & (1u << c)) != 0;
                if (user) brwChip(g, g_brw.iTags[c], kCharNames[c], on);
                else if (on) {
                    G::SolidBrush bg(kFSurf3); G::Pen bp(kFLine, 1.0f);
                    roundRect(g, (G::REAL)g_brw.iTags[c].left, (G::REAL)g_brw.iTags[c].top,
                              (G::REAL)(g_brw.iTags[c].right - g_brw.iTags[c].left),
                              (G::REAL)(g_brw.iTags[c].bottom - g_brw.iTags[c].top), kRPill, &bp, &bg);
                    brwTextIn(g, g_brw.iTags[c], kCharNames[c], G::FontStyleBold, kFTxt2, TxRotulo, 0.4f, 6.0f);
                }
            }
        }
        // color de favorito: se PULSA (recorre los cinco)
        drawTxFit(g, L"FAVORITO", G::FontStyleBold, kFTxt3, (G::REAL)ir.left, (G::REAL)g_brw.iFav.top - 19.0f, 1.0f, 160.0f, TxRotulo);
        {
            const RECT& fr = g_brw.iFav;
            const bool hov = user && rectHas(fr, g_mouseX, g_mouseY);
            if (hov) {
                G::SolidBrush hb(kFSurf2);
                roundRect(g, (G::REAL)fr.left, (G::REAL)fr.top, (G::REAL)(fr.right - fr.left), (G::REAL)(fr.bottom - fr.top), 6.0f, nullptr, &hb);
            }
            const int fc = presetFavColor(idx);
            const G::REAL dx = (G::REAL)fr.left + 8.0f, dy = (G::REAL)fr.top + 6.0f;
            if (fc) { G::SolidBrush db(favColorAt(fc)); g.FillEllipse(&db, dx, dy, 12.0f, 12.0f); }
            else    { G::Pen dp(kFTxt3, 1.2f); g.DrawEllipse(&dp, dx, dy, 12.0f, 12.0f); }
            drawTxFit(g, kBrwFavNames[fc], G::FontStyleRegular, kFTxt2, dx + 20.0f, (G::REAL)fr.top + 4.0f, 0.5f, 200.0f, TxCuerpo);
        }
        // Las FECHAS: solo los presets mios las tienen (los de fabrica no llevan).
        // Es lo unico de la ficha que no se edita.
        if (user) {
            WCHAR w1[48], w2[48], line[160];
            brwDateW(presetCreated(idx), w1, 48);
            brwDateW(presetUsed(idx),    w2, 48);
            swprintf_s(line, tr(L"CREADO %s   \x00b7   USADO %s"), w1, w2);
            drawTxFit(g, line, G::FontStyleRegular, kFTxt3, (G::REAL)ir.left, (G::REAL)g_brw.iWhen.top, 0.4f,
                      (G::REAL)(ir.right - ir.left), TxRotulo);
        }
        // BORRAR: la accion que en el sidebar era el aspa de la fila (paso 19).
        // Pide DOS clics -el primero arma el boton- para no llevarse un preset de
        // un roce, el mismo patron que el resto de la app.
        if (user) {
            const RECT& dr = g_brw.iDel;
            const bool armed = (g_delArm == idx);
            const bool hov = rectHas(dr, g_mouseX, g_mouseY);
            flatPillBody(g, (G::REAL)dr.left, (G::REAL)dr.top,
                         (G::REAL)(dr.right - dr.left), (G::REAL)(dr.bottom - dr.top),
                         hov && !armed, armed, kHot);
            brwTextIn(g, dr, armed ? L"SEGURO? PULSA OTRA VEZ" : L"BORRAR PRESET",
                      G::FontStyleBold, armed ? kFTxt : (hov ? kFTxt : kFTxt2), TxRotulo, 0.4f, 10.0f);
        }
        if (!user) {
            drawTxFit(g, L"los presets de fabrica no se editan: guarda una copia para tocar sus metadatos",
                      G::FontStyleRegular, kFTxt3, (G::REAL)ir.left, (G::REAL)ir.bottom - 20.0f, 0.4f,
                      (G::REAL)(ir.right - ir.left), TxRotulo);
        }
    }
    // --- pie: los atajos ---
    brwTextIn(g, RECT{ BRW_X + 24, BRW_Y + BRW_H - 40, BRW_X + BRW_W - 24, BRW_Y + BRW_H - 16 },
              L"ESC CIERRA   \x00b7   UN CLIC CARGA EL PRESET   \x00b7   EL PUNTO DE COLOR ES EL FAVORITO   \x00b7   EN UN PRESET MIO SE ESCRIBEN LOS CAMPOS Y BORRAR PIDE DOS CLICS",
              G::FontStyleRegular, kFTxt3, TxRotulo, 0.6f, 6.0f);
}

// --- Los metadatos del Info pane (paso 18.7) ---------------------------------
// Guarda lo que se estaba escribiendo en la ficha del preset mio. El nombre, el
// autor y el comentario viven en la misma ficha que las etiquetas y el color, y
// los tres se persisten al instante (saveIni), como el resto del navegador.
static void brwCommitEdit() {
    if (g_brwEdit == 0) return;
    const int idx = (g_brwSel >= 0) ? g_brwSel : g_presetIdx;
    if (isUserPreset(idx)) {
        UserPreset& u = g_userPresets[idx - kNumPresets];
        if      (g_brwEdit == 1) wcsncpy_s(u.name,    g_brwEditBuf, _TRUNCATE);
        else if (g_brwEdit == 2) wcsncpy_s(u.author,  g_brwEditBuf, _TRUNCATE);
        else if (g_brwEdit == 3) wcsncpy_s(u.comment, g_brwEditBuf, _TRUNCATE);
        saveIni();
        g_sceneDirty.store(true);
        toast(L"METADATOS GUARDADOS");
    }
    g_brwEdit = 0;
    g_brwEditBuf[0] = 0;
}
// Abre la escritura de un campo con lo que el campo ya dice.
static void brwBeginEdit(int que, const WCHAR* val) {
    g_brwEdit = que;
    wcsncpy_s(g_brwEditBuf, (val && val[0]) ? val : L"", _TRUNCATE);
    g_brwFocus = false;
}
// Enciende o apaga una etiqueta de la ficha del preset mio.
static void brwToggleTag(int idx, int c) {
    if (!isUserPreset(idx) || c < 0 || c >= kCharCount) return;
    g_userPresets[idx - kNumPresets].tags ^= (1u << c);
    saveIni();
    g_sceneDirty.store(true);
}
// Recorre el color de favorito del preset (el mismo gesto que en la lista).
static void brwCycleFav(int idx) {
    if (idx < 0 || idx >= presetTotal()) return;
    presetFavSet(idx, (presetFavColor(idx) + 1) % kFavColors);
    saveIni();
    g_sceneDirty.store(true);
}

// Abre el navegador sobre el preset que suena y con los filtros limpios.
static void brwOpen() {
    modalCloseAll();
    g_brwOpen = true;
    // La biblioteca de TONE3000 se relee del disco al abrir: si acabas de bajar o
    // de crear un preset en su web, aparece sin reiniciar la app.
    t3kLoadLocalPresets();
    g_brwSel  = (g_presetIdx >= 0 && g_presetIdx < presetTotal()) ? g_presetIdx : -1;
    g_brwScroll = 0;
    g_brwFocus = false;
    g_searchFocus = false;
    // Deja a la vista el preset que suena (si cabe en la lista, claro).
    brwLayout();
    int ord[kMaxListAll];
    const int n = browseOrder(ord);
    const int vis = brwVisRows();
    for (int i = 0; i < n; ++i) if (ord[i] == g_brwSel) {
        if (i >= vis) g_brwScroll = i - vis + 1;
        if (g_brwScroll > n - vis) g_brwScroll = (n > vis) ? n - vis : 0;
        break;
    }
}
// Un clic dentro de la ventana. Fuera del panel cierra (como el resto de modales).
static void brwClick(int x, int y) {
    POINT pt = { x, y };
    const RECT panel = { BRW_X, BRW_Y, BRW_X + BRW_W, BRW_Y + BRW_H };
    // Si se estaba escribiendo un campo y el clic no cae en EL MISMO campo, se
    // guarda lo escrito antes de seguir (como cualquier formulario).
    if (g_brwEdit != 0) {
        brwLayout();
        const RECT* er = (g_brwEdit == 1) ? &g_brw.iName
                       : (g_brwEdit == 2) ? &g_brw.iAuthor : &g_brw.iComment;
        if (!PtInRect(er, pt)) brwCommitEdit();
    }
    if (!PtInRect(&panel, pt)) { g_brwOpen = false; g_brwFocus = false; return; }
    brwLayout();
    if (PtInRect(&g_brw.close, pt)) { g_brwOpen = false; g_brwFocus = false; return; }
    for (int i = 0; i < 5; ++i) if (PtInRect(&g_brw.ord[i], pt)) { g_brwSort = i; g_brwScroll = 0; return; }
    for (int i = 0; i < kFavColors; ++i)
        if (PtInRect(&g_brw.fav[i], pt)) { g_brwColor = (i == 0) ? -1 : i; g_brwScroll = 0; return; }
    for (int i = 0; i < kBrwOrgCount; ++i)
        if (PtInRect(&g_brw.org[i], pt)) { g_brwOrigin = i - 1; g_brwScroll = 0; return; }
    for (int i = 0; i < kCharCount; ++i)
        if (PtInRect(&g_brw.chr[i], pt)) { g_brwChar ^= (1u << i); g_brwScroll = 0; return; }
    for (int i = 0; i < kAmpCount; ++i)
        if (PtInRect(&g_brw.amp[i], pt)) { g_brwAmp ^= (1u << i); g_brwScroll = 0; return; }
    if (PtInRect(&g_brw.search, pt)) { g_brwFocus = true; g_searchFocus = false; return; }
    g_brwFocus = false;
    if (PtInRect(&g_brw.list, pt)) {
        int ord[kMaxListAll];
        const int n = browseOrder(ord);
        const int vi = (y - g_brw.list.top) / BRW_ROW_H;
        const int k = g_brwScroll + vi;
        if (k >= 0 && k < n) {
            const int idx = ord[k];
            const RECT r = brwRowRect(vi);
            const RECT rd = brwRowDot(r);
            if (PtInRect(&rd, pt)) {   // el punto: cicla el color de favorito
                presetFavSet(idx, (presetFavColor(idx) + 1) % kFavColors);
                g_brwSel = idx;
                saveIni();
                g_sceneDirty.store(true);
            } else {                             // el resto de la fila: carga el preset
                g_brwSel = idx;
                // Un preset de TONE3000 NO pasa a g_presetIdx: al cargarlo, el
                // rig deja de ser un preset del banco (ese indice no vive ahi) y
                // el punto de MODIFICADO lo dira.
                if (!isT3kPreset(idx)) g_presetIdx = idx;
                presetApplyFromBrowser(idx);
                g_sceneDirty.store(true);
            }
        }
        return;
    }
    // --- Info pane: los metadatos del preset mio ---
    {
        const int idx = (g_brwSel >= 0) ? g_brwSel : g_presetIdx;
        const bool user = isUserPreset(idx);
        if (user) {
            if (PtInRect(&g_brw.iName, pt))    { WCHAR nm[128]; presetNameW(idx, nm, 128); brwBeginEdit(1, nm); return; }
            if (PtInRect(&g_brw.iAuthor, pt))  { brwBeginEdit(2, presetAuthor(idx));  return; }
            if (PtInRect(&g_brw.iComment, pt)) { brwBeginEdit(3, presetComment(idx)); return; }
            for (int c = 0; c < kCharCount; ++c)
                if (PtInRect(&g_brw.iTags[c], pt)) { brwToggleTag(idx, c); return; }
            if (PtInRect(&g_brw.iFav, pt)) { brwCycleFav(idx); return; }
            // BORRAR: el aspa que el sidebar tenia en la fila (paso 19). Dos
            // clics: el primero arma el boton. Al borrar hay que soltar la
            // seleccion: los indices de los presets mios se corren y g_brwSel
            // apuntaria a otro preset (o fuera de la lista).
            if (PtInRect(&g_brw.iDel, pt)) {
                if (g_delArm == idx) {
                    deleteUserPreset(idx);
                    g_delArm = -1;
                    g_brwSel = -1;
                } else {
                    g_delArm = idx;
                }
                return;
            }
        }
    }
}
// La rueda dentro de la lista. Devuelve true si el navegador la consume.
static bool brwWheel(int delta) {
    if (!g_brwOpen) return false;
    brwLayout();
    POINT mp = { g_mouseX, g_mouseY };
    if (!PtInRect(&g_brw.list, mp)) return true;   // modal: se traga la rueda
    int ord[kMaxListAll];
    const int n = browseOrder(ord);
    const int vis = brwVisRows();
    g_brwScroll += (delta > 0) ? -3 : 3;
    if (g_brwScroll > n - vis) g_brwScroll = (n > vis) ? n - vis : 0;
    if (g_brwScroll < 0) g_brwScroll = 0;
    return true;
}

// Cadena del perfil retirada: se suelta cuando han empezado al menos DOS bloques
// de audio desde que se sustituyo. El hilo de audio lee el puntero al principio
// de cada bloque, asi que dos bloques despues el que pudiera estar usando la
// cadena vieja ya termino. Soltarla antes seria liberar memoria que un bloque en
// vuelo esta leyendo. Si el motor no esta sonando no hay bloque en vuelo y se
// suelta todo menos la ultima (la publicada ahora).
static void namRetiroFlush() {
    const bool vivo = g_params.audioActive.load(std::memory_order_relaxed);
    const long long now = g_audioBlocks.load(std::memory_order_acquire);
    size_t k = 0;
    for (size_t i = 0; i < g_namRetiro.size(); ++i) {
        const bool ultima = (i + 1 == g_namRetiro.size());
        if (!ultima && (!vivo || now >= g_namRetiro[i].epoch + 2)) continue;   // se suelta
        if (k != i) g_namRetiro[k] = std::move(g_namRetiro[i]);
        ++k;
    }
    g_namRetiro.resize(k);
}

static void paintWindow(HWND hwnd) {
    LARGE_INTEGER pf0; QueryPerformanceCounter(&pf0);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) { EndPaint(hwnd, &ps); return; }
    uiCompute(W, H);                      // escala de interfaz segun el cliente real

    // Reconstruir la escena estatica solo al cambiar de tamano; las plantillas
    // de slot del pedalboard se construyen una sola vez
    if (!g_scene.valid || g_scene.w != W || g_scene.h != H || g_sceneDirty.exchange(false))
        buildStaticScene(W, H);
    if (!g_slotTmpl[0].valid) buildSlotTemplates();

    // Buffer trasero reutilizado (no se crea ninguno por frame)
    if (!g_bufDC || g_bufW != W || g_bufH != H) {
        freeBackbuffer();
        HDC scr = GetDC(nullptr);
        g_bufDC  = CreateCompatibleDC(scr);
        g_bufBmp = createSurfaceDib(scr, W, H, &g_bufBits);
        ReleaseDC(nullptr, scr);
        if (g_bufDC && g_bufBmp && g_bufBits) {
            g_bufOld = SelectObject(g_bufDC, g_bufBmp);
            g_bufW = W; g_bufH = H;
            // PixelFormat32bppPARGB es una macro de gdiplus.h: sin calificar
            g_bufGpBmp = new G::Bitmap(W, H, W*4, PixelFormat32bppPARGB, (BYTE*)g_bufBits);
        }
    }
    if (!g_bufDC || !g_bufBmp) { EndPaint(hwnd, &ps); return; }

    // GDI+ pinta sobre el Bitmap (via rapida); si no estuviera disponible, se
    // sigue pintando desde el HDC (via lenta pero correcta).
    std::unique_ptr<G::Graphics> gHolder;
    if (g_bufGpBmp) gHolder.reset(new G::Graphics(g_bufGpBmp));
    else            gHolder.reset(new G::Graphics(g_bufDC));
    G::Graphics& g = *gHolder;
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
    profSectionsReset();   //PFMARK

    // 1) Escena estatica con un solo BitBlt
    LARGE_INTEGER pfS0, pfS1, pfD1;
    QueryPerformanceCounter(&pfS0);
    if (g_scene.dc) {
        BitBlt(g_bufDC, 0, 0, W, H, g_scene.dc, 0, 0, SRCCOPY);
    } else {
        G::SolidBrush bg(G::Color(255, 0x08, 0x08, 0x0a));
        g.FillRectangle(&bg, 0.0f, 0.0f, (G::REAL)W, (G::REAL)H);
    }
    QueryPerformanceCounter(&pfS1);

    // 1b) Escala de interfaz para TODO lo que viene: la capa dinamica pinta en
    // coordenadas de diseno y GDI+ escala formas, plumas, sombras y tipografia
    // a la vez. El BitBlt de arriba ya iba a tanyo real.
    uiApplyTransform(g, W, H);

    // 2) Zonas: UNA tabla para pintura, hover y clic (ver buildZones)
    buildZones();

    G::StringFormat sfC;
    sfC.SetAlignment(G::StringAlignmentCenter);
    sfC.SetLineAlignment(G::StringAlignmentCenter);

    G::Font fH2(kFontTec, kTMarca, G::FontStyleBold);
    G::Font fSub(kFontUI, kTCuerpo);
    G::Font fKnob(kFontUI, kTCuerpo);
    G::Font fBold(kFontUI, kTCuerpo, G::FontStyleBold);
    G::Font fSmall(kFontUI, kTCuerpo);
    G::Font fPreset(kFontUI, kTNombre);

    // 2) Capa dinamica: solo lo que cambia con el estado y el raton

    profMark("Barra superior dinamica: preset + gear");   //PFMARK
    // --- Barra superior dinamica: preset + gear + audio ---
    {
        WCHAR pname[128];
        presetNameW(g_presetIdx, pname, 128);
        const bool bhov = zoneHit(6);          // preset superior
        if (bhov) {   // hover: la caja SUBE un tono, el mismo trato que las filas
            G::SolidBrush hb(kFSurf3);
            G::Pen hp(kFLine, 1.0f);
            roundRect(g, (G::REAL)TOP_BOX_X, (G::REAL)TOP_BOX_Y, (G::REAL)TOP_BOX_W,
                      (G::REAL)TOP_BOX_H, kRPill, &hp, &hb);
        }
        if (g_topOpen) {
            // Modo BUSCADOR: la caja es un CAMPO. Se pinta el texto tecleado (o
            // el marcador de posicion) y el cursor; al cerrar vuelve el preset.
            const G::REAL fx = (G::REAL)TOP_BOX_X + 18.0f;
            const G::REAL fw = (G::REAL)TOP_DIV_X - 34.0f;
            drawTextFit(g, g_topQuery[0] ? g_topQuery : L"Buscar preset...",
                        G::FontStyleRegular, g_topQuery[0] ? kFTxt : kFTxt3,
                        fx, (G::REAL)TOP_BOX_Y, fw, (G::REAL)TOP_BOX_H,
                        kTValor, 9.5f, kFontMono, false, true);
            if (g_topQuery[0]) {
                G::Font fq(kFontMono, kTValor);
                G::RectF mb;
                g.MeasureString(g_topQuery, -1, &fq, G::RectF(0, 0, 900, 60), nullptr, &mb);
                const G::REAL cx = fx + mb.Width + 2.0f;
                if (cx < fx + fw - 2.0f) {
                    G::SolidBrush cb(kAcc);
                    g.FillRectangle(&cb, cx, (G::REAL)(TOP_BOX_Y + 16), 1.5f, 20.0f);
                }
            }
            // foco: el mismo canto ambar que el resto de campos
            G::Pen fp(kAcc, 1.4f);
            roundRect(g, (G::REAL)TOP_BOX_X, (G::REAL)TOP_BOX_Y, (G::REAL)TOP_BOX_W,
                      (G::REAL)TOP_BOX_H, kRPill, &fp, nullptr);
        } else {
        // Linea de arriba: QUE CABEZA suena. Es el sitio que el diseno reserva
        //  al nombre del ampli (la banda de la unidad dice que ESTANTE es, no
        //  que ampli lleva montado). Va ALINEADO con el nombre del preset (que
        //  empieza en +60), no centrado en la caja: antes el rotulo quedaba
        //  flotando sobre el divisor de la flecha, desconectado del valor.
        drawTxFit(g, kAmpNames[g_params.model.load()], G::FontStyleBold,
                      kFTxt2, (G::REAL)(TOP_BOX_X + 60), (G::REAL)(TOP_BOX_Y + 7), 1.2f,
                      290.0f, TxRotulo);

        // El chip del numero: QUE LOTE del ampli de arriba suena, el mismo numero
        // que ensena la lista (1..6 los de fabrica, 7,8... los del usuario). El
        // chip leia antes "05. " del NOMBRE del preset: los nombres del banco no
        // llevan numero, asi que ese despiece no se ejecutaba nunca (chip muerto)
        // y el unico numero que se veia era el indice global de la lista.
        G::SolidBrush tb(kFTxt);
        const WCHAR* nm = pname;
        const int inG = presetInGroup(g_presetIdx);
        if (inG > 0 && !g_topOpen) {
            G::SolidBrush cb(G::Color(160, 0xff, 0xa5, 0x1f));
            G::Pen cp(G::Color(150, kAcc.GetR(), kAcc.GetG(), kAcc.GetB()), 1.0f);
            roundRect(g, (G::REAL)TOP_BOX_X + 14.0f, (G::REAL)(TOP_BOX_Y + 17), 38.0f, 26.0f, kRWell, &cp, &cb);
            WCHAR num[4]; swprintf_s(num, L"%d", inG);
            G::SolidBrush nb(kAccGlb);
            G::Font fNum(kFontMono, kTValor, G::FontStyleBold);
            g.DrawString(num, -1, &fNum, G::RectF((G::REAL)TOP_BOX_X + 14.0f, (G::REAL)(TOP_BOX_Y + 17), 38.0f, 26.0f), &sfC, &nb);
        }
        {
            G::StringFormat sfL;
            sfL.SetLineAlignment(G::StringAlignmentCenter);
            sfL.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoClip);
            // El nombre se ajusta a su caja: sin tope, un nombre largo del usuario
            // (hasta 63 letras) se salia por encima de la flecha del divisor.
            const std::wstring t = fitEllipsis(g, nm, fPreset, 0.0f, 276.0f);
            g.DrawString(t.c_str(), -1, &fPreset,
                         G::RectF((G::REAL)TOP_BOX_X + 62.0f, (G::REAL)(TOP_BOX_Y + 18), 288.0f, 28.0f), &sfL, &tb);
            // El PUNTO de MODIFICADO: el rig de ahora no es el del preset que
            // suena. Va pegado al nombre, que es donde se mira.
            if (presetDirty()) {
                G::RectF mb3;
                g.MeasureString(t.c_str(), -1, &fPreset, G::RectF(0.0f, 0.0f, 900.0f, 60.0f), nullptr, &mb3);
                const G::REAL dx = (G::REAL)(TOP_BOX_X + 62) + mb3.Width + 10.0f;
                if (dx < (G::REAL)(TOP_BOX_X + TOP_DIV_X) - 14.0f) {
                    G::SolidBrush db(kFTxt);
                    g.FillEllipse(&db, dx, (G::REAL)(TOP_BOX_Y + 29), 6.0f, 6.0f);
                }
            }
        }
        }   // fin del modo PRESET (el else del buscador sigue hasta aqui)
        icChev(g, (G::REAL)TOP_BOX_X + TOP_ARROW_X + 16.0f,
               (G::REAL)TOP_BOX_Y + TOP_BOX_H*0.5f, 11.0f,
               bhov ? kFTxt : kFTxt2, 0);
        pressVeil(g, (G::REAL)TOP_BOX_X, (G::REAL)TOP_BOX_Y, (G::REAL)TOP_BOX_W,
                  (G::REAL)TOP_BOX_H, kRPill);
    }

    {
        bool gh = zoneHit(5);                      // ajustes
        const bool gopen = g_settingsOpen;
        softShadow(g, (G::REAL)TOP_GEAR_X, 14.0f, 40.0f, 40.0f, 20.0f, 48);
        G::SolidBrush cgb(gopen ? G::Color(255, 0x10, 0x10, 0x12)
                                : (gh ? kBtnBgH : kBtnBg));
        g.FillEllipse(&cgb, TOP_GEAR_X, 14, 40, 40);
        const G::Color gtC(gopen ? kAcc
                                 : (gh ? kFTxt : kFTxt2));
        G::Pen gp(gopen ? kAcc
                        : (gh ? G::Color(255, 0x24, 0x24, 0x28) : kBtnLn), 1.0f);
        g.DrawEllipse(&gp, TOP_GEAR_X, 14, 40, 40);
        if (g_lbtn && zoneHit(5)) {
            G::SolidBrush gv(G::Color(70, 0, 0, 0));
            g.FillEllipse(&gv, TOP_GEAR_X, 14, 40, 40);
        }
        // Engranaje vectorial, centrado en el circulo POR CONSTRUCCION: el
        // glifo de Segoe UI Symbol traia descentrado propio y habia que medirlo.
        icGear(g, (G::REAL)TOP_GEAR_X + 20.0f, 34.0f, 10.5f, gtC);
    }

    profMark("Boton global AUDIO ON/OFF (arranca apa");   //PFMARK
    // --- Boton global AUDIO ON/OFF (arranca apagado) ---
    //  Tres estados, y cada uno se ve de un vistazo porque el boton es lo primero
    //  que se pulsa: ON en ambar (lo que suena), OFF en ROJO DE AVISO. Con el
    //  cuerpo neutro de los otros botones de cabecera, "AUDIO OFF" se leia como
    //  un boton mas y no como el estado de la app: de ahi venia la mitad de los
    //  "no me suena". El piloto esta en los dos estados (ambar / rojo), asi que
    //  el boton nunca parece apagado del todo.
    {
        const bool en  = g_audioEnabled.load();
        const bool hov = zoneHit(4);                     // AUDIO ON/OFF
        G::Font fBtn(kFontTec, kTRotulo, G::FontStyleBold);
        const G::Color ac = en ? kAcc : kFOff;
        {
            G::SolidBrush bg(flatTint(kFSurf3, ac, hov ? 0.42f : 0.30f));
            G::Pen bp(ac, 1.0f);
            roundRect(g, (G::REAL)TOP_AUDIO_X, 14.0f, 100.0f, 40.0f, kRPill, &bp, &bg);
        }
        {   // piloto: marfil-ambar cuando suena, rojo de aviso cuando no
            G::SolidBrush led(en ? G::Color(255, 0xff, 0xdd, 0x9a) : kFOff);
            g.FillEllipse(&led, (G::REAL)TOP_AUDIO_X + 9.0f, 14.0f + 20.0f - 2.0f, 4.0f, 4.0f);
        }
        {
            G::StringFormat sfB;
            sfB.SetAlignment(G::StringAlignmentCenter);
            sfB.SetLineAlignment(G::StringAlignmentCenter);
            sfB.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoClip);
            G::SolidBrush tb(en ? kFTxt : ac);
            g.DrawString(en ? L"AUDIO ON" : L"AUDIO OFF", -1, &fBtn,
                         G::RectF((G::REAL)TOP_AUDIO_X, 14.0f, 100.0f, 40.0f), &sfB, &tb);
        }
        pressVeil(g, (G::REAL)TOP_AUDIO_X, 14.0f, 100.0f, 40.0f, kRPill);
    }

    profMark("GUARDAR preset de usuario + GRABAR / P");   //PFMARK
    // --- GUARDAR preset de usuario + GRABAR / PARAR WAV ---
    {
        G::Font fTop(kFontTec, kTRotulo, G::FontStyleBold);
        // El filete entre los dos grupos de la derecha: a la izquierda la accion
        // que cambia el documento, a la derecha lo que solo enciende y apaga.
        {
            G::Pen vp(kFLine, 1.0f);
            g.DrawLine(&vp, (G::REAL)TOP_SEP_X, 22.0f, (G::REAL)TOP_SEP_X, 46.0f);
        }
        {
            // NAVEGADOR de presets (paso 18): abre la ventana superpuesta. Es un
            // conmutador: encendido mientras la ventana esta abierta.
            const bool hov = zoneHit(79);
            topBtn(g, (G::REAL)TOP_BRW_X, 14.0f, 100.0f, 40.0f, L"PRESETS", fTop,
                   hov, g_brwOpen, kAcc);
        }
        {
            // GUARDAR sobre un preset del usuario lo pisa; delante de un lote de
            // fabrica no hay nada que pisar, asi que el boton dice lo que hace:
            // GUARDAR COMO pregunta el nombre y crea uno nuevo. Es el PRIMARIO
            // de la cabecera (un tono por encima del panel, rotulo en marfil);
            // GRABAR y AUDIO son conmutadores y solo se encienden con su color.
            const bool user  = isUserPreset(g_presetIdx);
            const bool hov   = zoneHit(74);              // GUARDAR
            topBtn(g, (G::REAL)TOP_SAVE_X, 14.0f, 100.0f, 40.0f,
                   user ? L"GUARDAR" : L"GUARDAR COMO", fTop, hov, false, kAcc, true);
        }
        {
            const bool recOn = g_recActive.load();
            const bool hov = zoneHit(75);              // GRABAR
            WCHAR wrec[32];
            if (recOn) {
                const DWORD secs = (GetTickCount() - g_recTickStart) / 1000;
                swprintf_s(wrec, tr(L"PARAR %lu:%02lu"), secs / 60, secs % 60);
            } else {
                wcscpy_s(wrec, L"GRABAR");
            }
            topBtn(g, (G::REAL)TOP_REC_X, 14.0f, 100.0f, 40.0f, wrec, fTop,
                   hov, recOn, kHot);
        }
    }

    profMark("Recuento de cada unidad del rack");   //PFMARK
    // --- Recuento de cada unidad (capa viva: cambia con el preset) -----------
    //  "3 / 6" en los estantes (plazas ocupadas sobre las plazas del rack) y el
    //  LOTE del cabezal ("SPARKLE TWIN 3/6"), alineado a la derecha de la banda
    //  como el contador de un modulo de rack. Antes este renglon ponia "PRESET
    //  06", el indice global del banco: el 31 de 78 no es una posicion que nadie
    //  pueda buscar. El nombre del ampli a 15pt que ocupaba este renglon se
    //  pisaba con la serigrafia de la unidad.
    {
        G::Font fCnt(kFontMono, kTPico, G::FontStyleBold);
        G::SolidBrush cb(kFTxt3);
        G::StringFormat sfCntR;
        sfCntR.SetAlignment(G::StringAlignmentFar);
        sfCntR.SetFormatFlags(G::StringFormatFlagsNoWrap);
        const int bandY[3] = { PRE_SHELF_Y, AMP_UNIT_Y, POST_SHELF_Y };
        for (int i = 0; i < 3; ++i) {
            WCHAR cnt[48];
            if (i == 1) {
                presetTagW(g_presetIdx, cnt, 48);
            } else {
                const int used = (int)((i == 0) ? g_chainPre.size() : g_chainPost.size());
                swprintf_s(cnt, L"%d / %d", used, PED_MAX_ROW);
            }
            // margen de 44px al canto: el tornillo de la unidad vive a 13px del
            // borde y el recuento no puede tocarlo
            g.DrawString(cnt, -1, &fCnt,
                         G::RectF((G::REAL)(RACK_X + RACK_W - 200), (G::REAL)bandY[i] + 7.0f,
                                  156.0f, 15.0f), &sfCntR, &cb);
        }
    }

    profMark("Cadena de senal: la banda libre bajo e");   //PFMARK
    // --- Cadena de senal: la banda libre bajo el frontal deja de ser un hueco
    //     vacio y pasa a resumir el recorrido del sonido (IN -> pedales -> amp
    //     -> cabina -> OUT), que es lo que el usuario espera ver ahi.
    {
        const G::REAL csx = (G::REAL)ART_X;
        const G::REAL csy = (G::REAL)CHAIN_Y;
        const G::REAL csw = (G::REAL)ART_W, csh = (G::REAL)CHAIN_H;
        const int mm = g_params.model.load();
        struct ChainChip { WCHAR t[40]; G::Color c; };
        ChainChip chip[6];
        int nch = 0;
        WCHAR tmp[40];
        // Seis etapas SIEMPRE presentes: el mapa no puede saltar al anadir o
        // quitar un pedal (ENTRADA, PRE, cabezal, cabina, POST, SALIDA). Si una
        // fila esta vacia, su etapa se queda con el rotulo corto y su marca
        // apagada, en vez de desaparecer.
        wcscpy_s(tmp, tr(L"ENTRADA"));        chip[nch].c = kFTxt2; wcscpy_s(chip[nch++].t, tmp);
        {
            if (!g_chainPre.empty()) {
                swprintf_s(tmp, tr(L"PRE \u00D7%d"), (int)g_chainPre.size());
                chip[nch].c = colFx(g_chainPre[0]);
            } else {
                wcscpy_s(tmp, tr(L"PRE"));
                chip[nch].c = kFTxt3;
            }
            wcsncpy_s(chip[nch++].t, tmp, _TRUNCATE);
        }
        chip[nch].c = kAcc;                    wcsncpy_s(chip[nch++].t, tr(kAmpNames[mm]), _TRUNCATE);
        {
            const int cmv = g_params.cabModel.load();
            if (cmv >= 0 && cmv < 6) MultiByteToWideChar(CP_ACP, 0, kCabNames[cmv], -1, tmp, 40);
            else                     wcscpy_s(tmp, L"USER IR");
            chip[nch].c = G::Color(0xff, 0x82, 0x7d, 0x76); wcsncpy_s(chip[nch++].t, tmp, _TRUNCATE);
        }
        {
            if (!g_chainPost.empty()) {
                swprintf_s(tmp, tr(L"POST \u00D7%d"), (int)g_chainPost.size());
                chip[nch].c = colFx(g_chainPost[0]);
            } else {
                wcscpy_s(tmp, tr(L"POST"));
                chip[nch].c = kFTxt3;
            }
            wcsncpy_s(chip[nch++].t, tmp, _TRUNCATE);
        }
        wcscpy_s(tmp, tr(L"SALIDA"));          chip[nch].c = kFTxt2; wcsncpy_s(chip[nch++].t, tmp, _TRUNCATE);
        // anchos: el nombre del amp y el de la cabina son los que mas ocupan
        G::Font fc(kFontTec, kTRotulo, G::FontStyleBold);
        G::REAL cwid[6], sum = 0.0f;
        for (int i = 0; i < nch; ++i) {
            G::RectF b;
            g.MeasureString(chip[i].t, -1, &fc, G::RectF(0.0f, 0.0f, 900.0f, 30.0f), nullptr, &b);
            G::REAL cwi = b.Width + 26.0f;
            if (i == 2 && cwi < 132.0f) cwi = 132.0f;
            if (cwi > 150.0f) cwi = 150.0f;
            if (cwi < 58.0f)  cwi = 58.0f;
            cwid[i] = cwi; sum += cwi;
        }
        // El boton AFINADOR vive en el extremo derecho de la banda: los chips se
        // reparten SOLO en el tramo libre a su izquierda. Centrandolos en la
        // banda entera, el ultimo chip (SALIDA) caia debajo del boton y el borde
        // de la banda recortaba la etiqueta AFINADOR.
        const G::REAL chipL     = csx + 12.0f;
        const G::REAL chipR     = (G::REAL)TUNER_BTN_X - 14.0f;
        const G::REAL cswChips  = chipR - chipL;
        G::REAL gap = 28.0f;
        if (gap*(G::REAL)(nch - 1) > cswChips*0.45f)
            gap = cswChips*0.45f/(G::REAL)(nch - 1);
        const G::REAL avail = cswChips - gap*(nch - 1);
        if (sum > avail) {
            const G::REAL k = avail/sum;
            for (int i = 0; i < nch; ++i) cwid[i] *= k;
            sum = avail;
        }
        G::REAL cx2 = chipL + (cswChips - (sum + gap*(nch - 1)))*0.5f;
        const G::REAL cy2 = csy + (csh - 26.0f)*0.5f;
        // --- Mapa de senal: rail + rotulo + marca vertical ------------------
        //  El diseno NO pinta la cadena con pastillas (competian con los botones
        //  y el ultimo chip acababa debajo del AFINADOR): pinta el recorrido como
        //  el mapa de un rack â€” una linea con un anillo en cada extremo y, bajo
        //  cada etapa, su marca: 3x14 en ambar para el cabezal (el unico nodo
        //  vivo) y 1x8 en marfil bajo para el resto. El rotulo va encima.
        {
            const G::REAL railY = cy2 + 29.0f;
            G::Pen rl(kWellLn, 1.0f);
            g.DrawLine(&rl, csx + 29.0f, railY, (G::REAL)TUNER_BTN_X - 22.0f, railY);
            G::Pen cap(G::Color(255, 0x3a, 0x3a, 0x44), 1.2f);
            g.DrawEllipse(&cap, csx + 26.0f, railY - 3.0f, 6.0f, 6.0f);
            g.DrawEllipse(&cap, (G::REAL)TUNER_BTN_X - 25.0f, railY - 3.0f, 6.0f, 6.0f);
        }
        for (int i = 0; i < nch; ++i) {
            const bool isAmp = (i == 2);            // el cabezal, siempre el 3er nodo
            const G::REAL ccx = cx2 + cwid[i]*0.5f;
            drawTxFitC(g, chip[i].t, G::FontStyleBold,
                           isAmp ? kAcc : G::Color(255, 0xb0, 0xac, 0xa5),
                           ccx, cy2 + 1.0f, 0.7f, cwid[i] - 4.0f, TxRotulo);
            const G::REAL railY = cy2 + 29.0f;
            if (isAmp) {
                // halo ambar que late mientras hay audio: pulso suave 1.5 s,
                // solo vivo con g_params.audioActive. No se dibuja en reposo.
                if (g_params.audioActive.load()) {
                    const int ga = (int)(40.0f + 30.0f*animPulse());
                    G::SolidBrush gb(G::Color(ga, 0xff, 0xa5, 0x1f));
                    roundRect(g, ccx - 7.0f, railY - 12.0f, 14.0f, 24.0f, 6.0f, nullptr, &gb);
                }
                G::SolidBrush tk(kAcc);
                roundRect(g, ccx - 1.5f, railY - 7.0f, 3.0f, 14.0f, 1.0f, nullptr, &tk);
            } else {
                // Marca 2x10 y UN solo tono (kFTxt3) para todas las etapas: a
                // este tamano el color de cada pedal no decia nada y llenaba el
                // mapa de puntitos de colores. El unico ambar de la cadena es el
                // cabezal, que es lo unico que suena.
                G::SolidBrush tk(kFTxt3);
                roundRect(g, ccx - 1.0f, railY - 5.0f, 2.0f, 10.0f, 1.0f, nullptr, &tk);
            }
            cx2 += cwid[i];
            if (i < nch - 1) cx2 += gap;
        }
    }

    profMark("Cluster CAB (hero): IR + micro + mezcl");   //PFMARK
    // --- Cluster CAB (hero): IR + micro + mezcla + visualizador de senal ---
    {
        // borrado diferido de IRs retirados (un frame de margen: el audio ya cambio)
        for (CabIR* r : g_cabRetirados) delete r;
        g_cabRetirados.clear();
        // Cadenas del perfil retiradas: las que el hilo de audio ya dejo atras.
        namRetiroFlush();
        // boton CARGAR IR (abre dialogo WAV): es una ACCION, no un selector; va
        // con la pastilla del sistema, sin flechas y sin borde de color propio.
        {
            const bool hov = zoneHit(76);          // CARGAR IR
            flatPillBody(g, (G::REAL)CAB_BTN_X, (G::REAL)CAB_BTN_Y,
                         (G::REAL)CAB_BTN_W, (G::REAL)CAB_BTN_H, hov, false, kAcc);
            G::SolidBrush bt(hov ? kFTxt : kFTxt2);
            G::Font fB(kFontTec, kTRotulo, G::FontStyleBold);
            g.DrawString(tr(L"CARGAR IR"), -1, &fB,
                         G::RectF((G::REAL)CAB_BTN_X, (G::REAL)CAB_BTN_Y,
                                  (G::REAL)CAB_BTN_W, (G::REAL)CAB_BTN_H), &sfC, &bt);
        }
        pressVeil(g, (G::REAL)CAB_BTN_X, (G::REAL)CAB_BTN_Y, (G::REAL)CAB_BTN_W, (G::REAL)CAB_BTN_H, 8.0f);
        // selector CAB: valor + flechas en la misma pastilla. Izquierda = modelo
        // anterior, derecha = siguiente; el ciclo incluye USER IR si hay IR.
        // Con un IR cargado el valor es el NOMBRE DEL FICHERO (en ambar): el
        // estado util no es "USER IR", es que WAV suena.
        {
            const int cmv = g_params.cabModel.load();
            WCHAR wcb[140];
            if (cmv == 6) {
                const size_t bs = g_cabIRPath.find_last_of(L"\\/");
                wcsncpy_s(wcb, (bs == std::wstring::npos) ? g_cabIRPath.c_str()
                                                          : g_cabIRPath.c_str() + bs + 1, _TRUNCATE);
                if (!wcb[0]) wcscpy_s(wcb, L"USER IR");
            } else if (cmv >= 0 && cmv < 6) MultiByteToWideChar(CP_ACP, 0, kCabNames[cmv], -1, wcb, 80);
            else wcscpy_s(wcb, L"---");
            drawStepPill(g, 92, (G::REAL)CAB_SEL_X, (G::REAL)CAB_SEL_Y,
                         (G::REAL)CAB_SEL_W, (G::REAL)CAB_SEL_H, L"CAB", wcb, cmv == 6);
        }
        pressVeil(g, (G::REAL)CAB_SEL_X, (G::REAL)CAB_SEL_Y, (G::REAL)CAB_SEL_W, (G::REAL)CAB_SEL_H, 8.0f);
        // selector MIC: DYN -> RIBBON -> COND, con las dos direcciones
        {
            const int micv = g_params.cabMic.load();
            const WCHAR* micName = (micv == 1) ? L"RIBBON" : (micv == 2) ? L"COND" : L"DYN";
            drawStepPill(g, 77, (G::REAL)CAB_MIC_X, (G::REAL)CAB_MIC_Y,
                         (G::REAL)CAB_MIC_W, (G::REAL)CAB_MIC_H, L"MIC", micName, false);
        }
        pressVeil(g, (G::REAL)CAB_MIC_X, (G::REAL)CAB_MIC_Y, (G::REAL)CAB_MIC_W, (G::REAL)CAB_MIC_H, 8.0f);
        // selector DIST: NEAR -> MID -> FAR
        {
            const int dsv = g_params.cabDist.load();
            const WCHAR* dName = (dsv == 0) ? L"NEAR" : (dsv == 2) ? L"FAR" : L"MID";
            drawStepPill(g, 93, (G::REAL)CAB_DST_X, (G::REAL)CAB_DST_Y,
                         (G::REAL)CAB_DST_W, (G::REAL)CAB_DST_H, L"DIST", dName, false);
        }
        pressVeil(g, (G::REAL)CAB_DST_X, (G::REAL)CAB_DST_Y, (G::REAL)CAB_DST_W, (G::REAL)CAB_DST_H, 8.0f);
        // boton AFINADOR (abre el modal): diapason + texto + estado. El icono
        // se enciende en verde con el modal abierto y el texto va a su derecha
        // (antes quedaba centrado encima del icono y se solapaban).
        {
            const bool hov = zoneHit(91);          // AFINADOR
            const bool on = g_tunerOpen.load();
            const G::REAL bx = (G::REAL)TUNER_BTN_X, by = (G::REAL)TUNER_BTN_Y;
            if (hov || on) {
                G::SolidBrush hb(on ? kFSurf3 : kBtnBgH);
                G::Pen hp(on ? kAcc : kBtnLn, 1.2f);
                roundRect(g, bx, by, (G::REAL)TUNER_BTN_W, (G::REAL)TUNER_BTN_H, 8, &hp, &hb);
                G::SolidBrush nb(on ? kAcc : kFTxt);
                roundRect(g, bx + 6.0f, by + 4.0f, 3.0f, (G::REAL)TUNER_BTN_H - 8.0f, 1.5f, nullptr, &nb);
            }
            // diapason: dos puenes, base y mango
            {
                const G::REAL cx = bx + 17.0f, cy = by + TUNER_BTN_H*0.5f;
                const G::Color fc = on  ? kAcc
                                  : hov ? G::Color(255, 0x78, 0x78, 0x7e)
                                        : G::Color(0xff, 0x6e, 0x69, 0x63);
                G::Pen fp(fc, 1.7f);
                g.DrawLine(&fp, cx - 3.5f, cy - 8.5f, cx - 3.5f, cy - 1.0f);
                g.DrawLine(&fp, cx + 3.5f, cy - 8.5f, cx + 3.5f, cy - 1.0f);
                g.DrawLine(&fp, cx - 3.5f, cy - 1.0f, cx + 3.5f, cy - 1.0f);
                g.DrawLine(&fp, cx, cy - 1.0f, cx, cy + 7.5f);
                if (on) {   // armonicos: el diapason "suena" mientras mide
                    G::Pen ap(G::Color(0xaa, 0x82, 0x7d, 0x76), 1.2f);
                    g.DrawArc(&ap, cx - 9.5f, cy - 9.5f, 7.0f, 7.0f, 300.0f, 100.0f);
                    g.DrawArc(&ap, cx + 2.5f, cy - 9.5f, 7.0f, 7.0f, 140.0f, 100.0f);
                }
            }
            G::SolidBrush tb(on ? kAcc
                                : G::Color(255, hov ? 235 : 198, hov ? 243 : 208, hov ? 249 : 220));
            G::Font fTb(kFontTec, kTRotulo, G::FontStyleBold);
            g.DrawString(tr(L"AFINADOR"), -1, &fTb,
                         G::RectF(bx + 24.0f, by, (G::REAL)TUNER_BTN_W - 28.0f,
                                  (G::REAL)TUNER_BTN_H), &sfC, &tb);
        }
        pressVeil(g, (G::REAL)TUNER_BTN_X, (G::REAL)TUNER_BTN_Y, (G::REAL)TUNER_BTN_W, (G::REAL)TUNER_BTN_H, 8.0f);
        // visualizador de la senal procesada (onda)
        {
            {
                static float tmp[2048];
                const size_t got = g_scopeRing.pop(tmp, 2048);
                if (got) {
                    g_scopeHist.insert(g_scopeHist.end(), tmp, tmp + got);
                    if (g_scopeHist.size() > 2048)
                        g_scopeHist.erase(g_scopeHist.begin(), g_scopeHist.end() - 2048);
                }
            }
            G::Font fSc(kFontTec, kTPico, G::FontStyleBold);
            G::SolidBrush sb(G::Color(255, 0x78, 0x78, 0x7e));
            g.DrawString(tr(L"SALIDA"), -1, &fSc, G::RectF((G::REAL)SCOPE_X, (G::REAL)(SCOPE_Y - 16),
                                                       (G::REAL)SCOPE_W, 16.0f), &sfC, &sb);
            // Pico real en dB a la derecha de la cabecera: un visualizador sin
            // numero es un adorno, y el pico es lo que dice si hay headroom.
            {
                float pk = 0.0f;
                for (float sv : g_scopeHist) {
                    const float a = (sv < 0.0f) ? -sv : sv;
                    if (a > pk) pk = a;
                }
                WCHAR wdb[24];
                if (pk > 1.0e-4f) swprintf_s(wdb, L"%.1f dB", 20.0f*log10f(pk));
                else              wcscpy_s(wdb, L"\x2014 dB");
                G::Font fDb(kFontMono, kTPico, G::FontStyleBold);
                G::StringFormat sfR; sfR.SetAlignment(G::StringAlignmentFar);
                G::SolidBrush dbc(pk > 0.5f ? kAcc : kFTxt3);
                g.DrawString(wdb, -1, &fDb, G::RectF((G::REAL)SCOPE_X, (G::REAL)(SCOPE_Y - 16),
                                                     (G::REAL)SCOPE_W, 16.0f), &sfR, &dbc);
            }
            // highlight interior de 1px: da canto biselado al marco
            G::Pen fp(G::Color(34, kWellLn.GetR(), kWellLn.GetG(), kWellLn.GetB()), 1.0f);
            roundRect(g, (G::REAL)SCOPE_X + 1.5f, (G::REAL)SCOPE_Y + 1.5f,
                      (G::REAL)SCOPE_W - 3.0f, (G::REAL)SCOPE_H - 3.0f, 7, &fp, nullptr);
            const int pts = SCOPE_W;
            const G::Color acw = colAmp(g_params.model.load());
            if ((int)g_scopeHist.size() >= pts*4) {
                G::PointF wave[512];
                const size_t n = g_scopeHist.size();
                const G::REAL midY = (G::REAL)SCOPE_Y + SCOPE_H*0.5f;
                const G::REAL ampY = SCOPE_H*0.42f;
                for (int i = 0; i < pts; ++i) {
                    const float sv = g_scopeHist[n - (size_t)(pts - i)*4];
                    wave[i].X = (G::REAL)SCOPE_X + 3.0f + (G::REAL)i*((G::REAL)SCOPE_W - 6.0f)/(G::REAL)(pts - 1);
                    wave[i].Y = midY - sv*ampY;
                }
                G::Pen glow(G::Color(58, acw.GetR(), acw.GetG(), acw.GetB()), 4.5f);
                g.DrawLines(&glow, wave, pts);
                G::Pen wp(acw, 1.8f);
                g.DrawLines(&wp, wave, pts);
            } else {
                // Sin senal: trazo plano tenue en vez de un marco vacio
                G::Pen idle(G::Color(58, acw.GetR(), acw.GetG(), acw.GetB()), 1.6f);
                idle.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
                g.DrawLine(&idle, (G::REAL)SCOPE_X + 3.0f,
                                  (G::REAL)SCOPE_Y + SCOPE_H*0.5f,
                                  (G::REAL)SCOPE_X + SCOPE_W - 3.0f,
                                  (G::REAL)SCOPE_Y + SCOPE_H*0.5f);
            }
        }
    }

    profMark("Buscador (capa dinamica: responde al t");   //PFMARK
    // --- Buscador (capa dinamica: responde al teclado y al foco) ---
    {
        if (g_searchFocus) {
            G::Pen sp(kAcc, 1.0f);
            roundRect(g, (G::REAL)SEARCH_X, (G::REAL)SEARCH_Y, (G::REAL)SEARCH_W,
                      (G::REAL)SEARCH_H, 6.0f, &sp, nullptr);
        }
        const G::REAL lcx = (G::REAL)SEARCH_X + 16.0f, lcy = (G::REAL)SEARCH_Y + SEARCH_H*0.5f;
        G::Pen lp(g_searchFocus ? kAcc : kFTxt3, 1.4f);
        g.DrawEllipse(&lp, lcx - 4.2f, lcy - 4.2f, 8.4f, 8.4f);
        g.DrawLine(&lp, lcx + 3.0f, lcy + 3.0f, lcx + 6.6f, lcy + 6.6f);
        G::Font fSrch(kFontUI, kTCuerpo);
        G::StringFormat sfL;
        sfL.SetLineAlignment(G::StringAlignmentCenter);
        sfL.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoClip);
        G::SolidBrush ib(g_ampFilter[0] ? kFTxt : kFTxt3);
        WCHAR ph[48];
        if (g_ampFilter[0]) wcscpy_s(ph, g_ampFilter);
        else wcscpy_s(ph, searchHint());
        g.DrawString(tr(ph), -1, &fSrch,
                     G::RectF((G::REAL)SEARCH_X + 30.0f, (G::REAL)SEARCH_Y,
                              (G::REAL)SEARCH_W - 44.0f, (G::REAL)SEARCH_H), &sfL, &ib);
        if (g_searchFocus) {   // cursor
            G::RectF tb;
            g.MeasureString(ph, -1, &fSrch, G::RectF(0, 0, 900, 30), nullptr, &tb);
            const G::REAL cxp = (G::REAL)SEARCH_X + 30.0f + tb.Width + 1.0f;
            G::Pen cp(kAcc, 1.2f);
            g.DrawLine(&cp, cxp, (G::REAL)SEARCH_Y + 8.0f, cxp, (G::REAL)(SEARCH_Y + SEARCH_H) - 8.0f);
        }
    }

    profMark("Panel izquierdo: lista dinamica de amp");   //PFMARK
    // --- Panel izquierdo: lista dinamica de amps o presets ---
    {
        // El estado de la lista se lee en RANURAS (ver navRows): una ranura por
        // entrada. cnt es el numero de FILAS.
        int rows[kMaxNavRows];
        const int cnt = navRows(rows);
        const int vis = listVisibleRows();
        if (g_listScroll > listScrollMax()) g_listScroll = listScrollMax();
        if (g_listScroll < 0) g_listScroll = 0;
        // Al cambiar de ampli se desplaza la lista para que el que suena quede
        // siempre a la vista (p. ej. KRAFT V4).
        {
            const int sel = g_params.model.load();
            int pos = -1;
            for (int i = 0; i < cnt; ++i) if (rows[i] == sel) { pos = i; break; }
            static int lastKey = -1;
            const int key = (pos & 0xFFFF);
            if (pos >= 0 && key != lastKey) {
                if (pos < g_listScroll) g_listScroll = pos;
                else if (pos >= g_listScroll + vis) g_listScroll = pos - vis + 1;
                if (g_listScroll > listScrollMax()) g_listScroll = listScrollMax();
                if (g_listScroll < 0) g_listScroll = 0;
            }
            lastKey = (pos >= 0) ? key : -1;
        }
        G::Font fRowN(kFontUI, kTNombre, G::FontStyleBold);
        G::Font fRowS(kFontTec, kTPico);
        const int cur = g_params.model.load();
        for (int vi = 0; vi < vis; ++vi) {
            const int gi = g_listScroll + vi;
            if (gi >= cnt) break;
            const int a = rows[gi];
            const RECT rp = navRowPaintRect(vi);    // la fila, de UNA definicion
            const int rx = rp.left, ry = rp.top;
            const int rw = rp.right - rp.left, rh = rp.bottom - rp.top;
            const bool sel = (a == cur);
            const bool hov = rectHas(rp, g_mouseX, g_mouseY);
            // el hover entra/sale en kRampHover ms: la luz se vana/recupera sola,
            // en vez de encenderse y apagarse de golpe
            const float hr = (gi >= 0 && gi < kMaxNavRows)
                               ? g_anim.row[gi] : (hov ? 1.0f : 0.0f);
            if (sel) {
                // SELECCIONADA: la fila SUBE un tono (kFSurf2) y lleva su barra ambar en el
                // canto. El ambar significa "esto suena" y con la barra basta:
                // antes la fila llevaba tambien contorno ambar entero y tinte de
                // cristal (cuatro seÃ±ales para un solo estado) y la lista leia
                // como un panel de alarmas.
                G::SolidBrush sb2(kFSurf2);
                roundRect(g, (G::REAL)rx, (G::REAL)ry, (G::REAL)rw, (G::REAL)rh, 6.0f, nullptr, &sb2);
                G::SolidBrush ab(kAcc);
                roundRect(g, (G::REAL)rx + 2.0f, (G::REAL)ry + 11.0f, 3.0f, (G::REAL)rh - 22.0f,
                           1.5f, nullptr, &ab);
            } else if (hr > 0.001f) {
                // hover: UN tono mas (kFSurf3) y rampado. La luz que "pasaba por
                // el cristal" era del lenguaje viejo.
                G::SolidBrush hb(G::Color((BYTE)(kFSurf3.GetA()*hr), kFSurf3.GetR(),
                                          kFSurf3.GetG(), kFSurf3.GetB()));
                roundRect(g, (G::REAL)rx, (G::REAL)ry, (G::REAL)rw, (G::REAL)rh, 6.0f, nullptr, &hb);
            }
            // miniatura: marco de cristal + frontal recortado a las esquinas
            const G::REAL tx = (G::REAL)(rx + 10), ty = (G::REAL)(ry + 6);
            const G::REAL tw = 88.0f, th = 40.0f;
            // celda ya rasterizada (ver ampThumb): escalar la foto aqui
            // costaba ~4 ms por fila, ~40 ms de frame con la lista llena
            G::Bitmap* tb = ampThumb(a, !sel);
            if (tb) {
                g.DrawImage(tb, tx, ty, tw, th);
            } else {
                G::SolidBrush mbc(G::Color(0xff, 0x0c, 0x0c, 0x0e));
                G::Pen mpc(G::Color(46, 0x9c, 0x98, 0x90), 1.0f);
                roundRect(g, tx, ty, tw, th, 5.0f, &mpc, &mbc);
            }
            // El nombre y el subtitulo CABEN: si no, se bajan medio punto y solo
            // como ultimo recurso se recortan (antes se leia "MARAUDER 80").
            drawTextFit(g, kAmpNames[a], G::FontStyleBold, kFTxt,
                        (G::REAL)(rx + 108), (G::REAL)(ry + 6), (G::REAL)(rw - 140), 22.0f,
                        kTNombre, 11.0f);
            drawTextFit(g, kAmpSubs[a], G::FontStyleRegular, kFTxt2,
                        (G::REAL)(rx + 108), (G::REAL)(ry + 28), (G::REAL)(rw - 140), 16.0f,
                        kTPico, 7.0f, kFontTec);
            // estrella de favoritos
            {
                const bool fav = ampFav(a);
                // A plena intensidad solo si es favorito o la fila esta bajo
                // el raton: trece estrellas encendidas de seguido llenaban de
                // ruido el borde derecho de la lista y competian con los
                // nombres. En reposo quedan como un contorno tenue.
                const G::Color stc = fav ? colAmp(a)
                                         : (hov ? kFTxt2
                                                : G::Color(88, 0x8b, 0x87, 0x81));
                icStar(g, (G::REAL)rx + rw - 15.0f, (G::REAL)ry + 26.0f, 7.0f, stc, fav);
            }
        }
        // Estado vacio: sin esto una busqueda sin resultados dejaba el hueco en
        // blanco y no se distinguia de "la lista no ha cargado". La lista puede
        // quedar vacia por DOS filtros (el buscador y la estrella de favoritos),
        // asi que el aviso nombra el que este puesto.
        if (cnt == 0) {
            G::Font fEmp(kFontTec, kTRotulo, G::FontStyleRegular);
            G::SolidBrush em(kFTxt2);
            G::StringFormat sfc2; sfc2.SetAlignment(G::StringAlignmentCenter);
            g.DrawString(tr(L"SIN AMPLIFICADORES CON ESE FILTRO"), -1, &fEmp,
                         G::RectF((G::REAL)(COL_L_X + 8), (G::REAL)(LIST_Y + 44),
                                  (G::REAL)(COL_L_W - 16), 24.0f), &sfc2, &em);
            G::Font fEmp2(kFontUI, kTCuerpo, G::FontStyleRegular);
            G::StringFormat sfc3; sfc3.SetAlignment(G::StringAlignmentCenter);
            g.DrawString(tr(g_favOnly ? L"la estrella filtra: solo tus amplis marcados"
                                   : L"borra el filtro de la caja de busqueda"), -1, &fEmp2,
                         G::RectF((G::REAL)(COL_L_X + 8), (G::REAL)(LIST_Y + 68),
                                  (G::REAL)(COL_L_W - 16), 20.0f), &sfc3, &em);
        }
        // barra de scroll minimalista
        if (cnt > vis) {
            const G::REAL trackX = (G::REAL)(COL_L_X + COL_L_W - 6);
            G::SolidBrush trb(G::Color(255, 0x18, 0x18, 0x1b));
            roundRect(g, trackX, (G::REAL)LIST_Y, 3.0f, (G::REAL)LIST_H, 1.5f, nullptr, &trb);
            const G::REAL th = (G::REAL)LIST_H * ((G::REAL)vis/(G::REAL)cnt);
            const G::REAL ty = (G::REAL)LIST_Y +
                               ((G::REAL)LIST_H - th) * ((G::REAL)g_listScroll/(G::REAL)(cnt - vis));
            G::SolidBrush thb(kAcc);
            roundRect(g, trackX, ty, 3.0f, th, 1.5f, nullptr, &thb);
        }
    }

    profMark("Slots del pedalboard ---");   //PFMARK
    // --- Slots del pedalboard ---
    for (int row = 0; row < 2; ++row) {
        const std::vector<int>& ch = (row == 0) ? g_chainPre : g_chainPost;
        for (size_t i = 0; i < ch.size(); ++i) {
            const int t = ch[i];
            if (row == g_dragPedalRow && (int)i == g_dragPedalIdx) {
                // hueco de origen: la tarjeta entera (con switch y perillas) va en el
                // fantasma; aqui queda solo la marca del sitio que deja libre
                G::Pen hpo(G::Color(90, 0x39, 0x36, 0x33), 1.4f);
                hpo.SetDashStyle(G::DashStyleDash);
                roundRect(g, pedSlotX((int)i), PED_ROW_Y[row], PED_SLOT_W, PED_SLOT_H, 10, &hpo, nullptr);
                continue;
            }
            const G::REAL sx = pedSlotX((int)i), sy = PED_ROW_Y[row];
            // La plantilla entra por GDI+ con la transformacion puesta: misma
            // escala y mismo desplazamiento que el switch, el aspa y las perillas
            // que se pintan encima (ver el comentario del struct).
            if (g_slotTmpl[t].gp) {
                g.DrawImage(g_slotTmpl[t].gp, sx - (G::REAL)PED_TMPL_M,
                            sy - (G::REAL)PED_TMPL_M,
                            (G::REAL)PED_TMPL_W, (G::REAL)PED_TMPL_H);
            }
            const G::Color ac = colFx(t);
            if (rectHas(pedSlotBox((int)sx, (int)sy), g_mouseX, g_mouseY)) {
                G::Pen bp(G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()), 1.8f);
                roundRect(g, sx, sy, PED_SLOT_W, PED_SLOT_H, 10, &bp, nullptr);
            }
            // switch mini (la pista esta en la plantilla; aqui perilla + glow).
            // El origen es LA CHAPA: la funcion coloca pista y LED con las mismas
            // constantes que la plantilla, asi que el +68/+16 que llevaba aqui los
            // sacaba de la chapa (pista en mod_x+98..150 y mod_y+106..120).
            drawPedalSwitchKnob(g, t, sx, sy);
            // boton quitar: en el bolsillo inferior derecho de la chapa. La pista
            // acaba en x+82 y la etiqueta derecha en y+85, asi que el hueco limpio
            // es 88..104 en ambos ejes. Antes estaba en 77..93 x 84..100 y se
            // montaba sobre el final de la pista (y el clic de la pista caia aqui).
            {
                const G::REAL cx = sx + PED_RM_DX, cy = sy + PED_RM_DY;
                // El aspa se pone roja cuando el pedal esta armado: es el mismo
                // feedback que el aspa de los presets. Sin esto, el segundo clic
                // borra y el usuario no sabia que el primero habia hecho algo.
                const int pedKey = row*100 + (int)i;
                const bool armed = (g_pedDelArm == pedKey) &&
                                   (GetTickCount() - g_pedDelArmAt) < 4000;
                const bool hx = armed || mouseIn((int)(cx - 14), (int)(cy - 14),
                                                 (int)(cx + 14), (int)(cy + 14));
                G::SolidBrush xb(hx ? G::Color(0xff, 0x27, 0x25, 0x27) : G::Color(0xff, 0x1c, 0x1b, 0x1d));
                g.FillEllipse(&xb, cx - 8, cy - 8, 16.0f, 16.0f);
                G::Pen xr(hx ? G::Color(0x96, 0x6e, 0x69, 0x63) : G::Color(70, 0x9c, 0x98, 0x90), 1.0f);
                g.DrawEllipse(&xr, cx - 8, cy - 8, 16.0f, 16.0f);
                icClose(g, (G::REAL)cx, (G::REAL)cy, 5.6f,
                        hx ? kHot : G::Color(0xff, 0x82, 0x7d, 0x76));
            }
        }
        // slot fantasma "+ ANADIR"
        {
            const int cnt = (int)((row == 0) ? g_chainPre.size() : g_chainPost.size());
            if (cnt < PED_MAX_ROW) {
                const G::REAL sx = pedSlotX(cnt), sy = PED_ROW_Y[row];
                const bool hov = rectHas(pedSlotBox((int)sx, (int)sy), g_mouseX, g_mouseY);
                G::Pen dp(hov ? kAcc : G::Color(0xff, 0x39, 0x36, 0x33), 1.6f);
                dp.SetDashStyle(G::DashStyleDash);
                roundRect(g, sx, sy, PED_SLOT_W, PED_SLOT_H, 10, &dp, nullptr);
                // El "+" es un icono de la familia, no un glifo: el glifo (Bahnschrift
                // 24 en negrita) traia su propio peso y su propio ojo y era el unico
                // signo de la app que no salia de icXxx. Mismo centro optico que el
                // glifo (el "+" grande quedaba alto cuando se le ponia la leyenda
                // debajo).
                const G::Color pc = hov ? kAcc : G::Color(0xff, 0x6e, 0x69, 0x63);
                icPlus(g, sx + PED_SLOT_W*0.5f, sy + 32.0f, 11.0f, pc);
                G::SolidBrush pb(pc);
                G::Font fAdd(kFontTec, kTRotulo, G::FontStyleBold);
                g.DrawString(tr(L"A\u00D1ADIR"), -1, &fAdd, G::RectF(sx, sy + 58, PED_SLOT_W, 16), &sfC, &pb);
                pressVeil(g, sx, sy, (G::REAL)PED_SLOT_W, (G::REAL)PED_SLOT_H, 10.0f);
            }
        }
    }

    // --- Fantasma de arrastre + indicador de insercion ---
    // El fantasma es la tarjeta ENTERA: cuerpo, switch y las dos perillas. Si
    // solo se moviera el cuerpo, las perillas del pedal arrastrado se quedarian
    // pintadas en el hueco de origen (eso es lo que se veia de fondo).
    if (g_dragPedalRow >= 0 && g_dragPedalType >= 0) {
        const G::REAL gx = (G::REAL)(g_mouseX - PED_TMPL_W/2);
        const G::REAL gy = (G::REAL)(g_mouseY - PED_TMPL_H/2);
        const G::REAL ox = (G::REAL)PED_TMPL_M, oy = (G::REAL)PED_TMPL_M;  // tarjeta dentro del margen
        // La copia translucida se prepara al empezar el arrastre y se dibuja por
        // GDI+ con la transformacion puesta (ver freeGhostCard/buildGhostCard).
        if (g_ghostType != g_dragPedalType) buildGhostCard(g_dragPedalType);
        if (g_ghostBmp) {
            g.DrawImage(g_ghostBmp, gx, gy, (G::REAL)PED_TMPL_W, (G::REAL)PED_TMPL_H);
        }
        // El fantasma blitea la plantilla y dibuja encima lo mismo que la capa
        // viva: con LA CHAPA como origen (gx+ox es la esquina de la chapa dentro
        // del margen de 6px). Antes llevaba +68/+16 y perillas en 68/152: la
        // segunda se iba fuera de la tarjeta (152 > 112).
        drawPedalSwitchKnob(g, g_dragPedalType, gx + ox, gy + oy);
        {
            const G::Color ac = colFx(g_dragPedalType);
            for (int k = 0; k < 2; ++k) {
                const G::REAL kx = gx + ox + (k == 0 ? PED_KB1_X : PED_KB2_X);
                const G::REAL ky = gy + oy + PED_KB_Y;
                drawKnobDetail(g, kx, ky, PED_KB_R, pedalKnobV01(g_dragPedalType, k), ac, false, false);
            }
        }
        if (g_dropSwap >= 0 && g_dropRow >= 0) {
            // Intercambio: se marca la PLAZA que va a cambiar, no un hueco.
            G::Pen ip(kAcc, 2.2f);
            roundRect(g, pedSlotX(g_dropSwap), PED_ROW_Y[g_dropRow],
                      PED_SLOT_W, PED_SLOT_H, 10.0f, &ip, nullptr);
        } else if (g_dropRow >= 0) {
            const G::REAL ix = pedSlotX(g_dropIdx), iy = PED_ROW_Y[g_dropRow];
            G::Pen ip(kAcc, 2.4f);
            g.DrawLine(&ip, ix - 4.0f, iy - 2.0f, ix - 4.0f, iy + PED_SLOT_H + 2.0f);
        }
    }

    profMark("Panel modal: ANADIR PEDAL ---");   //PFMARK
    // --- Panel modal: ANADIR PEDAL ---
    if (g_addOpen) {
        G::SolidBrush dim(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
        g.FillRectangle(&dim, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
        const G::REAL px = PAL_X, py = PAL_Y, pw = PAL_W, ph = PAL_H;
        glassPanel(g, px, py, pw, ph, kRModal, true);
        drawTxFit(g, L"A\u00D1ADIR PEDAL", G::FontStyleBold, kAcc,
                      px + 16, py + 18, 2.0f, pw - 32, TxMarca);
        for (int t = 0; t < 18; ++t) {
            const int col = t % 3, r = t / 3;
            const G::REAL tx = px + PAL_TX0 + col*PAL_TILE_DX, ty = py + PAL_TY0 + r*PAL_TILE_DY;
            const bool inCh = chainHas(t);
            const bool hov = !inCh && mouseIn((int)tx, (int)ty, (int)(tx + PAL_TILE_W), (int)(ty + PAL_TILE_H));
            G::Color ac = colFx(t);
            G::SolidBrush tb(hov ? kFSurf3 : kFSurf2);
            G::Pen tp(inCh ? kFLine
                     : hov  ? G::Color(255, ac.GetR(), ac.GetG(), ac.GetB())
                            : kFLine, 1.6f);
            roundRect(g, tx, ty, PAL_TILE_W, PAL_TILE_H, 9, &tp, &tb);
            drawFxGlyph(g, t, tx + 32.0f, ty + 34.0f, 22.0f,
                        inCh ? G::Color(0xff, 0x5a, 0x56, 0x51)
                             : G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()));
            WCHAR wn[64];
            MultiByteToWideChar(CP_ACP, 0, kFxNames[t], -1, wn, 64);
            drawTxFit(g, wn, G::FontStyleBold,
                          inCh ? G::Color(0xff, 0x6e, 0x69, 0x63) : G::Color(255, 238, 244, 247),
                          tx + 58, ty + 14, 0.4f, PAL_TILE_W - 70.0f, TxNombre);
            G::SolidBrush sb(inCh ? G::Color(0xff, 0x5a, 0x56, 0x51) : kAcc);
            g.DrawString(tr(inCh ? L"YA EN LA CADENA" : L"CLIC PARA A\u00D1ADIR"), -1, &fSmall,
                         G::RectF(tx + 58, ty + 44, PAL_TILE_W - 70.0f, 18), nullptr, &sb);
            if (!inCh) pressVeil(g, tx, ty, (G::REAL)PAL_TILE_W, (G::REAL)PAL_TILE_H, 9.0f);
        }
        // boton CANCELAR
        {
            const G::REAL bx = px + pw/2 - 90, by = py + ph - 58;
            const bool hov = mouseIn((int)bx, (int)by, (int)(bx + 180), (int)(by + 40));
            G::SolidBrush cb(hov ? G::Color(0xff, 0x48, 0x45, 0x41) : G::Color(0xff, 0x39, 0x36, 0x33));
            roundRect(g, bx, by, 180, 40, 8, nullptr, &cb);
            G::SolidBrush tb(G::Color(255, 255, 255, 255));
            g.DrawString(tr(L"CANCELAR"), -1, &fPreset, G::RectF(bx, by, 180, 40), &sfC, &tb);
            pressVeil(g, bx, by, 180, 40, 8.0f);
        }
    }

    profMark("Panel modal: TONE3000 ---");   //PFMARK
    // --- Panel modal: explorador TONE3000 ---
    if (g_t3kOpen) t3kDraw(g);

    profMark("Todos los knobs vivos (amp + pedales +");   //PFMARK
    // --- Todos los knobs vivos (amp + pedales + MIX + master) ---
    // Con un modal abierto no se pintan: el modal debe ocultarlos por completo
    const bool knobsVisible = !g_settingsOpen && !g_addOpen && !g_tunerOpen.load() && !g_t3kOpen;
    // con un pedal en el aire, sus dos perillas viajan con el fantasma
    int dragKnobFi = -1;
    if (g_dragPedalRow >= 0 && g_dragPedalIdx >= 0) {
        const int preN = (int)g_chainPre.size();
        dragKnobFi = (g_dragPedalRow == 0) ? g_dragPedalIdx : preN + g_dragPedalIdx;
    }
    for (size_t i = 0; knobsVisible && i < g_knobs.size(); ++i) {
        // el pedal que va en el aire pinta sus perillas dentro del fantasma:
        // dejarlas aqui las mantenia clavadas en el hueco de origen
        if (dragKnobFi >= 0 && i >= 6 && i + 2 < (int)g_knobs.size() &&
            (int)((i - 6)/2) == dragKnobFi) continue;   // nunca INPUT/MASTER
        const KnobUI& k = g_knobs[i];
        float v01 = (k.value->load() - k.vmin)/(k.vmax - k.vmin);
        if (v01 < 0) v01 = 0;
        if (v01 > 1) v01 = 1;
        // acento por contexto: master / amp activo / tipo de pedal
        G::Color ac;
        if (i + 1 == g_knobs.size()) {                  // MASTER (el ultimo)
            ac = kAcc;
        } else if (i + 2 == g_knobs.size()) {           // INPUT trim (el penultimo)
            ac = kAcc;       // verde del medidor
        } else if (i < 6) {
            ac = colAmp(g_params.model.load());
        } else {
            const int fi   = (int)((i - 6) / 2);
            const int preN = (int)g_chainPre.size();
            int type = 0;
            if (fi < preN) type = g_chainPre[fi];
            else if (fi - preN < (int)g_chainPost.size()) type = g_chainPost[fi - preN];
            ac = colFx(type);
        }
        int hdx = g_mouseX - k.x, hdy = g_mouseY - k.y;
        bool hov = (hdx*hdx + hdy*hdy) <= (k.r + 10)*(k.r + 10);
        // knobs del amp sobre el frontal: filmstrip del hueco (radio = artKnobR).
        // Antes se comparaba k.r==35 y nunca coincidia (el registro usa r=33),
        // asi que pintaban drawKnobDetail con ticks anchos y se solapaban.
        // Los de banda (BASS/MIDDLE/TREBLE del TWEED) entran aqui por tener
        // hueco: mismo filmstrip, otro tamano (el escalado sale del cache).
        const int ampArtR = (int)(artKnobR() + 0.5f);
        const int mdl = g_params.model.load();
        if ((k.r == ampArtR || (k.amp == mdl && k.hole >= 0)) && g_artOk[mdl] && g_stripImg[mdl])
            drawArtKnob(g, g_stripImg[mdl], (G::REAL)k.x, (G::REAL)k.y, (G::REAL)k.r, v01, ac, hov, (int)i == g_dragIdx);
        else
            drawKnobDetail(g, (G::REAL)k.x, (G::REAL)k.y, (G::REAL)k.r, v01, ac, hov, (int)i == g_dragIdx);
        if ((int)i == g_dragIdx) {
            WCHAR wv[32];
            float val = k.value->load();
            if (k.dB) swprintf_s(wv, L"%.0f dB", val);
            else      swprintf_s(wv, L"%.0f", val);
            const G::REAL bw2 = 64.0f, bh2 = 24.0f;
            G::REAL bx2 = (G::REAL)k.x - bw2/2, by2 = (G::REAL)k.y - k.r - bh2 - 10.0f;
            if (by2 < 62.0f) by2 = (G::REAL)k.y + k.r + 12.0f;   // mando alto: debajo
            drawValuePop(g, bx2, by2, bw2, bh2, wv, ac);
        } else if (hov && knobsVisible) {
            // Tooltip al pasar el raton: nombre del knob + valor actual
            WCHAR wl2[64] = L"";
            MultiByteToWideChar(CP_ACP, 0, k.label, -1, wl2, 64);
            WCHAR wv2[32];
            const float val2 = k.value->load();
            if (k.dB) swprintf_s(wv2, L"%.0f dB", (double)val2);
            else      swprintf_s(wv2, L"%.0f%%", (double)val2);
            WCHAR wfull[112];
            swprintf_s(wfull, L"%s  %s", wl2, wv2);
            G::Font fTip(kFontTec, kTRotulo, G::FontStyleBold);
            const G::REAL bw3 = 176.0f, bh3 = 24.0f;
            G::REAL bx3 = (G::REAL)k.x - bw3/2.0f;
            if (bx3 < 12.0f) bx3 = 12.0f;                                   // nunca sale de la ventana
            if (bx3 > (G::REAL)WIN_W - 12.0f - bw3) bx3 = (G::REAL)WIN_W - 12.0f - bw3;
            G::REAL by3 = (G::REAL)k.y - k.r - 40.0f;
            if (by3 < 62.0f) by3 = (G::REAL)k.y + k.r + 12.0f;   // knobs altos: tooltip debajo
            G::SolidBrush tb3(kWellA);
            G::Pen tp3(G::Color(255, ac.GetR(), ac.GetG(), ac.GetB()), 1.5f);
            roundRect(g, bx3, by3, bw3, bh3, 6, &tp3, &tb3);
            G::SolidBrush tt3(kFTxt);
            g.DrawString(wfull, -1, &fTip, G::RectF(bx3, by3, bw3, bh3), &sfC, &tt3);
        }
    }

    profMark("Bloque MEZCLA (panel derecho): recorrido, mango y valor");   //PFMARK
    // --- MEZCLA: el recorrido, el mango y el valor --------------------------
    // El valor ya se lee SIEMPRE en la linea del rotulo (antes solo aparecia
    // mientras arrastrabas): la cifra es el dato del control, no una ayuda.
    {
        const bool vivo = g_mixDrag || zoneHit(101);      // pulsado o bajo el raton
        const G::REAL capX = mixCapX((G::REAL)(g_params.mixCab.load()/100.0f));
        // El recorrido: la parte de cabina de la mezcla. Un tono calido quedo
        // (gris de mando tibio, no marron sobre el pozo) y al arrastrar sube al
        // ambar vivo: lo pulsado es lo que lleva el acento.
        const G::REAL fw = capX - (G::REAL)MIX_SL_X - 1.0f;
        if (fw > 2.0f) {
            G::SolidBrush fb(flatTint(kFSurf3, kAcc, g_mixDrag ? 0.80f : 0.00f));
            roundRect(g, (G::REAL)MIX_SL_X + 1.0f, (G::REAL)MIX_SL_Y + 1.0f, fw,
                      (G::REAL)(MIX_SL_H - 2), (G::REAL)(MIX_SL_H - 2)*0.5f, nullptr, &fb);
        }
        // El mango: una tecla plana de 7x20, centrada en el valor y SIEMPRE
        // dentro de la pista (el recorrido va metido media manija por extremo).
        {
            const G::REAL hw = (G::REAL)MIX_HND_W, hh = (G::REAL)MIX_HND_H;
            const G::REAL hx = capX - hw*0.5f;
            const G::REAL hy = (G::REAL)MIX_SL_Y + (G::REAL)MIX_SL_H*0.5f - hh*0.5f;
            G::SolidBrush hb(kFTxt);
            roundRect(g, hx, hy, hw, hh, 3.0f, nullptr, &hb);
            G::Pen hp(vivo ? kAcc : G::Color(255, 0x0b, 0x0b, 0x0c), vivo ? 1.6f : 1.0f);
            roundRect(g, hx, hy, hw, hh, 3.0f, &hp, nullptr);
        }
        // El valor, a la derecha de la linea del rotulo
        {
            WCHAR wv[32];
            swprintf_s(wv, L"%.0f %%", g_params.mixCab.load());
            G::Font fVal(kFontMono, kTValor, G::FontStyleRegular);
            const G::REAL vw = measureSpaced(g, wv, fVal, 0.15f);
            drawSpaced(g, wv, fVal, vivo ? kAcc : kFTxt,
                       (G::REAL)(MIX_SL_X + MIX_SL_W) - vw, (G::REAL)MIX_LBL_Y + 1.0f, 0.15f);
        }
    }

    profMark("Barra inferior dinamica ---");   //PFMARK
    // --- Barra inferior dinamica ---
    {
        WCHAR pname[128];
        presetNameW(g_presetIdx, pname, 128);
        if (zoneHit(70)) {                     // preset inferior
            G::SolidBrush pb(G::Color(255, 0x10, 0x10, 0x12));
            G::Pen pp(G::Color(255, 0x18, 0x18, 0x1b), 1.0f);
            roundRect(g, 24, BOT_BOX_Y, 306, 40, 8, &pp, &pb);
        }
        G::SolidBrush tb(kFTxt);
        {
            G::StringFormat sfL;
            sfL.SetLineAlignment(G::StringAlignmentCenter);
            sfL.SetFormatFlags(G::StringFormatFlagsNoWrap | G::StringFormatFlagsNoClip);
            // El nombre se AJUSTA a su caja: un nombre de usuario largo se
            // cortaba a media palabra ("CLEAN DIAMOND WITH A VER").
            const std::wstring t = fitEllipsis(g, pname, fPreset, 0.0f, 242.0f);
            g.DrawString(t.c_str(), -1, &fPreset, G::RectF(38, BOT_BOX_Y, 250, 40), &sfL, &tb);
        }

        const G::Color acCol  = zoneHit(71) ? kAcc : G::Color(0xff, 0xb7, 0xb1, 0xa7);
        const G::Color acCol2 = zoneHit(72) ? kAcc : G::Color(0xff, 0xb7, 0xb1, 0xa7);
        // Los tres glifos de la tira de transporte, en UNA linea (centros en
        // BOT_BOX_Y+20 = 890, el centro de la tira): antes cada uno traia la
        // altura de su fuente.
        icChev(g, 350.0f, (G::REAL)BOT_BOX_Y + 20.0f, 13.0f, acCol, 1);
        icChev(g, 386.0f, (G::REAL)BOT_BOX_Y + 20.0f, 13.0f, acCol2, 2);
        bool fhov = zoneHit(73);               // favorito
        const bool fv = presetFav(g_presetIdx);   // el PRESET que suena
        const G::Color hbC = fv    ? kHot
                           : fhov  ? G::Color(0xff, 0xd8, 0x9c, 0x98)
                                   : G::Color(0xff, 0x82, 0x7d, 0x76);
        icHeart(g, 427.0f, (G::REAL)BOT_BOX_Y + 20.0f, 7.5f, hbC, fv);

        pressVeil(g, 24.0f, (G::REAL)BOT_BOX_Y, 306.0f, 40.0f, 8.0f);
        pressVeil(g, 334.0f, (G::REAL)BOT_BOX_Y + 2, 32.0f, 36.0f, 6.0f);
        pressVeil(g, 370.0f, (G::REAL)BOT_BOX_Y + 2, 32.0f, 36.0f, 6.0f);
        pressVeil(g, 410.0f, (G::REAL)BOT_BOX_Y + 2, 34.0f, 36.0f, 6.0f);
        drawMeterFill(g, (G::REAL)MTR_IN_X,  (G::REAL)BOT_MTR_Y, (G::REAL)MTR_IN_W,  g_params.meterIn.load(),  g_pkHoldIn);
        drawMeterFill(g, (G::REAL)MTR_OUT_X, (G::REAL)BOT_MTR_Y, (G::REAL)MTR_OUT_W, g_params.meterOut.load(), g_pkHoldOut);

        // Reduccion del gate (0 dB = puerta abierta, barra vacia)
        {
            float gr = -g_grGateDb.load();
            if (gr < 0.0f) gr = 0.0f;
            if (gr > 40.0f) gr = 40.0f;
            drawMeterFill(g, (G::REAL)MTR_GT_X, (G::REAL)BOT_MTR_Y, (G::REAL)MTR_GT_W, gr / 40.0f, g_pkHoldGr);
        }
        // LED de clip + etiqueta: bajo el medidor OUTPUT, centrado con su
        // canal â€” lejos de los ticks del MASTER y de la etiqueta GATE
        {
            static float hold = 0.0f;
            const float fl = g_clipFlag.exchange(0.0f);
            hold = (fl > hold) ? fl : hold*0.90f;   // decaimiento ~1 s a 25 fps
            const G::REAL cx = (G::REAL)CLIP_LED_X + 5.5f, cy = (G::REAL)BOT_MTR_Y + 13.0f, r = 5.5f;
            if (hold > 0.05f) {
                G::SolidBrush gl(G::Color(0x78, 0x6e, 0x69, 0x63));
                g.FillEllipse(&gl, cx - r - 5.0f, cy - r - 5.0f, (r + 5.0f)*2.0f, (r + 5.0f)*2.0f);
            }
            G::SolidBrush led(hold > 0.05f ? kHot
                                           : G::Color(0xff, 0x21, 0x1f, 0x21));
            g.FillEllipse(&led, cx - r, cy - r, r*2.0f, r*2.0f);
        }
    }

    profMark("Linea de estado ---");   //PFMARK
    // --- Linea de estado ---
    {
        AudioSettings stx;
        { std::lock_guard<std::mutex> lock(g_settingsMutex); stx = g_appliedSet; }
        // Los nombres de dispositivo ya son std::wstring: se usan TAL CUAL (antes
        // pasaban a ANSI, se cortaban a 32 caracteres y volvian a ancho).
        WCHAR inNm[160] = L"", outNm[160] = L"";
        {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            if (!g_inputDevices.empty() && g_selectedInputIdx >= 0 &&
                g_selectedInputIdx < (int)g_inputDevices.size())
                wcsncpy_s(inNm, g_inputDevices[g_selectedInputIdx].name.c_str(), _TRUNCATE);
            if (!g_outputDevices.empty() && g_selectedOutputIdx >= 0 &&
                g_selectedOutputIdx < (int)g_outputDevices.size())
                wcsncpy_s(outNm, g_outputDevices[g_selectedOutputIdx].name.c_str(), _TRUNCATE);
        }
        int rate = kRates[stx.rateIdx], bsz = kBuffers[stx.bufIdx];
        const bool en = g_audioEnabled.load();
        const bool sessOn = g_params.audioActive.load();
        const WCHAR* audioWord = !en ? tr(L"SILENCIO") : (sessOn ? L"ONLINE" : L"OFFLINE");
        const WCHAR* recWord = g_recActive.load() ? L" + REC" : L"";
        WCHAR wpname[96];
        presetNameW(g_presetIdx, wpname, 96);   // incluye presets de usuario (sin leer fuera de g_presets)
        // El estado de audio NO es un campo mas: es el unico dato de la linea que
        // no es un par rotulo+valor y el que el usuario tiene que encontrar de un
        // vistazo cuando no suena nada. Va en PASTILLA (cuerpo propio), con el
        // ambar vivo si la sesion esta en linea, ambar tenue si esta apagado (el
        // aviso con el que arranca la app) y el rojo de alerta si el motor no
        // pudo abrir el dispositivo. El piloto solo se enciende si hay sesion.
        WCHAR wstate[64];
        swprintf_s(wstate, L"%s%s", audioWord, recWord);
        // hairline sobre la linea de estado: la separa del pie sin recuadros
        hairSep(g, 24.0f, (G::REAL)(BOT_STAT_Y - 7), (G::REAL)(WIN_W - 48), 24);
        {
            const bool eIn  = (inNm[0]  == 0);     // sin dispositivo elegido
            const bool eOut = (outNm[0] == 0);
            // Con el motor EN MARCHA se ensena lo que corre de verdad: en
            // compartido el dispositivo fija su periodo (el ajuste de BUFFER solo
            // puede subirlo) y el modo exclusivo puede haber caido a compartido.
            // Parado, se ensena lo que se ha pedido en AJUSTES.
            const bool sessOn2 = g_params.audioActive.load();
            const int  sRate = sessOn2 ? g_sessRate.load() : 0;
            const int  sBuf  = sessOn2 ? g_sessBufFrames.load() : 0;
            const bool sExcl = sessOn2 ? (g_sessExclusive.load() != 0) : (stx.system == 1);
            if (sRate > 0) rate = sRate;
            if (sBuf  > 0) bsz  = sBuf;
            WCHAR weng[64], winN[160], woutN[160], wrate[64], wbuf[96], wpres[160];
            wcscpy_s(weng, tr(sExcl ? L"WASAPI EXCLUSIVO" : L"WASAPI COMPARTIDO"));
            wcscpy_s(winN, inNm);
            wcscpy_s(woutN, outNm);
            swprintf_s(wrate, L"%d Hz", rate);
            swprintf_s(wbuf, tr(L"%d muestras (%.1f ms)"), bsz, bsz*1000.0/rate);
            wcscpy_s(wpres, wpname);

            const G::REAL py = (G::REAL)BOT_STAT_Y;
            G::REAL px = 24.0f;
            G::RectF mb;
            G::SolidBrush bLbl(kFTxt3), bVal(kFTxt2);
            G::Pen fsep(kFLine, 1.0f);
            // --- campo 0: AUDIO, en pastilla (ver statPill) ---
            {
                // Un solo criterio de color para el estado de audio, el mismo que
                // el boton de la cabecera: AMBAR = suena, ROJO = no suena. Antes
                // apagado iba en ambar translucido y "encendido sin sesion" en
                // rojo, asi que el ambar decia dos cosas contrarias a la vez.
                const G::Color stCol = !en   ? kFOff
                                     : sessOn ? kAcc
                                              : kHot;
                px += statPill(g, px, py, wstate, stCol, sessOn, fSmall) + 2.0f;
            }
            // El resto son CAMPOS. Y una casilla VACIA se apaga ENTERA (rotulo y
            // valor al mismo tono) con una raya larga en el valor: antes decia "-"
            // con el mismo peso que un dato, asi que "sin dispositivo elegido"
            // parecia un dato mas.
            const WCHAR* fl[6] = { tr(L"MOTOR"), L"IN", L"OUT", tr(L"FREC"), tr(L"BUFFER"), L"PRESET" };
            const WCHAR* fv[6] = { weng, winN, woutN, wrate, wbuf, wpres };
            const bool   vc[6] = { false, eIn, eOut, false, false, false };
            for (int i = 0; i < 6; ++i) {
                G::SolidBrush& bl = vc[i] ? bLbl : bVal;
                const WCHAR*   v  = vc[i] ? L"\u2014" : fv[i];
                g.DrawLine(&fsep, px + 7.0f, py + 2.0f, px + 7.0f, py + 15.0f);
                px += 14.0f;
                g.DrawString(fl[i], -1, &fSmall, G::RectF(px, py, 260.0f, 22), nullptr, &bLbl);
                g.MeasureString(fl[i], -1, &fSmall, G::RectF(0, 0, 900, 24), nullptr, &mb);
                px += mb.Width + 5.0f;
                // Lo que no cabe en el ancho que queda se corta con puntos
                // suspensivos: un nombre largo de dispositivo o de preset no
                // puede empujar la linea fuera de la ventana.
                const std::wstring vs = fitEllipsis(g, v, fSmall, 0.0f,
                    std::max((G::REAL)40.0f, (G::REAL)WIN_W - 24.0f - px));
                g.DrawString(vs.c_str(), -1, &fSmall, G::RectF(px, py, 620.0f, 22), nullptr, &bl);
                g.MeasureString(vs.c_str(), -1, &fSmall, G::RectF(0, 0, 900, 24), nullptr, &mb);
                px += mb.Width;
            }
        }
    }

    profMark("Buscador de presets (despliegue) ---");   //PFMARK
    // --- Despliegue del buscador de la cabecera ---
    if (g_topOpen) {
        const RECT dr = topDropRect();
        const G::REAL dx = (G::REAL)dr.left, dy = (G::REAL)dr.top;
        const G::REAL dw = (G::REAL)(dr.right - dr.left), dh = (G::REAL)(dr.bottom - dr.top);
        softShadow(g, dx, dy, dw, dh, 12.0f, 80);
        glassPanel(g, dx, dy, dw, dh, 12.0f, true);
        if (g_topCount <= 0) {
            drawTxFitC(g, L"sin resultados", G::FontStyleRegular, kFTxt3,
                       dx + dw*0.5f, dy + kTopPad + 7.0f, 0.4f, dw - 24.0f, TxCuerpo);
        } else {
            const int vis = (g_topCount - g_topScroll > kTopMaxRows)
                              ? kTopMaxRows : (g_topCount - g_topScroll);
            G::Font fNom(kFontUI, kTCuerpo);
            G::Font fAmp(kFontTec, kTPico, G::FontStyleRegular);
            G::StringFormat sfR;
            sfR.SetAlignment(G::StringAlignmentFar);
            sfR.SetLineAlignment(G::StringAlignmentCenter);
            sfR.SetTrimming(G::StringTrimmingEllipsisCharacter);
            sfR.SetFormatFlags(G::StringFormatFlagsNoWrap);
            for (int k = 0; k < vis; ++k) {
                const int li  = g_topScroll + k;
                const int idx = g_topList[li];
                const G::REAL ry = dy + kTopPad + k*kTopRowH;
                G::RectF rr(dx + 4.0f, ry, dw - 8.0f, (G::REAL)kTopRowH);
                const bool sel = (li == g_topSel);
                const bool hov = mouseIn((int)rr.X, (int)rr.Y, (int)(rr.X + rr.Width), (int)(rr.Y + rr.Height));
                if (sel || hov) {
                    G::SolidBrush rb(sel ? kFSurf3 : kFSurf2);
                    roundRect(g, rr.X, rr.Y, rr.Width, rr.Height, 6.0f, nullptr, &rb);
                }
                const bool live = (idx == g_presetIdx);
                if (live) {   // el que suena: la misma marca que la fila del navegador
                    G::SolidBrush ab(kAcc);
                    roundRect(g, rr.X + 2.0f, rr.Y + 7.0f, 3.0f, rr.Height - 14.0f, 1.5f, nullptr, &ab);
                }
                WCHAR nm[128];
                presetNameW(idx, nm, 128);
                drawTextFit(g, nm, G::FontStyleBold, live ? kAcc : (sel || hov ? kFTxt : kFTxt2),
                            rr.X + 12.0f, rr.Y, rr.Width - 142.0f, rr.Height,
                            kTCuerpo, 8.5f, kFontUI, false, true);
                const int gp = presetGroup(idx);
                const WCHAR* sub = (gp >= 0) ? tr(kAmpNames[gp]) : tr(L"SIN AMPLI");
                G::SolidBrush sb(live ? kFTxt3 : G::Color(0xc0, 0x8b, 0x87, 0x81));
                g.DrawString(sub, -1, &fAmp,
                             G::RectF(rr.X + rr.Width - 128.0f, rr.Y, 116.0f, rr.Height),
                             &sfR, &sb);
            }
        }
    }

    profMark("Panel AJUSTES (modal, kAcc) -");   //PFMARK
    // --- Panel AJUSTES (modal, paleta kAcc de la app) ---
    if (g_settingsOpen) {
        G::SolidBrush dim(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
        g.FillRectangle(&dim, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
        // Dialogo centrado y compacto: antes era un panel casi a pantalla completa con mucho vacio
        const G::REAL px = (WIN_W - kSetPanelW) / 2, py = (WIN_H - kSetPanelH) / 2 - 10;
        const G::REAL pw = kSetPanelW, ph = kSetPanelH;
        // Ejes de la rejilla (kSet*): todo lo que va dentro comparte x0/x1; la
        // pastilla del valor empieza en pillL y termina en x1.
        const G::REAL x0 = px + kSetX0, x1 = px + kSetX1;
        const G::REAL pillL = px + kSetPillL, pillR = x1, pillW = pillR - pillL;
        glassPanel(g, px, py, pw, ph, kRModal, true);
        // El titulo va en marfil, no en ambar: el ambar marca lo que suena o
        // esta pulsado, y una cabecera no es ninguna de las dos cosas.
        drawTxFit(g, L"AJUSTES DE AUDIO", G::FontStyleBold, kFTxt,
                      x0, py + 18.0f, 2.0f, pw - 2.0f*kSetX0, TxMarca);
        // Filete a TODO el ancho del contenido: antes era un subrayado de 190px
        // que en espanol casi tocaba el final del titulo y en ingles lo dejaba a
        // medio camino (se leia como un error de maquetacion).
        {
            G::Pen ul(kFLine, 1.0f);
            g.DrawLine(&ul, x0, py + 54.0f, x1, py + 54.0f);
        }

        AudioSettings st;
        { std::lock_guard<std::mutex> lock(g_settingsMutex); st = g_settings; }
        // Los valores ya nacen ANCHOS: el nombre del dispositivo es std::wstring
        // (antes se pasaba a ANSI y de vuelta) y los textos fijos se traducen.
        WCHAR wvEng[96] = L"", wvIn[160] = L"", wvOut[160] = L"", wvRate[64] = L"", wvBuf[96] = L"";
        wcscpy_s(wvEng, tr(st.system == 1 ? L"WASAPI EXCLUSIVO" : L"WASAPI COMPARTIDO"));
        // Si se pide exclusivo y el endpoint no lo acepta, el motor ya ha caido a
        // compartido: se dice aqui (y el detalle real esta en la linea de estado).
        if (st.system == 1 && g_params.audioActive.load() && g_sessExclusive.load() == 0)
            wcscat_s(wvEng, tr(L" (no disponible)"));
        {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            if (!g_inputDevices.empty() && g_setInIdx >= 0 && g_setInIdx < (int)g_inputDevices.size())
                wcsncpy_s(wvIn, g_inputDevices[g_setInIdx].name.c_str(), _TRUNCATE);
            else
                wcscpy_s(wvIn, tr(L"(sin dispositivos)"));
            if (!g_outputDevices.empty() && g_setOutIdx >= 0 && g_setOutIdx < (int)g_outputDevices.size())
                wcsncpy_s(wvOut, g_outputDevices[g_setOutIdx].name.c_str(), _TRUNCATE);
            else
                wcscpy_s(wvOut, tr(L"(sin dispositivos)"));
        }
        swprintf_s(wvRate, L"%d Hz", kRates[st.rateIdx]);
        int bsz = kBuffers[st.bufIdx];
        swprintf_s(wvBuf, tr(L"%d muestras (%.1f ms)"), bsz, bsz*1000.0/kRates[st.rateIdx]);

        const WCHAR* labels[6] = { tr(L"MOTOR DE AUDIO"), tr(L"ENTRADA"), tr(L"SALIDA"),
                                   tr(L"FRECUENCIA"), tr(L"BUFFER"), tr(L"IDIOMA") };
        const WCHAR* values[6] = { wvEng, wvIn, wvOut, wvRate, wvBuf,
                                   g_lang ? L"ENGLISH" : L"ESPAÑOL" };
        const G::REAL cyR = kSetPillH * 0.5f;    // centro vertical de una fila
        for (int i = 0; i < 6; ++i) {
            const G::REAL ry = py + kSetRowY0 + i*kSetRowPitch;
            g_panelRows[i] = { (int)x0, (int)ry, (int)x1, (int)(ry + kSetPillH) };
            const bool hovR = mouseIn(g_panelRows[i].left, g_panelRows[i].top,
                                      g_panelRows[i].right, g_panelRows[i].bottom);
            // UNA caja por fila: rotulo | pastilla (con las flechas DENTRO).
            // Antes habia rotulo + circulo + pastilla + circulo, cada uno a su x.
            {
                G::SolidBrush vb(hovR ? kFSurf3 : kFSurf2);
                G::Pen vp(hovR ? kAcc : kFLine, 1.0f);
                roundRect(g, pillL, ry, pillW, kSetPillH, 7.0f, &vp, &vb);
            }
            // Rotulo y valor comparten el MISMO centro vertical (antes el rotulo
            // iba pegado arriba y el valor centrado en la pastilla: media fila
            // de desfase).
            drawTextFit(g, labels[i], G::FontStyleBold, hovR ? kFTxt : kFTxt3,
                        x0, ry, kSetLabelW, kSetPillH, kTRotulo, 8.5f, kFontTec, false, true);
            drawTextFit(g, values[i], G::FontStyleRegular, kFTxt,
                        pillL + 44.0f, ry, pillW - 88.0f, kSetPillH,
                        kTValor, 9.5f, kFontMono, true, true);
            // Flechas: la zona de clic es el extremo de la pastilla (40px) y
            // solo se resalta la que esta bajo el raton.
            const bool hl = mouseIn((int)pillL, (int)ry, (int)(pillL + 40.0f), (int)(ry + kSetPillH));
            const bool hr = mouseIn((int)(pillR - 40.0f), (int)ry, (int)pillR, (int)(ry + kSetPillH));
            if (hl || hr) {
                G::SolidBrush cb(kFSurf3);
                roundRect(g, hl ? pillL + 3.0f : pillR - 31.0f, ry + 3.0f, 28.0f,
                          kSetPillH - 6.0f, 14.0f, nullptr, &cb);
            }
            icChev(g, pillL + 22.0f, ry + cyR, 12.0f, hl ? kFTxt : kFTxt2, 1);
            icChev(g, pillR - 22.0f, ry + cyR, 12.0f, hr ? kFTxt : kFTxt2, 2);
            if (hl) pressVeil(g, pillL + 3.0f, ry + 3.0f, 28.0f, kSetPillH - 6.0f, 14.0f);
            if (hr) pressVeil(g, pillR - 31.0f, ry + 3.0f, 28.0f, kSetPillH - 6.0f, 14.0f);
        }
        // Fila 7: RECARGAR DISPOSITIVOS. Una sola accion, asi que la fila entera
        // es el boton (sin flechas) y lleva la cuenta de lo que hay ahora mismo.
        {
            const G::REAL ry = py + kSetRowY0 + 6*kSetRowPitch;
            g_panelRows[6] = { (int)x0, (int)ry, (int)x1, (int)(ry + kSetPillH) };
            const bool hovR = mouseIn(g_panelRows[6].left, g_panelRows[6].top,
                                      g_panelRows[6].right, g_panelRows[6].bottom);
            {
                G::SolidBrush vb(hovR ? kFSurf3 : kFSurf2);
                G::Pen vp(hovR ? kAcc : kFLine, 1.0f);
                roundRect(g, pillL, ry, pillW, kSetPillH, 7.0f, &vp, &vb);
            }
            drawTextFit(g, L"DISPOSITIVOS", G::FontStyleBold, hovR ? kFTxt : kFTxt3,
                        x0, ry, kSetLabelW, kSetPillH, kTRotulo, 8.5f, kFontTec, false, true);
            // Icono + rotulo de la accion, centrados en la MISMA fila que el resto.
            icRefresh(g, pillL + 18.0f, ry + cyR, 6.5f, hovR ? kAcc : kFTxt2);
            drawTextFit(g, L"RECARGAR", G::FontStyleBold, hovR ? kFTxt : kFTxt2,
                        pillL + 32.0f, ry, 130.0f, kSetPillH, kTValor, 9.5f, kFontMono, false, true);
            {
                // A la derecha, lo que ha encontrado (o el aviso de la recarga).
                std::wstring sub;
                if (!g_devMsg.empty() && (GetTickCount() - g_devMsgAt) < 6000) {
                    sub = g_devMsg;
                } else {
                    sub = std::to_wstring(g_inputDevices.size()) + L" IN \xB7 " +
                          std::to_wstring(g_outputDevices.size()) + L" OUT";
                }
                const G::REAL rx0 = pillR - 14.0f - 420.0f;
                const std::wstring sfit = fitEllipsis(g, sub.c_str(), fSmall, 0.0f, 420.0f);
                G::SolidBrush sb((GetTickCount() - g_devMsgAt) < 6000 ? kAcc : kFTxt2);
                G::StringFormat sfr;
                sfr.SetAlignment(G::StringAlignmentFar);
                sfr.SetLineAlignment(G::StringAlignmentCenter);
                sfr.SetFormatFlags(G::StringFormatFlagsNoWrap);
                g.DrawString(sfit.c_str(), -1, &fSmall,
                             G::RectF(rx0, ry, 420.0f, kSetPillH), &sfr, &sb);
            }
        }
        {
            WCHAR wlat[160];
            // Con el motor en marcha, la cuenta usa el buffer y la frecuencia
            // REALES: en compartido el dispositivo impone su periodo, asi que el
            // numero del ajuste (1024) no es el que corre (1056).
            const int latBuf  = (g_params.audioActive.load() && g_sessBufFrames.load() > 0)
                                  ? g_sessBufFrames.load() : bsz;
            const int latRate = (g_params.audioActive.load() && g_sessRate.load() > 0)
                                  ? g_sessRate.load() : kRates[st.rateIdx];
            swprintf_s(wlat, tr(L"Latencia estimada (captura + salida): ~%.1f ms"),
                       latBuf*1000.0/latRate*2.0);
            G::SolidBrush lb(kFTxt2);
            g.DrawString(wlat, -1, &fSmall, G::RectF(x0, py + 332.0f, x1 - x0, 16.0f), nullptr, &lb);
        }
        // Salud del motor: si el anillo captura->reproduccion se queda corto se
        // ve aqui. Es el dato que separa "crujido del motor" de "crujido del
        // dispositivo": con 0 bloques cortos, lo que suena no viene de aqui.
        {
            const long long xrS = g_xrunUnderrun.load(std::memory_order_relaxed);
            const long long xrM = g_xrunShortMax.load(std::memory_order_relaxed);
            const long long xrL = g_xrunLate.load(std::memory_order_relaxed);
            WCHAR wh[240];
            swprintf_s(wh, tr(L"Anillo corto: %lld bloque(s), peor hueco %lld muestras | tardios: %lld | colchon: %d"),
                       xrS, xrM, xrL, g_bridgeCushion);
            G::SolidBrush hb(xrS > 0 ? kFTxt2 : kFTxt3);
            g.DrawString(wh, -1, &fSmall, G::RectF(x0, py + 350.0f, x1 - x0, 16.0f), nullptr, &hb);
        }
        // separador tenue sobre los botones
        {
            G::Pen sep(G::Color(40, 0x24, 0x24, 0x28), 1.0f);
            g.DrawLine(&sep, x0, py + ph - 84.0f, x1, py + ph - 84.0f);
        }
        {
            // Botones alineados al borde derecho del contenido (antes centrados
            // en la ventana, sin relacion con la rejilla de las filas).
            g_panelApply  = { (int)(x1 - 380.0f), (int)(py + ph - 64),
                              (int)(x1 - 200.0f), (int)(py + ph - 24) };
            g_panelCancel = { (int)(x1 - 180.0f), (int)(py + ph - 64),
                              (int)x1,            (int)(py + ph - 24) };
            bool ah = mouseIn(g_panelApply.left, g_panelApply.top, g_panelApply.right, g_panelApply.bottom);
            bool ch = mouseIn(g_panelCancel.left, g_panelCancel.top, g_panelCancel.right, g_panelCancel.bottom);
            {
                G::SolidBrush abr(ah ? kFTxt2 : kAcc);
                G::Pen appn(ah ? kFLine : kAccGlb, 1.0f);
                roundRect(g, (G::REAL)g_panelApply.left, (G::REAL)g_panelApply.top, 180, 40, 8, &appn, &abr);
                G::SolidBrush cbr(ch ? kFSurf3 : kFSurf2);
                G::Pen cbp(kFLine, 1.0f);
                roundRect(g, (G::REAL)g_panelCancel.left, (G::REAL)g_panelCancel.top, 180, 40, 8, &cbp, &cbr);
            }
            G::SolidBrush tb(G::Color(255, 255, 255, 255));
            g.DrawString(tr(L"APLICAR"), -1, &fPreset,
                         G::RectF((G::REAL)g_panelApply.left, (G::REAL)g_panelApply.top, 180, 40), &sfC, &tb);
            G::SolidBrush tb2(kFTxt);
            g.DrawString(tr(L"CANCELAR"), -1, &fPreset,
                         G::RectF((G::REAL)g_panelCancel.left, (G::REAL)g_panelCancel.top, 180, 40), &sfC, &tb2);
            pressVeil(g, (G::REAL)g_panelApply.left, (G::REAL)g_panelApply.top, 180, 40, 8.0f);
            pressVeil(g, (G::REAL)g_panelCancel.left, (G::REAL)g_panelCancel.top, 180, 40, 8.0f);
        }
    }

    profMark("Modal AFINADOR (cromatico): se pinta e");   //PFMARK
    // --- Modal AFINADOR (cromatico): se pinta el ultimo para ocultar todo lo demas ---
    if (g_tunerOpen.load()) {
        G::SolidBrush dim(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
        g.FillRectangle(&dim, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
        const G::REAL px = TUN_X, py = TUN_Y, pw = TUN_W, ph = TUN_H;
        glassPanel(g, px, py, pw, ph, kRModal, true);
        drawTxFit(g, L"AFINADOR", G::FontStyleBold, kAcc,
                      px + 34, py + 20, 2.0f, pw - 68, TxMarca);
        const int   note   = g_tunerNote.load();
        const float hz     = g_tunerHz.load();
        const float cen    = g_tunerCents.load();
        const bool  inTune = (note > 0) && (cen >= -5.0f) && (cen <= 5.0f);
        const G::Color nc  = inTune ? kAcc
                          : (note > 0 && cen >= -15.0f && cen <= 15.0f) ? G::Color(0xff, 0xb7, 0xb1, 0xa7)
                                                                       : kHot;
        if (note > 0) {
            static const WCHAR* kNoteNames[12] = {
                L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
            };
            WCHAR wnote[8];
            wcscpy_s(wnote, kNoteNames[note % 12]);
            WCHAR woct[8];
            swprintf_s(woct, L"%d", note/12 - 1);
            G::Font fNote(kFontMono, kTDisplay, G::FontStyleBold);
            G::SolidBrush nb(nc);
            g.DrawString(wnote, -1, &fNote, G::RectF(px, py + 62, pw - 48, 116), &sfC, &nb);
            G::Font fOct(kFontMono, 18.0f, G::FontStyleBold);
            G::SolidBrush ob(G::Color(0xff, 0x82, 0x7d, 0x76));
            g.DrawString(woct, -1, &fOct, G::RectF(px + pw - 96, py + 140, 60, 36), nullptr, &ob);
            WCHAR whz[32];
            swprintf_s(whz, L"%.1f Hz", (double)hz);
            G::Font fHz(kFontMono, kTValor, G::FontStyleRegular);
            g.DrawString(whz, -1, &fHz, G::RectF(px, py + 186, pw, 26), &sfC, &ob);
        } else {
            G::Font fIdle(kFontMono, kTDisplay2, G::FontStyleBold);
            G::SolidBrush ib(G::Color(255, 0x78, 0x78, 0x7e));
            g.DrawString(L"--", -1, &fIdle, G::RectF(px, py + 88, pw, 60), &sfC, &ib);
            G::Font fHint(kFontTec, kTRotulo, G::FontStyleRegular);
            g.DrawString(tr(L"TOCA UNA CUERDA PARA AFINAR"), -1, &fHint, G::RectF(px, py + 160, pw, 24),
                         &sfC, &ib);
        }
        // Barra de centavos: zona verde +-5, aguja
        {
            const G::REAL bx = px + 50, by = py + 250, bw = pw - 100, bh = 18;
            G::SolidBrush bgd(G::Color(0xff, 0x21, 0x1f, 0x21));
            roundRect(g, bx, by, bw, bh, 9, nullptr, &bgd);
            {
                G::SolidBrush gz(G::Color(0xff, 0x27, 0x25, 0x27));
                roundRect(g, bx + bw*0.45f, by, bw*0.10f, bh, 9, nullptr, &gz);
            }
            G::Font fCL(kFontUI, kTCuerpo, G::FontStyleRegular);
            G::SolidBrush cl(G::Color(0xff, 0x6e, 0x69, 0x63));
            g.DrawString(L"-50", -1, &fCL, G::RectF(bx - 38, by - 2, 34, bh + 4), nullptr, &cl);
            g.DrawString(L"+50", -1, &fCL, G::RectF(bx + bw + 4, by - 2, 36, bh + 4), nullptr, &cl);
            if (note > 0) {
                float c = cen;
                if (c < -50.0f) c = -50.0f;
                if (c >  50.0f) c =  50.0f;
                const G::REAL nx = bx + bw*0.5f + (bw*0.5f)*(c/50.0f);
                G::Pen np(nc, 3.0f);
                g.DrawLine(&np, nx, by - 7.0f, nx, by + bh + 7.0f);
                WCHAR wcen[40];
                swprintf_s(wcen, tr(L"%+.0f centavos"), (double)cen);
                G::Font fCen(kFontTec, kTRotulo, G::FontStyleBold);
                G::SolidBrush cb2(nc);
                g.DrawString(wcen, -1, &fCen, G::RectF(px, py + 292, pw, 22), &sfC, &cb2);
            }
        }
        // A4 de referencia + boton CERRAR
        {
            G::Font fA4(kFontUI, kTCuerpo, G::FontStyleRegular);
            G::SolidBrush ab(G::Color(0xff, 0x6e, 0x69, 0x63));
            g.DrawString(L"A4 = 440 Hz", -1, &fA4, G::RectF(px + 34, py + ph - 52, 160, 24), nullptr, &ab);
            const bool hov = mouseIn(TUN_CLOSE_X, TUN_CLOSE_Y, TUN_CLOSE_X + TUN_CLOSE_W, TUN_CLOSE_Y + 36);
            G::SolidBrush bb(hov ? G::Color(255, 0x18, 0x18, 0x1b) : G::Color(255, 0x10, 0x10, 0x12));
            G::Pen bp(G::Color(255, hov ? 0x4a : 0x30, hov ? 0x58 : 0x3b, hov ? 0x64 : 0x44), 1.5f);
            roundRect(g, (G::REAL)TUN_CLOSE_X, (G::REAL)TUN_CLOSE_Y, (G::REAL)TUN_CLOSE_W, 36.0f,
                      8, &bp, &bb);
            G::SolidBrush bt(G::Color(0xff, 0xb7, 0xb1, 0xa7));
            G::Font fBtn(kFontTec, kTRotulo, G::FontStyleBold);
            g.DrawString(tr(L"CERRAR"), -1, &fBtn, G::RectF((G::REAL)TUN_CLOSE_X, (G::REAL)TUN_CLOSE_Y,
                                                         (G::REAL)TUN_CLOSE_W, 36.0f), &sfC, &bt);
            pressVeil(g, (G::REAL)TUN_CLOSE_X, (G::REAL)TUN_CLOSE_Y, (G::REAL)TUN_CLOSE_W, 36.0f, 8.0f);
        }
    }

    // --- Modal de NOMBRE (RENOMBRAR un preset del usuario / GUARDAR COMO) ---
    if (g_renameOpen) {
        G::SolidBrush dim(G::Color((BYTE)(kVeil.GetA()*g_anim.modal), 0, 0, 0));
        g.FillRectangle(&dim, 0.0f, 0.0f, (G::REAL)WIN_W, (G::REAL)WIN_H);
        glassPanel(g, (G::REAL)REN_X, (G::REAL)REN_Y, (G::REAL)REN_W, (G::REAL)REN_H, kRModal, true);
        drawTxFit(g, g_renameSave ? L"GUARDAR COMO" : L"RENOMBRAR PRESET", G::FontStyleBold, kFTxt,
                      (G::REAL)(REN_X + 28), (G::REAL)(REN_Y + 24), 1.4f, 320.0f, TxMarca);
        {   // campo activo: el hueco del sistema, con filete y cursor del acento
            G::SolidBrush wb2(kWellA);
            G::Pen wp2(kAcc, 1.5f);
            roundRect(g, (G::REAL)REN_F_X, (G::REAL)REN_F_Y, (G::REAL)REN_F_W, (G::REAL)REN_F_H,
                      6.0f, &wp2, &wb2);
            G::Font fRen(kFontUI, kTNombre);
            G::SolidBrush tb2(kFTxt);
            const G::REAL tx = (G::REAL)(REN_F_X + 12), ty = (G::REAL)REN_F_Y;
            const G::REAL fw = (G::REAL)(REN_F_W - 24);
            const G::RectF mbox(0, 0, 900.0f, 60.0f);
            G::RectF mb2;
            auto nameW = [&](const WCHAR* t) {
                g.MeasureString(t, -1, &fRen, mbox, nullptr, &mb2);
                return mb2.Width;
            };
            // El campo muestra la COLA que cabe: un nombre admite 63 letras y el
            // hueco es mas corto, asi que el texto se cortaba y el cursor se iba
            // FUERA de la caja. Con el nombre corto no se mueve nada.
            std::wstring shown(g_renameBuf);
            if (nameW(shown.c_str()) > fw) {
                while (shown.size() > 1 && nameW(shown.c_str()) > fw) shown.erase(shown.begin());
            }
            g.DrawString(shown.c_str(), -1, &fRen, G::RectF(tx, ty, fw, (G::REAL)REN_F_H), nullptr, &tb2);
            G::Pen cur(kAcc, 1.2f);
            const G::REAL cxr = tx + nameW(shown.c_str()) + 2.0f;
            g.DrawLine(&cur, cxr, ty + 10.0f, cxr, ty + (G::REAL)REN_F_H - 10.0f);
        }
        {
            G::Font fB2(kFontTec, kTRotulo, G::FontStyleBold);
            const bool hOK = mouseIn(REN_OK_X, REN_OK_Y, REN_OK_X + REN_OK_W, REN_OK_Y + REN_OK_H);
            topBtn(g, (G::REAL)REN_OK_X, (G::REAL)REN_OK_Y, (G::REAL)REN_OK_W, (G::REAL)REN_OK_H,
                   g_renameSave ? L"GUARDAR" : L"APLICAR", fB2, hOK, true, kAcc);
            const bool hNO = mouseIn(REN_NO_X, REN_NO_Y, REN_NO_X + REN_NO_W, REN_NO_Y + REN_NO_H);
            topBtn(g, (G::REAL)REN_NO_X, (G::REAL)REN_NO_Y, (G::REAL)REN_NO_W, (G::REAL)REN_NO_H,
                   L"CANCELAR", fB2, hNO, false, kAcc);
        }
        pressVeil(g, (G::REAL)REN_OK_X, (G::REAL)REN_OK_Y, (G::REAL)REN_OK_W, (G::REAL)REN_OK_H, 6.0f);
    }

    // --- Navegador de presets (paso 18): la ventana superpuesta, encima de todo
    // menos el aviso efimero ---
    if (g_brwOpen) brwPaint(g);

    // --- Aviso efimero (toast): arriba y centrado, sobre todo lo demas ---
    if (toastVisible()) {
        G::Font fTo(kFontTec, kTRotulo, G::FontStyleBold);
        G::RectF tbx;
        g.MeasureString(g_toast, -1, &fTo, G::RectF(0, 0, 1200.0f, 60.0f), nullptr, &tbx);
        const G::REAL tw = tbx.Width + 44.0f, th = 36.0f;
        const G::REAL txx = ((G::REAL)WIN_W - tw)*0.5f, tyy = 84.0f;
        G::SolidBrush tbg(kFSurf2);
        G::Pen tpen(kAcc, 1.0f);
        roundRect(g, txx, tyy, tw, th, 8.0f, &tpen, &tbg);
        G::SolidBrush ttx(kFTxt);
        g.DrawString(tr(g_toast), -1, &fTo, G::RectF(txx, tyy, tw, th), &sfC, &ttx);
    }

    // Volcar al cliente: un solo BitBlt sobre el buffer cacheado
    QueryPerformanceCounter(&pfD1);
    g.Flush(G::FlushIntentionFlush);
    // volcar solo la region sucia: el backbuffer tiene el frame completo
    const RECT& rp = ps.rcPaint;
    BitBlt(hdc, rp.left, rp.top, rp.right - rp.left, rp.bottom - rp.top,
           g_bufDC, rp.left, rp.top, SRCCOPY);
    LARGE_INTEGER pf1; QueryPerformanceCounter(&pf1);
    {
        const bool profFull = (ps.rcPaint.left <= 0 && ps.rcPaint.top <= 0 &&
                               ps.rcPaint.right >= W && ps.rcPaint.bottom >= H);
        profPaint(pf1.QuadPart - pf0.QuadPart, pfS1.QuadPart - pfS0.QuadPart,
                  pfD1.QuadPart - pfS1.QuadPart, pf1.QuadPart - pfD1.QuadPart, profFull);
    }
    EndPaint(hwnd, &ps);
}

// Objetivo del arrastre de un pedal: fila + indice.
//  - Sobre una TARJETA (misma fila) => se INTERCAMBIAN las dos plazas. Es lo
//    que el usuario espera al "cambiar dos pedales": antes se insertaba antes
//    del destino y arrastrar el de la izquierda sobre el de la derecha no
//    movia NADA (el indice de insercion coincidia con el de origen).
//  - Entre tarjetas (o al final) => insercion en el hueco mas cercano.
static void updateDropTarget() {
    g_dropRow = -1; g_dropIdx = -1; g_dropSwap = -1;
    for (int row = 0; row < 2; ++row) {
        const int y0 = PED_ROW_Y[row] - 8, y1 = PED_ROW_Y[row] + PED_SLOT_H + 8;
        if (g_mouseY < y0 || g_mouseY >= y1) continue;
        const int cnt = (int)((row == 0) ? g_chainPre.size() : g_chainPost.size());
        for (int i = 0; i < cnt; ++i) {
            const int sx = (int)pedSlotX(i);
            if (g_mouseX >= sx && g_mouseX < sx + PED_SLOT_W) {
                g_dropRow = row; g_dropIdx = i;
                if (row == g_dragPedalRow && i != g_dragPedalIdx) g_dropSwap = i;
                return;
            }
        }
        // El indice sale del MISMO origen que las plazas (PED_X0). Con un -24
        // fijo el destino caia ~3 casillas a la derecha del raton.
        int idx = (int)((g_mouseX - PED_X0 + PED_PITCH/2) / PED_PITCH);
        if (idx < 0) idx = 0;
        if (idx > cnt) idx = cnt;
        g_dropRow = row; g_dropIdx = idx;
        return;
    }
}

// ---------------------------------------------------------------------------
//  Tabla unica de zonas: se recalcula UNA vez por frame, antes de la capa
//  dinamica. La pintura, hoverKey y el clic leen esto mismo, asi que no pueden
//  discrepir. Con un modal abierto la tabla se queda vacia a proposito: la
//  regla de un solo modal ya descarta todo lo que no sea del modal, y dejar
//  zonas vivas detras invitaba a pulsar controles que no se ven.
//  Los mandos (circulares, hitKnob) y el panel TONE3000 (t3kHit) tienen su
//  propio hit-test y lo usan todos sus sitios: no hace falta duplicarlos.
// ---------------------------------------------------------------------------
static void buildZones() {
    zoneBeginFrame();
    if (g_t3kOpen || g_tunerOpen.load() || g_settingsOpen || g_addOpen || g_brwOpen) return;

    // --- cabecera ---
    zoneSetBox(4,  TOP_AUDIO_X, 14, 100, 40);                              // AUDIO ON/OFF
    zoneSetBox(5,  TOP_GEAR_X,  14,  40, 40);                              // ajustes
    zoneSetBox(6,  TOP_BOX_X,  TOP_BOX_Y,  TOP_BOX_W,  TOP_BOX_H);         // preset superior
    zoneSetBox(79, TOP_BRW_X,   14, 100, 40);                              // NAVEGADOR de presets
    zoneSetBox(74, TOP_SAVE_X,  14, 100, 40);                              // GUARDAR
    zoneSetBox(75, TOP_REC_X,   14, 100, 40);                              // GRABAR

    // --- columna de cabina ---
    zoneSet   (91, TUNER_BTN_X, TUNER_BTN_Y, TUNER_BTN_X + TUNER_BTN_W, TUNER_BTN_Y + TUNER_BTN_H);
    zoneSetBox(76, CAB_BTN_X, CAB_BTN_Y, CAB_BTN_W, CAB_BTN_H);            // CARGAR IR
    zoneSetBox(92, CAB_SEL_X, CAB_SEL_Y, CAB_SEL_W, CAB_SEL_H);            // pill CAB
    zoneSetBox(77, CAB_MIC_X, CAB_MIC_Y, CAB_MIC_W, CAB_MIC_H);            // pill MIC
    zoneSetBox(93, CAB_DST_X, CAB_DST_Y, CAB_DST_W, CAB_DST_H);            // pill DIST
    zoneSetBox(101, MIX_SL_X - MIX_SL_PAD, MIX_BLK_Y,
                  MIX_SL_W + MIX_SL_PAD*2, MIX_BLK_B - MIX_BLK_Y);         // bloque MEZCLA

    // --- pie: preset y su navegacion ---
    zoneSetBox(70, 24,  BOT_BOX_Y,     306, 40);                           // preset inferior
    zoneSetBox(71, 334, BOT_BOX_Y + 2,  32, 36);                           // anterior
    zoneSetBox(72, 370, BOT_BOX_Y + 2,  32, 36);                           // siguiente
    zoneSetBox(73, 410, BOT_BOX_Y + 2,  34, 36);                           // favorito
}

// ---------------------------------------------------------------------------
//  Clave de hover: que control esta bajo el raton. El WM_MOUSEMOVE solo
//  repinta cuando esta clave cambia => sin tormentas de WM_PAINT al mover
//  el cursor por zonas que no cambian visualmente.
// ---------------------------------------------------------------------------
static int hoverKey() {
    POINT pt = { g_mouseX, g_mouseY };
    // El despliegue del buscador es un popup: su fila bajo el raton es la unica
    // clave de hover (600+fila) para que el realce se repinte al cambiar de fila.
    if (g_topOpen) {
        const RECT dr = topDropRect();
        if (PtInRect(&dr, pt)) {
            const int row = (pt.y - (dr.top + kTopPad)) / kTopRowH;
            if (row >= 0 && row < kTopMaxRows) return 600 + row;
        }
        return 599;
    }
    if (g_brwOpen) {
        // El navegador manda su propio hit-test: una clave por elemento bajo el
        // raton, para que el hover se repinte al cambiar de fila o de filtro.
        brwLayout();
        for (int i = 0; i < kFavColors; ++i) if (PtInRect(&g_brw.fav[i], pt)) return 400 + i;
        for (int i = 0; i < kBrwOrgCount; ++i) if (PtInRect(&g_brw.org[i], pt)) return 410 + i;
        for (int i = 0; i < kCharCount; ++i) if (PtInRect(&g_brw.chr[i], pt)) return 420 + i;
        for (int i = 0; i < kAmpCount; ++i) if (PtInRect(&g_brw.amp[i], pt)) return 440 + i;
        for (int i = 0; i < 5; ++i) if (PtInRect(&g_brw.ord[i], pt)) return 460 + i;
        if (PtInRect(&g_brw.close, pt)) return 470;
        if (PtInRect(&g_brw.search, pt)) return 471;
        if (PtInRect(&g_brw.list, pt)) return 480 + (g_mouseY - g_brw.list.top) / BRW_ROW_H;
        return 479;
    }
    if (g_t3kOpen) {                     // panel TONE3000: manda su propio hit-test
        const int k = t3kHit(g_mouseX, g_mouseY);
        return (k > 0) ? (220 + k) : 297;
    }
    if (g_tunerOpen.load()) {
        RECT cr = { TUN_CLOSE_X, TUN_CLOSE_Y, TUN_CLOSE_X + TUN_CLOSE_W, TUN_CLOSE_Y + 36 };
        if (PtInRect(&cr, pt)) return 78;
        return 3;   // modal afinador: captura el resto
    }
    if (g_settingsOpen) {
        if (PtInRect(&g_panelApply, pt)) return 1;
        if (PtInRect(&g_panelCancel, pt)) return 2;
        for (int i = 0; i < 6; ++i)
            if (PtInRect(&g_panelRows[i], pt)) return 10 + i;
        if (PtInRect(&g_panelRows[6], pt)) return 16;   // RECARGAR DISPOSITIVOS
        return 3;   // dentro del panel modal
    }
    if (g_addOpen) {
        for (int t = 0; t < 18; ++t) {
            const int col = t % 3, r = t / 3;
            RECT tr = { PAL_X + PAL_TX0 + col*PAL_TILE_DX, PAL_Y + PAL_TY0 + r*PAL_TILE_DY,
                        PAL_X + PAL_TX0 + col*PAL_TILE_DX + PAL_TILE_W,
                        PAL_Y + PAL_TY0 + r*PAL_TILE_DY + PAL_TILE_H };
            if (PtInRect(&tr, pt)) return 80 + t;
        }
        RECT cr = { PAL_X + PAL_W/2 - 90, PAL_Y + PAL_H - 58,
                    PAL_X + PAL_W/2 + 90, PAL_Y + PAL_H - 18 };
        if (PtInRect(&cr, pt)) return 98;
        return 3;   // dentro del panel modal de anadido
    }
    if (zoneHit(4)) return 4;   // boton AUDIO
    if (zoneHit(5)) return 5;    // gear
    if (zoneHit(6)) return 6;    // preset superior
    if (zoneHit(74)) return 74;   // GUARDAR
    if (zoneHit(75)) return 75;   // GRABAR WAV
    if (zoneHit(91)) return 91;   // AFINADOR
    if (zoneHit(76)) return 76;   // CARGAR IR
    // Los selectores de la cabina repintan al cruzar la MITAD: la flecha viva es
    // la que esta bajo el raton y el hoverKey tiene que distinguir las dos aunque
    // la zona sea una sola.
    if (zoneHit(92)) { const RECT r = zoneRect(92); return 92 + (g_mouseX >= (r.left + r.right)/2 ? 500 : 0); }
    if (zoneHit(77)) { const RECT r = zoneRect(77); return 77 + (g_mouseX >= (r.left + r.right)/2 ? 500 : 0); }
    if (zoneHit(93)) { const RECT r = zoneRect(93); return 93 + (g_mouseX >= (r.left + r.right)/2 ? 500 : 0); }
    if (zoneHit(101)) return 101; // slider MEZCLA
    for (int row = 0; row < 2; ++row) {
        const std::vector<int>& ch = (row == 0) ? g_chainPre : g_chainPost;
        for (size_t i = 0; i < ch.size(); ++i) {
            const int sx = (int)pedSlotX((int)i), sy = PED_ROW_Y[row];
            if (rectHas(pedSlotBox(sx, sy), g_mouseX, g_mouseY))
                return 30 + row*10 + (int)i;        // slot de pedal
        }
        const int cnt = (int)ch.size();
        if (cnt < PED_MAX_ROW) {
            const int sx = (int)pedSlotX(cnt), sy = PED_ROW_Y[row];
            if (rectHas(pedSlotBox(sx, sy), g_mouseX, g_mouseY))
                return 40 + row;                    // slot fantasma ANADIR
        }
    }
    for (size_t i = 0; i < g_knobs.size(); ++i) {
        int dx = g_mouseX - g_knobs[i].x, dy = g_mouseY - g_knobs[i].y;
        int rr = g_knobs[i].r + 10;
        if (dx*dx + dy*dy <= rr*rr) return 50 + (int)i;
    }
    if (zoneHit(70)) return 70;   // preset inferior
    if (zoneHit(71)) return 71;
    if (zoneHit(72)) return 72;
    if (zoneHit(73)) return 73;
    return 0;
}
// ---------------------------------------------------------------------------
//  Dirty-flag del timer: el timer es el unico que repinta "por inercia".
//  Si nada visible cambio desde el ultimo tick, el frame seria identico y se
//  salta el repintado (en reposo la CPU cae a ~0%).
// ---------------------------------------------------------------------------
struct VisSig {
    float mIn, mOut;                    // medidores en bruto
    float hIn, hOut, hGr;               // retencion de pico (snap a 0 bajo 0.02)
    float gate;                         // atenuacion del gate en dB
    float scope;                        // hash RMS del ultimo bloque del scope
    float tunerCen;                     // aguja del afinador
    bool clip, car, tuner, add, set, en, rec, act, focus;
    int  preset, tunerNote;
    DWORD fav;
    DWORD toast;                        // aviso efimero visible (0 = ninguno)
    bool ren; int delArm;               // modal RENOMBRAR / preset armado para borrar
    // Explorador TONE3000: entra en la firma para que "cargando" y los
    // mensajes de estado repinten sin depender del raton.
    bool t3k, t3kConn, t3kBusy, t3kFoc, t3kDev;
    int  t3kItems, t3kPage, t3kGear, t3kSel;
    unsigned t3kStat;
};
static VisSig g_visLast;
static bool visFloatEq(float a, float b, float e) { return a > b - e && a < b + e; }
// cuantizacion a la resolucion visual real: el ruido de fondo que no
// llega a mover ni un pixel no debe cambiar la firma (ni provocar repaint)
static float qStep(float v, float step) {
    if (v < 0.0f) v = 0.0f;
    return (float)((int)(v/step + 0.5f))*step;
}
static float qMeter(float v) { return (v < 0.025f) ? 0.0f : qStep(v, 1.0f/120.0f); }
static float qHold(float v)  { return (v < 0.02f)  ? 0.0f : qStep(v, 1.0f/120.0f); }
static float qGate(float v)  { return qStep(v, 0.6f); }
static bool visSigDirty() {
    VisSig s = VisSig();   // sin miembros sin inicializar: la firma es determinista
    s.mIn  = qMeter(g_params.meterIn.load());
    s.mOut = qMeter(g_params.meterOut.load());
    s.hIn  = qHold(g_pkHoldIn);
    s.hOut = qHold(g_pkHoldOut);
    s.hGr  = qHold(g_pkHoldGr);
    s.gate = qGate(g_grGateDb.load());
    s.scope = qStep(g_scopeHash.load() + 1.0f, 0.01f);
    s.tunerCen = g_tunerCents.load();
    s.clip  = g_clipFlag.load() > 0.0f;
    s.tuner = g_tunerOpen.load();
    s.add   = g_addOpen;
    s.set   = g_settingsOpen;
    s.en    = g_audioEnabled.load();
    s.rec   = g_recActive.load();
    s.act   = g_params.audioActive.load();
    s.focus = g_searchFocus;
    s.preset = g_presetIdx;
    s.tunerNote = g_tunerNote.load();
    s.fav   = g_favMask;
    s.toast = toastVisible() ? g_toastT0 : 0;
    s.ren   = g_renameOpen;
    s.delArm = g_delArm;
    s.t3k   = g_t3kOpen;
    s.t3kConn = g_t3kConnOpen;
    s.t3kBusy = g_t3kBusy;
    s.t3kFoc  = (g_t3kFocus != 0);
    s.t3kDev  = t3kDevWaiting();
    s.t3kItems = (int)g_t3kItems.size();
    s.t3kPage = g_t3kPage;
    s.t3kGear = g_t3kGear;
    s.t3kSel  = g_t3kSel;
    {
        std::lock_guard<std::mutex> lk(g_t3kMutex);
        unsigned h = 2166136261u ^ (unsigned)g_t3kStatus.size();
        for (WCHAR c : g_t3kStatus) h = (h * 16777619u) ^ (unsigned)c;
        s.t3kStat = h;
    }
    bool same = s.preset == g_visLast.preset && s.fav == g_visLast.fav &&
                s.toast == g_visLast.toast && s.ren == g_visLast.ren &&
                s.delArm == g_visLast.delArm &&
                s.tunerNote == g_visLast.tunerNote &&
                s.clip == g_visLast.clip && s.car == g_visLast.car &&
                s.tuner == g_visLast.tuner && s.add == g_visLast.add &&
                s.set == g_visLast.set && s.en == g_visLast.en &&
                s.rec == g_visLast.rec && s.act == g_visLast.act &&
                s.focus == g_visLast.focus &&
                s.t3k == g_visLast.t3k && s.t3kConn == g_visLast.t3kConn &&
                s.t3kBusy == g_visLast.t3kBusy && s.t3kFoc == g_visLast.t3kFoc &&
                s.t3kDev == g_visLast.t3kDev && s.t3kItems == g_visLast.t3kItems &&
                s.t3kPage == g_visLast.t3kPage && s.t3kGear == g_visLast.t3kGear &&
                s.t3kSel == g_visLast.t3kSel && s.t3kStat == g_visLast.t3kStat &&
                s.mIn == g_visLast.mIn && s.mOut == g_visLast.mOut &&
                s.hIn == g_visLast.hIn && s.hOut == g_visLast.hOut &&
                s.hGr == g_visLast.hGr && s.gate == g_visLast.gate &&
                s.scope == g_visLast.scope &&
                visFloatEq(s.tunerCen, g_visLast.tunerCen, 0.5f);
    g_visLast = s;
    return !same;
}

// ---------------------------------------------------------------------------
//  Window procedure: hit-testing de la interfaz estilo GUITAR ONE
// ---------------------------------------------------------------------------
// --- Pegar en una caja de texto propia -----------------------------------------
// Las tres cajas (clave/token, buscador del explorador, buscador de la lista y
// renombrar) son dibujo propio y se alimentan por WM_CHAR, asi que Ctrl+V NO les
// llegaba: se podia escribir a mano pero no pegar. Aqui se lee el portapapeles y se
// inserta al final, con dos cuidados:
//   - token=true: se quitan TODOS los espacios y saltos. Un token copiado de la
//     web trae salto de linea al final, y ese salto bastaba para que el servidor
//     lo rechazara.
//   - token=false: solo se quitan los saltos; en un nombre de preset o en una
//     busqueda los espacios son legitimos.
// Copia texto al portapapeles. Lo usa la hoja de conexion: ademas de intentar
// abrir el navegador, deja el codigo listo para pegar, que nunca falla.
static bool copyToClipboard(const WCHAR* txt)
{
    if (!txt || !txt[0]) return false;
    if (!OpenClipboard(g_hwnd)) return false;
    bool ok = false;
    EmptyClipboard();
    const size_t cb = (wcslen(txt) + 1) * sizeof(WCHAR);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cb)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, txt, cb);
            GlobalUnlock(h);
            ok = (SetClipboardData(CF_UNICODETEXT, h) != nullptr);
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}
static bool pasteIntoField(WCHAR* dst, size_t cap, bool token)
{
    if (!dst || cap < 2) return false;
    if (!OpenClipboard(g_hwnd)) return false;          // otro proceso lo tiene: no pasa nada
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        const WCHAR* src = (const WCHAR*)GlobalLock(h);
        if (src) {
            size_t n = wcslen(dst);
            for (const WCHAR* p = src; *p && n + 1 < cap; ++p) {
                if (*p == L'\r' || *p == L'\n' || *p == L'\t') continue;
                if (token && *p == L' ') continue;
                if (*p < 32) continue;
                dst[n++] = *p;
            }
            dst[n] = 0;
            ok = true;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return ok;
}
// Pegar en la caja que tenga el foco ahora mismo (0 = ninguna).
static bool pasteIntoFocusedField()
{
    if (g_renameOpen)                     return pasteIntoField(g_renameBuf, 64,  false);
    if (g_t3kOpen && g_t3kFocus == 2)     return pasteIntoField(g_t3kKey,     200, true);
    if (g_t3kOpen && g_t3kFocus == 1)     return pasteIntoField(g_t3kQuery,   64,  false);
    if (g_searchFocus)                    return pasteIntoField(g_ampFilter,  40,  false);
    return false;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // GA_PROF: clasificar el origen del repaint que este mensaje provoque
    if (profOn() && msg != WM_PAINT && msg != WM_ERASEBKGND && msg != WM_NCHITTEST &&
        msg != WM_SETCURSOR && msg != WM_GETMINMAXINFO && msg != WM_NULL)
        // un evento real manda sobre el timer mientras no se pinte
        g_pfSrc = (msg == WM_TIMER) ? (g_pfSrc ? g_pfSrc : 1) : 2;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paintWindow(hwnd);
        return 0;

    case WM_T3K_DONE:      // un hilo del explorador ha terminado su trabajo
        t3kApplyResult();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER: {
        // la animacion sigue viva tambien mientras se mueve la ventana:
        // congelarla era justo lo que se percibia como caida de fps
        // retencion de pico de los medidores (cae lento)
        float vi = g_params.meterIn.load();
        float vo = g_params.meterOut.load();
        g_pkHoldIn  = (vi > g_pkHoldIn)  ? vi : g_pkHoldIn*0.93f;
        g_pkHoldOut = (vo > g_pkHoldOut) ? vo : g_pkHoldOut*0.93f;
        // rampas: animacion y logica de repintado comparten este tick
        const int nvis = LIST_H / LIST_ROW_H;
        for (int vi = 0; vi < nvis; ++vi) {
            const int gi = g_listScroll + vi;
            const int rx = COL_L_X + 8, ry = LIST_Y + vi*LIST_ROW_H;
            const int rw = COL_L_W - 16, rh = LIST_ROW_H - 4;
            if (gi >= 0 && gi < kMaxNavRows)
                animStep(g_anim.row[gi], mouseIn(rx, ry, rx + rw, ry + rh) ? 1.0f : 0.0f, kRampHover);
        }
        const bool modalOpen = g_addOpen || g_settingsOpen || g_renameOpen ||
                               g_t3kOpen || g_t3kConnOpen;
        animStep(g_anim.modal, modalOpen ? 1.0f : 0.0f, kRampModal);
        if (g_params.audioActive.load())
            g_anim.pulse = (float)fmod((double)g_anim.pulse + 16.0*(6.283185307/1500.0), 6.283185307);
        // dirty-flag: si nada visible cambio, el frame seria identico y se
        // salta el repintado (en reposo la CPU cae a ~0%). animActive anade las
        // rampas que siguen en marcha: sin esto se repintarian los frames
        // finales al apagarse.
        if (visSigDirty() || animActive())
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_SETCURSOR: {
        // Cursor con significado: IDC_SIZENS sobre los mandos (se ajustan en
        // vertical), IDC_SIZEWE sobre el slider MEZCLA, flecha en el resto. Sin
        // esto el cursor es el mismo sobre un mando que sobre el fondo y la
        // interfaz no dice donde se puede tocar.
        if (LOWORD(lp) == HTCLIENT) {
            // Campos de texto: cursor de insercion, no de interfaz
            if (g_searchFocus || (g_t3kOpen && g_t3kFocus)) {
                SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                return TRUE;
            }
            const int hk = hoverKey();
            if (hk >= 50 && (size_t)hk < 50 + g_knobs.size()) {
                SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                return TRUE;
            }
            if (hk == 101) {   // slider MEZCLA
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;   // bordes y barra de titulo: cursor del sistema
    }

    case WM_LBUTTONDBLCLK: {
        // Doble clic sobre un mando: volver al valor con el que se cargo. Es el
        // gesto canonico en Guitar Rig y Amplitube, y aqui no existia: para
        // dejar un mando como estaba habia que recordar el numero.
        if (g_tunerOpen.load() || g_searchFocus) return 0;
        const int dx = uiX(GET_X_LPARAM(lp)), dy = uiY(GET_Y_LPARAM(lp));
        const int dbl = hitKnob(dx, dy);
        const float dv = (dbl >= 0) ? knobDefault(g_knobs[dbl].value) : -1e9f;
        if (dv > -1e8f) {
            g_knobs[dbl].value->store(dv);
            g_dragIdx = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        // Lo mismo para la MEZCLA: volver al 100% (todo cabina).
        if (zoneHitAt(101, dx, dy)) {
            g_params.mixCab.store(100.0f);
            g_mixDrag = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        g_lbtn = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        int x = uiX(GET_X_LPARAM(lp)), y = uiY(GET_Y_LPARAM(lp));
        POINT pt = { x, y };

        // --- Despliegue del buscador de la cabecera: popup, se come el clic ---
        if (g_topOpen) {
            const RECT dr = topDropRect();
            RECT box = { TOP_BOX_X, TOP_BOX_Y, TOP_BOX_X + TOP_BOX_W, TOP_BOX_Y + TOP_BOX_H };
            if (PtInRect(&dr, pt)) {
                const int row = (y - (dr.top + kTopPad)) / kTopRowH;
                const int li  = g_topScroll + row;
                if (row >= 0 && row < kTopMaxRows && li >= 0 && li < g_topCount)
                    topApply(g_topList[li]);
            } else if (PtInRect(&box, pt)) {
                g_topOpen = false;      // la misma caja lo cierra
            } else {
                g_topOpen = false;      // fuera: se cierra y no pulsa nada debajo
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // --- Modal NAVEGADOR de presets (paso 18): manda su propio hit-test ---
        if (g_brwOpen) {
            brwClick(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // --- Modal AFINADOR: se cierra con su boton CERRAR o pulsando FUERA del
        // cuadro, igual que los demas modales. Antes cualquier clic lo cerraba,
        // incluido dentro del propio panel, y su boton CERRAR no hacia nada
        // (el hit-test le daba identidad 78 para el hover, pero el clic lo
        // ignoraba): un boton decorativo y un modal que no se deja usar.
        if (g_tunerOpen.load()) {
            const bool inPanel = (x >= TUN_X && x <= TUN_X + TUN_W &&
                                  y >= TUN_Y && y <= TUN_Y + TUN_H);
            const bool onClose = (x >= TUN_CLOSE_X && x <= TUN_CLOSE_X + TUN_CLOSE_W &&
                                  y >= TUN_CLOSE_Y && y <= TUN_CLOSE_Y + 36);
            if (onClose || !inPanel) {
                g_tunerOpen.store(false);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // --- Modal de nombre: captura todos los clics ---
        if (g_renameOpen) {
            if (x >= REN_OK_X && x <= REN_OK_X + REN_OK_W &&
                y >= REN_OK_Y && y <= REN_OK_Y + REN_OK_H) {
                if (g_renameSave) savePresetAs(g_renameBuf);
                else              renameUserPreset(g_renameIdx, g_renameBuf);
                g_renameOpen = false; g_renameSave = false; g_renameIdx = -1; g_delArm = -1;
            } else if (x >= REN_NO_X && x <= REN_NO_X + REN_NO_W &&
                       y >= REN_NO_Y && y <= REN_NO_Y + REN_NO_H) {
                g_renameOpen = false; g_renameSave = false; g_renameIdx = -1;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // --- Modal TONE3000: captura los clics mientras esta abierto ---------
        if (g_t3kOpen) {
            const int hit = t3kHit(x, y);
            if (g_t3kConnOpen) {
                if (hit == T3K_HIT_KEYFIELD) {
                    g_t3kFocus = 2;
                } else if (hit == T3K_HIT_KEYGO) {
                    t3kDoConnect(w8u(std::wstring(g_t3kKey)));
                } else if (hit == T3K_HIT_KEYOPEN) {
                    // Ademas de abrir el navegador, el codigo queda COPIADO: si el
                    // sistema no puede abrir nada (ShellExecute devuelve false y
                    // antes se callaba), el usuario tiene igualmente el codigo
                    // listo para pegar. Nunca un boton que no dice nada.
                    std::string uri; std::string code;
                    {
                        std::lock_guard<std::mutex> lk(g_t3kMutex);
                        uri = g_t3kVerifyUri; code = g_t3kUserCode;
                    }
                    std::wstring wcode(code.begin(), code.end());
                    const bool copiado = copyToClipboard(wcode.c_str());
                    const bool abierto = !uri.empty() && t3k::openBrowser(uri);
                    if (abierto)
                        t3kStatusW(copiado ? L"PAGINA ABIERTA \xB7 CODIGO COPIADO: PEGALO SI TE LO PIDE"
                                           : L"PAGINA ABIERTA EN EL NAVEGADOR");
                    else
                        t3kStatusW(copiado ? L"NO PUDE ABRIR EL NAVEGADOR: CODIGO COPIADO, PEGALO EN TONE3000.COM/ACTIVATE"
                                           : L"NO PUDE ABRIR EL NAVEGADOR: VE A TONE3000.COM/ACTIVATE Y ESCRIBE EL CODIGO");
                } else if (hit == T3K_HIT_KEYCANCEL) {
                    g_t3kConnOpen = false; g_t3kFocus = 0;
                    { std::lock_guard<std::mutex> lk(g_t3kMutex); g_t3kUserCode.clear(); g_t3kVerifyUri.clear(); }
                    g_t3kExpMin = 0;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (hit == T3K_HIT_CLOSE) {
                g_t3kOpen = false; g_t3kFocus = 0; g_t3kConnOpen = false;
                t3kImgFree();
            } else if (hit == T3K_HIT_SEARCH) {
                g_t3kFocus = 1;
            } else if (hit == T3K_HIT_GO) {
                g_t3kFocus = 0;
                t3kDoSearch();
            } else if (hit == T3K_HIT_VER) {
                t3k::openBrowser("https://www.tone3000.com/tones");
            } else if (hit == T3K_HIT_LIB) {
                t3kShowLocalPresets();
            } else if (hit == T3K_HIT_CONN) {
                g_t3kConnOpen = true;
            } else if (hit == T3K_HIT_VERIF) {
                g_t3kVerif = !g_t3kVerif;
                t3kApplyGearFilter();
            } else if (hit >= T3K_HIT_CHIP0 && hit < T3K_HIT_CHIP0 + 8) {
                const int gsel = hit - T3K_HIT_CHIP0 - 1;
                g_t3kGear = (g_t3kGear == gsel) ? -1 : gsel;
                g_t3kPage = 1;
                if (g_t3kIsTrend) t3kApplyGearFilter();
                else if (g_t3kConnected) { g_t3kBusy = true; t3kStartSearch(); }
            } else if (hit >= T3K_HIT_CARD0 && hit < T3K_HIT_CARD0 + 6) {
                const int i = hit - T3K_HIT_CARD0;
                if (i >= 0 && i < (int)g_t3kItems.size()) {
                    g_t3kSel = i;
                    // Biblioteca LOCAL: no hay nada que descargar de la API. Se
                    // muestra lo que trae el preset (su cadena de capturas); la
                    // carga a las celdas es el paso siguiente.
                    const long long li = g_t3kItems[i].id;
                    if (g_t3kLocalMode && li >= 0 && li < (long long)g_t3kLocal.size()) {
                        // CARGAR el preset: se bajan sus capturas y se montan en las
                        // celdas, en orden. Pide sesion (las capturas se descargan de
                        // su servidor) y una celda libre.
                        if (!g_t3kConnected) {
                            g_t3kConnOpen = true;
                            t3kStatusW(L"PARA CARGAR UN PRESET HAY QUE CONECTAR: SUS CAPTURAS SE DESCARGAN");
                            return 0;
                        }
                        int libres = 0;
                        { std::lock_guard<std::mutex> lk(g_namMutex); libres = namBlkVis() - (int)g_namUi.size(); }
                        if (libres <= 0) {
                            toast(L"LAS CELDAS ESTAN LLENAS: QUITA UNA CAPTURA ANTES");
                            return 0;
                        }
                        t3kStatusW(L"BAJANDO LAS CAPTURAS DEL PRESET...");
                        std::thread(t3kWorkLoadPreset, (int)li, t3kTokenSnap()).detach();
                        return 0;
                    }
                    if (!g_t3kConnected) {
                        g_t3kConnOpen = true;
                        t3kStatusW(L"PARA BAJAR UN PERFIL HAY QUE CONECTAR CON TONE3000");
                    } else {
                        t3kStatusW(tr(L"BAJANDO LA CAPTURA DE ") + g_t3kItems[i].title + L"...");
                        std::thread(t3kWorkGrab, g_t3kItems[i].id, g_t3kItems[i].title, t3kTokenSnap()).detach();
                    }
                }
            } else if (hit >= T3K_HIT_LIST0 && hit < T3K_HIT_LIST0 + 4) {
                t3kStartList(hit - T3K_HIT_LIST0);
            } else if (!g_t3kIsTrend) {
                if (hit == T3K_HIT_PREV && g_t3kPage > 1) {
                    g_t3kPage--; g_t3kBusy = true; t3kStartSearch();
                } else if (hit == T3K_HIT_NEXT && g_t3kPage < t3kPageCount()) {
                    g_t3kPage++; g_t3kBusy = true; t3kStartSearch();
                } else if (hit >= T3K_HIT_PAGE0 && hit < T3K_HIT_PAGE0 + 6) {
                    const int pg = hit - T3K_HIT_PAGE0 + 1;
                    if (pg != g_t3kPage && pg <= t3kPageCount()) { g_t3kPage = pg; g_t3kBusy = true; t3kStartSearch(); }
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;   // modal: captura el resto
        }

        // Gear AJUSTES (siempre activo)
        RECT gear = zoneRect(5);      // ajustes
        if (PtInRect(&gear, pt)) {
            if (!g_settingsOpen) modalCloseAll();   // un modal a la vez
            g_settingsOpen = !g_settingsOpen;
            if (g_settingsOpen) {
                // Al abrir se reenumera en silencio: si acabas de enchufar la
                // interfaz aparece ya en la lista, sin tener que pulsar RECARGAR
                // (el boton sigue estando para cuando el panel ya esta abierto).
                RefreshDeviceLists(true);
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                g_setInIdx = g_selectedInputIdx;
                g_setOutIdx = g_selectedOutputIdx;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Panel de ajustes modal
        if (g_settingsOpen) {
            const G::REAL px = (WIN_W - kSetPanelW) / 2, py = (WIN_H - kSetPanelH) / 2 - 10;
            // Mismas zonas que pinta el panel (kSet*): la pastilla empieza en
            // pillL y acaba en x1, y cada flecha es su extremo de 40px.
            const G::REAL pillL = px + kSetPillL, pillR = px + kSetX1;
            if (PtInRect(&g_panelApply, pt)) {
                int oldRateIdx = 0;
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    oldRateIdx = g_appliedSet.rateIdx;
                    g_appliedSet = g_settings;
                }
                // Si cambia la FRECUENCIA, el IR de usuario se rehace a la nueva:
                // convolucionarlo con la tasa vieja desplazaba todo el gabinete
                // (48/44,1 = 8,8%, el corte de 5 kHz caia en 4,6 kHz).
                if (oldRateIdx != g_appliedSet.rateIdx && !g_cabIRPath.empty() &&
                    g_params.cabModel.load() == 6 &&
                    g_irBuiltRate.load() != kRates[g_appliedSet.rateIdx]) {
                    (void)loadIRFromWav(g_cabIRPath.c_str());
                    g_sceneDirty.store(true);
                }
                SelectInputDevice(g_setInIdx);
                SelectOutputDevice(g_setOutIdx);
                saveIni();   // persistir la configuracion de audio aplicada
                g_settingsOpen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&g_panelCancel, pt)) {
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    g_settings = g_appliedSet;
                }
                g_setInIdx = g_selectedInputIdx;
                g_setOutIdx = g_selectedOutputIdx;
                g_settingsOpen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            for (int i = 0; i < 6; ++i) {
                if (!PtInRect(&g_panelRows[i], pt)) continue;
                const G::REAL ry = py + kSetRowY0 + i*kSetRowPitch;
                RECT la = { (int)pillL, (int)ry, (int)(pillL + 40.0f), (int)(ry + kSetPillH) };
                RECT ra = { (int)(pillR - 40.0f), (int)ry, (int)pillR, (int)(ry + kSetPillH) };
                bool L = PtInRect(&la, pt), R = PtInRect(&ra, pt);
                // Toda la fila (y su pildora de valor) se pinta pulsable: el clic
                // no puede caer en hueco y el panel no se traga la mitad de un
                // control visible. Se avanza hacia el lado en el que se pulsa,
                // igual que las flechas de 36x30 (que eran lo unico que respondia).
                const int d = R ? 1 : (L ? -1 : (pt.x >= (int)((pillL + pillR)*0.5f) ? 1 : -1));
                {
                    switch (i) {
                    case 0: {
                        std::lock_guard<std::mutex> lock(g_settingsMutex);
                        g_settings.system = (g_settings.system + d + 2) % 2;
                        break;
                    }
                    case 1: g_setInIdx  = cycleIdx(g_setInIdx,  d, (int)g_inputDevices.size());  break;
                    case 2: g_setOutIdx = cycleIdx(g_setOutIdx, d, (int)g_outputDevices.size()); break;
                    case 3: {
                        std::lock_guard<std::mutex> lock(g_settingsMutex);
                        g_settings.rateIdx = cycleIdx(g_settings.rateIdx, d, kNumRates);
                        break;
                    }
                    case 4: {
                        std::lock_guard<std::mutex> lock(g_settingsMutex);
                        g_settings.bufIdx = cycleIdx(g_settings.bufIdx, d, kNumBufs);
                        break;
                    }
                    case 5:
                        // El idioma no es un ajuste de audio: se aplica y se guarda
                        // al instante, sin pasar por APLICAR/CANCELAR.
                        g_lang ^= 1;
                        g_scene.valid = false;
                        saveIni();
                        break;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;   // modal: ignora todo lo demas dentro del panel
            }
            // RECARGAR DISPOSITIVOS: fila entera = boton. Reenumera y conserva la
            // seleccion por ID; no cambia lo que suena hasta APLICAR.
            if (PtInRect(&g_panelRows[6], pt)) {
                RefreshDeviceLists();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;       // modal: clics fuera del panel se ignoran
        }

        // Boton AFINADOR (abre/cierra el modal)
        RECT tunBtn = zoneRect(91);   // AFINADOR
        if (PtInRect(&tunBtn, pt)) {
            if (!g_tunerOpen.load()) modalCloseAll();   // un modal a la vez
            g_tunerOpen.store(!g_tunerOpen.load());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // CARGAR IR del cab (dialogo WAV) + pills CAB / MIC / DIST
        RECT irBtn = zoneRect(76);    // CARGAR IR
        if (PtInRect(&irBtn, pt)) {
            wchar_t file[MAX_PATH] = L"";
            OPENFILENAMEW ofn = { sizeof(ofn) };
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = g_lang ? L"Impulse responses (WAV)\0*.wav\0All files\0*.*\0"
                                : L"Respuestas de impulso (WAV)\0*.wav\0Todos los archivos\0*.*\0";
            ofn.lpstrFile   = file;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrTitle  = tr(L"Cargar IR de cabina (WAV)");
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                if (!loadIRFromWav(file)) {
                    MessageBoxW(hwnd, tr(L"No se pudo cargar el WAV como respuesta de impulso.\n"
                                       L"Formatos admitidos: PCM 8/16/24/32 bits o float 32/64."),
                                L"GUITAR ONE", MB_OK | MB_ICONWARNING);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        RECT selBtn = zoneRect(92);   // selector CAB: izquierda = anterior, derecha = siguiente
        if (PtInRect(&selBtn, pt)) {
            const int nmax = g_userIRPtr ? 6 : 5;
            int cmv = g_params.cabModel.load();
            const bool back = pt.x < (selBtn.left + selBtn.right)/2;
            cmv = back ? (cmv <= 0 ? nmax : cmv - 1)
                       : (cmv >= nmax ? 0 : cmv + 1);
            g_params.cabModel.store(cmv);
            selectCab(cmv);
            g_sceneDirty.store(true);   // el nombre del cab cambia en la escena estatica
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        RECT micBtn = zoneRect(77);   // selector MIC: izquierda = anterior, derecha = siguiente
        if (PtInRect(&micBtn, pt)) {
            const int micv = g_params.cabMic.load();
            const bool back = pt.x < (micBtn.left + micBtn.right)/2;
            g_params.cabMic.store(back ? (micv <= 0 ? 2 : micv - 1)
                                       : (micv >= 2 ? 0 : micv + 1));
            selectCabEx(g_params.cabModel.load(), g_params.cabMic.load(), g_params.cabDist.load());
            g_sceneDirty.store(true);   // el tipo de micro se lee en el preview
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        RECT dstBtn = zoneRect(93);   // selector DIST: izquierda = anterior, derecha = siguiente
        if (PtInRect(&dstBtn, pt)) {
            const int dsv = g_params.cabDist.load();
            const bool back = pt.x < (dstBtn.left + dstBtn.right)/2;
            g_params.cabDist.store(back ? (dsv <= 0 ? 2 : dsv - 1)
                                        : (dsv >= 2 ? 0 : dsv + 1));
            selectCabEx(g_params.cabModel.load(), g_params.cabMic.load(), g_params.cabDist.load());
            g_sceneDirty.store(true);   // la distancia mueve el micro del preview
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Boton global AUDIO ON/OFF
        RECT audioBtn = zoneRect(4);  // AUDIO ON/OFF
        if (PtInRect(&audioBtn, pt)) {
            g_audioEnabled.store(!g_audioEnabled.load());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // NAVEGADOR de presets (paso 18): abre o cierra la ventana superpuesta
        RECT brwBtn = zoneRect(79);   // PRESETS
        if (PtInRect(&brwBtn, pt)) {
            if (g_brwOpen) { g_brwOpen = false; g_brwFocus = false; }
            else           { brwOpen(); }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // GUARDAR (pisa el preset del usuario) / GUARDAR COMO (crea uno nuevo con
        // nombre; ver el boton en la cabecera)
        RECT saveBtn = zoneRect(74);  // GUARDAR
        if (PtInRect(&saveBtn, pt)) {
            if (isUserPreset(g_presetIdx)) {
                saveUserPreset();
            } else {
                WCHAR pn[128];
                presetNameW(g_presetIdx, pn, 128);
                openNameModal(-1, true, pn);
                g_searchFocus = false;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // GRABAR / PARAR WAV de la salida
        RECT recBtn = zoneRect(75);   // GRABAR
        if (PtInRect(&recBtn, pt)) {
            if (g_recActive.load()) recStop(); else recStart();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Panel modal: ANADIR PEDAL
        if (g_addOpen) {
            for (int t = 0; t < 18; ++t) {
                const int col = t % 3, r = t / 3;
                RECT tr = { PAL_X + PAL_TX0 + col*PAL_TILE_DX, PAL_Y + PAL_TY0 + r*PAL_TILE_DY,
                            PAL_X + PAL_TX0 + col*PAL_TILE_DX + PAL_TILE_W,
                            PAL_Y + PAL_TY0 + r*PAL_TILE_DY + PAL_TILE_H };
                if (PtInRect(&tr, pt)) {
                    if (!chainHas(t)) {
                        std::vector<int>& v = (g_addRow == 0) ? g_chainPre : g_chainPost;
                        if ((int)v.size() < PED_MAX_ROW) {
                            // valores iniciales musicales para los pedales 6..17
                            if (t >= 6) {
                                static const float dfl[12][2] = { { 40, 60 }, { 30, 55 }, { 25, 45 }, { 50, 50 },
                                                                   { 45, 50 }, { 50, 40 }, { 55, 45 }, { 35, 40 },
                                                                   { 38, 35 }, { 45, 40 }, { 60, 50 }, { 45, 50 } };
                                g_params.fx[t][0].store(dfl[t - 6][0]);
                                g_params.fx[t][1].store(dfl[t - 6][1]);
                                g_params.fxOn[t].store(true);
                            }
                            v.push_back(t);
                        }
                        publishChain(g_chainPre, g_chainPost);
                        rebuildKnobs();
                        g_sceneDirty.store(true);   // las bahias se pintan en la escena
                    }
                    g_addOpen = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            RECT cr = { PAL_X + PAL_W/2 - 90, PAL_Y + PAL_H - 58,
                        PAL_X + PAL_W/2 + 90, PAL_Y + PAL_H - 18 };
            if (PtInRect(&cr, pt)) {
                g_addOpen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;   // modal: clics fuera del panel se ignoran
        }

        // --- Panel izquierdo: buscador, estrella del filtro, filas y favoritos ---
        {
            // caja de busqueda: toma el foco para escribir
            RECT sBox = { SEARCH_X, SEARCH_Y, SEARCH_X + SEARCH_W, SEARCH_Y + SEARCH_H };
            if (PtInRect(&sBox, pt)) {
                g_searchFocus = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // la estrella del filtro: "solo mis amplis favoritos". Vive en la
            // escena estatica (ambar o tenue), asi que hay que rehacerla.
            {
                RECT sr = searchStarRect();
                if (PtInRect(&sr, pt)) {
                    g_favOnly = !g_favOnly;
                    g_listScroll = 0;
                    g_searchFocus = false;
                    g_scene.valid = false;
                    g_sceneDirty.store(true);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            // filas de la lista: seleccionar ampli o marcar favorito
            RECT lBox = { COL_L_X + 8, LIST_Y, COL_L_X + 8 + (COL_L_W - 16), LIST_Y + LIST_H };
            if (PtInRect(&lBox, pt)) {
                g_searchFocus = false;
                int rows[kMaxNavRows];
                const int cnt = navRows(rows);
                const int vis = listVisibleRows();
                // La banda de pulsacion de una fila es su ranura entera (ver
                // navRowRect): esta division es su inversa exacta.
                const int vi = (y - LIST_Y) / LIST_ROW_H;
                const int gi = g_listScroll + vi;
                if (vi >= 0 && vi < vis && gi >= 0 && gi < cnt) {
                    const int a = rows[gi];
                    // estrella de favoritos (esquina derecha de la fila)
                    const RECT fr = navStarRect(vi);
                    if (PtInRect(&fr, pt)) {
                        g_favMask ^= (1u << a);         // una sola fuente de favoritos
                        g_sceneDirty.store(true);       // la estrella esta en la escena
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    if (g_params.model.load() != a) {
                        g_params.model.store(a);
                        g_scene.valid = false;   // miniaturas/hero/knobs dependen del modelo
                        rebuildKnobs();
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
        }

        // --- Cabezal PERFIL: fila de capturas (ANADIR, T3K, bloques) --------
        if (g_params.model.load() == 12) {
            const int ry = NAMROW_Y, rh = NAMROW_H;
            if (mouseIn(NAM_ADD_X, ry, NAM_ADD_X + NAM_ADD_W, ry + rh)) {          // + ANADIR (.nam)
                namPickAndAdd(hwnd);
                return 0;
            }
            // Boton impreso CARGAR PERFIL (dentro del frontal): antes su caja no
            // se miraba en el clic, asi que el rotulo prometia y no pasaba nada.
            {
                G::REAL bx2, by2, bw2, bh2;
                namBtnBox(bx2, by2, bw2, bh2);
                if (mouseIn((int)bx2, (int)by2, (int)(bx2 + bw2), (int)(by2 + bh2))) {
                    namPickAndAdd(hwnd);
                    return 0;
                }
            }
            if (mouseIn(NAM_T3K_X, ry, NAM_T3K_X + NAM_T3K_W, ry + rh)) {                   // T3K
                modalCloseAll();   // un modal a la vez
                g_t3kOpen = true;
                g_t3kFocus = 0;
                if (g_t3kItems.empty() && !g_t3kBusy) { g_t3kBusy = true; t3kStartTrend(); }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            NamChain snap;
            { std::lock_guard<std::mutex> lk(g_namMutex); snap = g_namUi; }
            const int vis = namBlkVis();
            for (int i = 0; i < vis; ++i) {
                const int bx = namBlkX(i);
                if (!mouseIn(bx, ry, bx + NAM_BLK_W, ry + rh)) continue;
                if (i >= (int)snap.size()) {
                    // CELDA VACIA: abre el explorador y lo que elijas caera AQUI.
                    g_namDropIdx = i;
                    modalCloseAll();   // un modal a la vez
                    g_t3kOpen = true;
                    g_t3kFocus = 0;
                    if (g_t3kItems.empty() && !g_t3kBusy) { g_t3kBusy = true; t3kStartTrend(); }
                    toast(L"LO QUE ELIJAS IRA A ESA CELDA");
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (mouseIn(bx + 4, ry + 4, bx + 30, ry + 30)) {            // LED: encender/apagar
                    snap[i]->on.store(!snap[i]->on.load());
                    saveIni();
                    g_sceneDirty.store(true);   // el LED vive en la escena cacheada
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                // Los tres botones, con las MISMAS cuentas que la pintura: 28x26,
                // paso 34, centrados en la celda de 104 y a 8 del canto inferior.
                const int q0x = bx + (NAM_BLK_W - (3*28 + 2*6))/2;
                const int qy0 = ry + rh - 26 - 8;
                for (int k = 0; k < 3; ++k) {
                    const int qx = q0x + k*34;
                    if (!mouseIn(qx, qy0, qx + 28, qy0 + 26)) continue;
                    if (k == 0)      { namChainMove(i, -1); g_namDelArm = -1; }
                    else if (k == 1) { namChainMove(i, +1); g_namDelArm = -1; }
                    else {
                        if (g_namDelArm == i) { namChainRemove(i); g_namDelArm = -1; }
                        else                  { g_namDelArm = i;
                                                toast(L"PULSA OTRA VEZ PARA QUITAR LA CAPTURA"); }
                    }
                    saveIni();
                    g_sceneDirty.store(true);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                // CUERPO de la celda: la primera pulsacion SELECCIONA (la pantalla
                // del frontal pasa a mostrar esa captura); si ya habia otra
                // seleccionada, las dos se INTERCAMBIAN de celda. Es el "poner y
                // cambiar los elementos" sin arrastrar.
                if (g_namSel >= 0 && g_namSel < (int)snap.size() && g_namSel != i) {
                    {
                        std::lock_guard<std::mutex> lk(g_namMutex);
                        if (i < (int)g_namUi.size() && g_namSel < (int)g_namUi.size())
                            std::swap(g_namUi[g_namSel], g_namUi[i]);
                    }
                    g_namSel = i;
                    g_namDelArm = -1;
                    namPublish();
                    saveIni();
                    toast(L"CAPTURAS INTERCAMBIADAS DE CELDA");
                } else {
                    g_namSel = i;
                }
                g_sceneDirty.store(true);   // la seleccion vive en la escena
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Slots del pedalboard: quitar / switch / knobs / arrastrar
        for (int row = 0; row < 2; ++row) {
            const std::vector<int>& ch = (row == 0) ? g_chainPre : g_chainPost;
            for (size_t i = 0; i < ch.size(); ++i) {
                const int sx = (int)pedSlotX((int)i), sy = PED_ROW_Y[row];
                if (!rectHas(pedSlotBox(sx, sy), x, y)) continue;
                const int t = ch[i];
                // Quitar pide DOS CLICS, como borrar un preset (g_delArm): el
                // primero arma y pone el aspa en rojo, el segundo borra. La diana
                // ademas se ensancha a +/-14 (el hueco limpio de la chapa llega a
                // 104, y el borde derecho del slot es 112): con +/-10 media 20px,
                // un tercio del minimo manejable, y borraba de un roce.
                const int pedKey = row*100 + (int)i;
                const bool pedFresh = (GetTickCount() - g_pedDelArmAt) < 4000;
                if (g_pedDelArm != pedKey || !pedFresh) g_pedDelArm = -1;
                RECT xr = { (LONG)(sx + PED_RM_DX - 14), (LONG)(sy + PED_RM_DY - 14),
                            (LONG)(sx + PED_RM_DX + 14), (LONG)(sy + PED_RM_DY + 14) };
                if (PtInRect(&xr, pt)) {
                    if (g_pedDelArm == pedKey) {
                        std::vector<int>& v = (row == 0) ? g_chainPre : g_chainPost;
                        v.erase(v.begin() + i);
                        publishChain(g_chainPre, g_chainPost);
                        rebuildKnobs();
                        g_sceneDirty.store(true);   // las bahias se pintan en la escena
                        g_pedDelArm = -1;
                    } else {
                        g_pedDelArm    = pedKey;
                        g_pedDelArmAt = GetTickCount();
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                // switch mini: la pista que pinta la plantilla (PED_TRK_*), con 4px
                // de margen. El rect viejo (esquina superior derecha) ni tocaba lo
                // que se ve: el clic del switch caia a 40px de la pista y, si se
                // acercaba al boton quitar, borraba el pedal en vez de conmutarlo.
                RECT sr = { sx + PED_TRK_X - 4, sy + PED_TRK_Y - 4,
                            sx + PED_TRK_X + PED_TRK_W + 4, sy + PED_TRK_Y + PED_TRK_H + 4 };
                if (PtInRect(&sr, pt)) {
                    g_params.fxOn[t].store(!g_params.fxOn[t].load());
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (hitKnob(x, y) >= 0) break;   // knob del slot: lo gestiona el bloque general
                // arrastrar el slot: reordenar dentro de la fila o cruzar el amp
                g_dragPedalRow = row;
                g_dragPedalIdx  = (int)i;
                g_dragPedalType = t;
                g_dropSwap = -1;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Un clic en cualquier otro punto de la chapa desarma el borrado:
            // es lo que hace el patron de los presets (g_delArm = -1 al final).
            if (g_pedDelArm != -1) { g_pedDelArm = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        }

        // Slots fantasma "+ ANADIR" (una por fila)
        for (int row = 0; row < 2; ++row) {
            const int cnt = (int)((row == 0) ? g_chainPre.size() : g_chainPost.size());
            if (cnt >= PED_MAX_ROW) continue;
            const int sx = (int)pedSlotX(cnt), sy = PED_ROW_Y[row];
            RECT ar = pedSlotBox(sx, sy);
            if (PtInRect(&ar, pt)) {
                modalCloseAll();   // un modal a la vez
                g_addOpen = true;
                g_addRow = row;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Caja de preset superior -> BUSCADOR: despliega la lista y se escribe.
        RECT pbox = zoneRect(6);      // preset superior
        if (PtInRect(&pbox, pt)) {
            g_topOpen = true;
            g_topQuery[0] = 0;
            g_topScroll = 0;
            topFilter();
            // La fila resaltada de salida: el preset que suena (si esta).
            g_topSel = -1;
            for (int i = 0; i < g_topCount; ++i)
                if (g_topList[i] == g_presetIdx) { g_topSel = i; break; }
            if (g_topSel >= kTopMaxRows) g_topScroll = g_topSel - kTopMaxRows + 1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Caja de preset del pie -> verlo en el NAVEGADOR, con el preset que suena
        // ya seleccionado (brwOpen lo deja a la vista). Se pinta con hover y con
        // velo de pulsacion, asi que TIENE que responder: antes era un control
        // pulsable que no hacia nada, y despues abria la pestana PRESETS del
        // sidebar, que ya no existe (paso 19).
        RECT fbox = zoneRect(70);     // preset del pie
        if (PtInRect(&fbox, pt)) {
            g_searchFocus = false;
            brwOpen();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Preset prev / siguiente / favorito
        RECT prevR  = zoneRect(71);   // preset anterior
        RECT nextR  = zoneRect(72);   // preset siguiente
        RECT heartR = zoneRect(73);   // favorito
        if (PtInRect(&prevR, pt)) {
            g_presetIdx = (g_presetIdx + presetTotal() - 1) % presetTotal();
            applyPreset(g_presetIdx);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&nextR, pt)) {
            g_presetIdx = (g_presetIdx + 1) % presetTotal();
            applyPreset(g_presetIdx);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&heartR, pt)) {
            // El corazon esta al lado de las flechas que mueven PRESETS, asi que
            // marca el preset que suena -la misma estrella que la fila de ese
            // preset en la lista-, no el ampli. Antes marcaba el ampli y la
            // estrella de la lista de presets no existia: dos listas y un solo
            // estado para las dos. Un clic pasa al color siguiente.
            presetFavSet(g_presetIdx, (presetFavColor(g_presetIdx) + 1) % kFavColors);
            saveIni();
            g_sceneDirty.store(true);   // la estrella de la fila vive en la escena
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Bloque MEZCLA: arrastre horizontal (coordenadas del clic, no el hover)
        if (zoneHitAt(101, x, y)) {   // bloque MEZCLA
            g_mixDrag = true;
            g_params.mixCab.store(mixSliderFrac(x)*100.0f);
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Knobs arrastrables
        g_dragIdx = hitKnob(x, y);
        if (g_dragIdx >= 0) {
            g_dragY = y;
            g_dragStart = g_knobs[g_dragIdx].value->load();
            g_dragFine = (GetKeyState(VK_SHIFT) < 0);   // se lee AL EMPEZAR, no en cada movimiento
            SetCapture(hwnd);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_lbtn && !(wp & MK_LBUTTON)) { g_lbtn = false; InvalidateRect(hwnd, nullptr, FALSE); }
        g_mouseX = uiX(GET_X_LPARAM(lp));
        g_mouseY = uiY(GET_Y_LPARAM(lp));
        if (!g_mouseTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_mouseTracking = true;
        }
        if (g_dragPedalRow >= 0) {
            // arrastre de pedal: actualizar fantasma + objetivo de insercion
            updateDropTarget();
            // regiones: banda del pedalboard + tarjeta fantasma (sigue al
            // mouse). El backbuffer se reconstruye completo en cada paint,
            // asi que volcar solo esta union es seguro: la posicion anterior
            // del fantasma queda cubierta por la acumulacion de regiones.
            RECT r = { 0, PED_ROW_Y[0] - 8, WIN_W, PED_ROW_Y[1] + PED_SLOT_H + 8 };
            const int fx = g_mouseX - PED_TMPL_W/2, fy = g_mouseY - PED_TMPL_H/2;
            if (fx - 4 < r.left)              r.left   = fx - 4;
            if (fy - 4 < r.top)               r.top    = fy - 4;
            if (fx + PED_TMPL_W + 4 > r.right)  r.right  = fx + PED_TMPL_W + 4;
            if (fy + PED_TMPL_H + 4 > r.bottom) r.bottom = fy + PED_TMPL_H + 4;
            uiInvalidate(hwnd, r);
            return 0;
        }
        if (g_mixDrag) {
            g_params.mixCab.store(mixSliderFrac(g_mouseX)*100.0f);
            // regiones: el bloque entero (valor, pista y extremos)
            RECT r = { MIX_SL_X - 4, MIX_BLK_Y - 4,
                       MIX_SL_X + MIX_SL_W + 4, MIX_BLK_B + 4 };
            uiInvalidate(hwnd, r);
            return 0;
        }
        if (g_dragIdx >= 0) {
            int y = uiY(GET_Y_LPARAM(lp));
            const KnobUI& k = g_knobs[g_dragIdx];
            float range = k.vmax - k.vmin;
            float v = g_dragStart + (float)(g_dragY - y)*0.45f*(g_dragFine ? 0.25f : 1.0f)*range/130.0f;
            if (v < k.vmin) v = k.vmin;
            if (v > k.vmax) v = k.vmax;
            k.value->store(v);
            // regiones: knob + tooltip de valor (cuelga hasta k.r+58 sobre el
            // centro; margen generoso para ticks y glow)
            RECT r = { k.x - k.r - 70, k.y - k.r - 70,
                       k.x + k.r + 70, k.y + k.r + 70 };
            uiInvalidate(hwnd, r);
            return 0;
        }
        // repintar solo cuando el hover cambia de control
        int hk = hoverKey();
        if (hk != g_lastHoverKey) {
            g_lastHoverKey = hk;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_mouseX = g_mouseY = -1;
        g_mouseTracking = false;
        g_lastHoverKey = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    // Cualquier arrastre (pedal, perilla, mezcla) se cancela al perder la captura
    // (otra ventana roba el foco, un dialogo del sistema) o con WM_CANCELMODE.
    // Sin este handler los flags quedaban vivos: el fantasma del pedal -la
    // tarjeta entera con su NOISE GATE y sus perillas- seguia pintandose para
    // siempre donde estuviera el cursor, sobre el slot fantasma "+ ANADIR".
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE: {
        const bool stuck = (g_dragPedalRow >= 0 || g_dragPedalIdx >= 0 ||
                            g_dragPedalType >= 0 || g_dragIdx >= 0 || g_mixDrag);
        if (stuck) {
            g_dragPedalRow = g_dragPedalIdx = g_dragPedalType = -1;
            g_dropRow = g_dropIdx = g_dropSwap = -1;
            g_dragIdx = -1;
            g_mixDrag = false;
            g_lbtn = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_lbtn) { g_lbtn = false; InvalidateRect(hwnd, nullptr, FALSE); }
        if (g_dragPedalRow >= 0) {
            // ReleaseCapture() manda WM_CAPTURECHANGED, y ese handler LIMPIA
            // g_dropRow/g_dragPedal* (esta pensado para capturas robadas). Antes
            // se soltaba la captura primero y el propio ReleaseCapture anulaba
            // el destino: el pedal nunca se movia. Se copian los valores, se
            // limpia el estado y despues se suelta.
            const int dragRow = g_dragPedalRow, dragIdx = g_dragPedalIdx;
            const int dropRow = g_dropRow,    dropIdx = g_dropIdx, dropSwap = g_dropSwap;
            g_dragPedalRow = g_dragPedalIdx = g_dragPedalType = -1;
            g_dropRow = g_dropIdx = g_dropSwap = -1;
            ReleaseCapture();
            if (dropSwap >= 0 && dropRow == dragRow) {
                // Soltar sobre otra tarjeta de la MISMA fila = cambiar las dos.
                std::vector<int>& ch = (dragRow == 0) ? g_chainPre : g_chainPost;
                if (dragIdx >= 0 && dragIdx < (int)ch.size() &&
                    dropSwap >= 0 && dropSwap < (int)ch.size()) {
                    std::swap(ch[dragIdx], ch[dropSwap]);
                    publishChain(g_chainPre, g_chainPost);
                    rebuildKnobs();
                    saveIni();
                    g_sceneDirty.store(true);
                }
            } else if (dropRow >= 0) {
                std::vector<int>& src = (dragRow == 0) ? g_chainPre : g_chainPost;
                std::vector<int>& dst = (dropRow == 0) ? g_chainPre : g_chainPost;
                if (dragIdx >= 0 && dragIdx < (int)src.size()) {
                    const int type = src[dragIdx];
                    src.erase(src.begin() + dragIdx);
                    int ins = dropIdx;
                    if (dropRow == dragRow && dragIdx < ins) --ins;
                    if (ins < 0) ins = 0;
                    if (ins > (int)dst.size()) ins = (int)dst.size();
                    dst.insert(dst.begin() + ins, type);
                    publishChain(g_chainPre, g_chainPost);
                    rebuildKnobs();
                    saveIni();                  // el orden nuevo sobrevive al cierre
                    g_sceneDirty.store(true);   // las bahias se pintan en la escena
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_mixDrag) {
            g_mixDrag = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_dragIdx >= 0) {
            g_dragIdx = -1;
            ReleaseCapture();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        if (g_topOpen) {   // rueda sobre el despliegue: mueve la lista
            const int wd = GET_WHEEL_DELTA_WPARAM(wp) / 120;
            if (wd != 0) {
                const int maxS = (g_topCount > kTopMaxRows) ? g_topCount - kTopMaxRows : 0;
                g_topScroll -= wd;
                if (g_topScroll < 0) g_topScroll = 0;
                if (g_topScroll > maxS) g_topScroll = maxS;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (g_brwOpen) {   // el navegador consume la rueda (su lista se desplaza)
            const int wd = GET_WHEEL_DELTA_WPARAM(wp) / 120;
            if (wd != 0) brwWheel(wd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_settingsOpen) return 0;
        if (g_t3kOpen) {   // rueda sobre la rejilla: pasar de pagina
            const int wd = GET_WHEEL_DELTA_WPARAM(wp) / 120;
            if (wd != 0 && !g_t3kIsTrend && g_t3kConnected) {
                const int pg = g_t3kPage + (wd > 0 ? -1 : 1);
                if (pg >= 1 && pg <= t3kPageCount() && pg != g_t3kPage) {
                    g_t3kPage = pg; g_t3kBusy = true; t3kStartSearch();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        POINT pt = { (short)GET_X_LPARAM(lp), (short)GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        pt.x = uiX(pt.x); pt.y = uiY(pt.y);
        // Rueda sobre la lista izquierda: desplazar el listado de amps/presets
        if (pt.x >= COL_L_X && pt.x <= COL_L_X + COL_L_W &&
            pt.y >= LIST_Y && pt.y <= LIST_Y + LIST_H) {
            const int wd = GET_WHEEL_DELTA_WPARAM(wp) / 120;
            if (wd != 0) {
                g_listScroll -= wd;
                const int maxS = listScrollMax();   // en RANURAS (los encabezados cuentan)
                if (g_listScroll > maxS) g_listScroll = maxS;
                if (g_listScroll < 0)    g_listScroll = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        int idx = hitKnob(pt.x, pt.y);
        if (idx >= 0) {
            int d = GET_WHEEL_DELTA_WPARAM(wp) / 120;
            const KnobUI& k = g_knobs[idx];
            float range = k.vmax - k.vmin;
            // SHIFT = ajuste fino (un cuarto de punto por muesca): la rueda sin
            // modificador salta de 100 en 100 mandos a la vez.
            const float fine = (wp & MK_SHIFT) ? 0.25f : 1.0f;
            float v = k.value->load() + (float)d*range*0.01f*fine;
            if (v < k.vmin) v = k.vmin;
            if (v > k.vmax) v = k.vmax;
            k.value->store(v);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Rueda sobre el bloque MEZCLA: el mismo criterio que los mandos
        if (pt.x >= MIX_SL_X - MIX_SL_PAD && pt.x <= MIX_SL_X + MIX_SL_W + MIX_SL_PAD &&
            pt.y >= MIX_BLK_Y && pt.y <= MIX_BLK_B) {
            const int wd = GET_WHEEL_DELTA_WPARAM(wp) / 120;
            if (wd != 0) {
                const float fine = (wp & MK_SHIFT) ? 0.1f : 1.0f;
                float v = g_params.mixCab.load() + (float)wd*fine;
                if (v < 0.0f) v = 0.0f;
                if (v > 100.0f) v = 100.0f;
                g_params.mixCab.store(v);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        return 0;
    }

    case WM_KEYDOWN: {
        // BUSCADOR de la cabecera abierto: manda el (flechas, Enter, ESC) y el
        // resto de atajos no se tocan mientras se escribe.
        if (g_topOpen) {
            if (wp == VK_ESCAPE) {
                g_topOpen = false;
            } else if (wp == VK_UP || wp == VK_DOWN) {
                if (g_topCount > 0) {
                    int sel = (g_topSel < 0)
                                ? (wp == VK_DOWN ? 0 : g_topCount - 1)
                                : g_topSel + (wp == VK_DOWN ? 1 : -1);
                    if (sel < 0) sel = g_topCount - 1;
                    if (sel >= g_topCount) sel = 0;
                    g_topSel = sel;
                    if (sel < g_topScroll) g_topScroll = sel;
                    if (sel >= g_topScroll + kTopMaxRows) g_topScroll = sel - kTopMaxRows + 1;
                }
            } else if (wp == VK_RETURN) {
                if (g_topSel >= 0 && g_topSel < g_topCount) topApply(g_topList[g_topSel]);
                else if (g_topCount > 0)                    topApply(g_topList[0]);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Atajos: ESPACIO = silencio, flechas = cambiar preset, ESC = cerrar modal
        if (wp == VK_ESCAPE) {
            // ESC tambien suelta un arrastre en curso (pedal, perilla, mezcla):
            // sin esto el fantasma quedaba colgando del cursor hasta el proximo
            // clic (WM_CANCELMODE no llega con una tecla, solo con un modal).
            if (g_dragPedalRow >= 0 || g_dragPedalIdx >= 0 || g_dragPedalType >= 0 ||
                g_dragIdx >= 0 || g_mixDrag) {
                g_dragPedalRow = g_dragPedalIdx = g_dragPedalType = -1;
                g_dropRow = g_dropIdx = g_dropSwap = -1;
                g_dragIdx = -1;
                g_mixDrag = false;
                g_lbtn = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_t3kOpen) {
                if (g_t3kConnOpen) { g_t3kConnOpen = false; g_t3kFocus = 0; }
                else { g_t3kOpen = false; g_t3kFocus = 0; t3kImgFree(); }
            }
            else if (g_renameOpen) { g_renameOpen = false; g_renameSave = false; g_renameIdx = -1; }
            else if (g_brwEdit) { g_brwEdit = 0; g_brwEditBuf[0] = 0; }   // suelta el campo, no la ventana
            else if (g_brwOpen) { g_brwOpen = false; g_brwFocus = false; }
            else if (g_searchFocus) g_searchFocus = false;
            else if (g_tunerOpen.load()) g_tunerOpen.store(false);
            else if (g_addOpen) g_addOpen = false;
            else if (g_settingsOpen) g_settingsOpen = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // PEGAR: Ctrl+V (y el clasico Shift+Insert) en la caja que tenga el foco.
        // Va ANTES de los guardianes de modal porque las cajas de la clave y del
        // buscador viven dentro de cuadros: si no, el modal se comeria la tecla.
        if (((GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'V' || wp == 'v')) ||
            ((GetKeyState(VK_SHIFT)   & 0x8000) && wp == VK_INSERT)) {
            if (pasteIntoFocusedField()) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_brwOpen) {
            // El navegador es un modal: las flechas mueven el resaltado por la
            // lista filtrada y ENTER carga ese preset. El resto de teclas no
            // disparan los atajos globales; las letras las recoge WM_CHAR en el
            // buscador cuando tiene el foco.
            brwLayout();
            if (g_brwEdit != 0) {
                // Escribiendo un campo del Info pane: ENTER lo guarda y ESC lo
                // suelta (el ESC lo trata la rama de arriba). Nada mas viaja.
                if (wp == VK_RETURN) brwCommitEdit();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == VK_DOWN || wp == VK_UP) {
                int ord[kMaxListAll];
                const int n = browseOrder(ord);
                if (n > 0) {
                    int cur = 0;
                    for (int i = 0; i < n; ++i) if (ord[i] == g_brwSel) { cur = i; break; }
                    int nx = cur + ((wp == VK_DOWN) ? 1 : -1);
                    if (nx < 0) nx = n - 1;
                    if (nx >= n) nx = 0;
                    g_brwSel = ord[nx];
                    const int vis = brwVisRows();
                    if (nx < g_brwScroll) g_brwScroll = nx;
                    if (nx >= g_brwScroll + vis) g_brwScroll = nx - vis + 1;
                }
            } else if (wp == VK_RETURN) {
                if (g_brwSel >= 0) {
                    if (!isT3kPreset(g_brwSel)) g_presetIdx = g_brwSel;
                    presetApplyFromBrowser(g_brwSel);
                    g_sceneDirty.store(true);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_tunerOpen.load() || g_addOpen || g_settingsOpen || g_t3kOpen || g_renameOpen)
            return 0;                                                  // modal: sin mas atajos
        if (g_searchFocus) return 0;                                     // escribiendo: sin atajos
        if (g_dragIdx >= 0 || g_dragPedalIdx >= 0) return 0;             // arrastrando: no
        // Flechas ARRIBA/ABAJO: navegan la lista de amplificadores de la
        // izquierda. Es el gesto del navegador de Guitar Rig y Amplitube: cambiar
        // de ampli SIN soltar el instrumento, con el resaltado de fila como foco
        // visible (el mismo que marca el raton). Los presets se mueven con las
        // flechas IZQUIERDA/DERECHA.
        if (wp == VK_UP || wp == VK_DOWN) {
            int idx[kMaxListEntries], cnt = 0;
            listFilterIdx(idx, &cnt);
            if (cnt > 0) {
                const int dir = (wp == VK_DOWN) ? 1 : -1;
                int cur = 0;
                const int self = g_params.model.load();
                for (int i = 0; i < cnt; ++i) if (idx[i] == self) { cur = i; break; }
                int n = cur + dir;
                if (n < 0) n = cnt - 1;
                if (n >= cnt) n = 0;
                if (g_params.model.load() != idx[n]) {
                    g_params.model.store(idx[n]);
                    g_scene.valid = false;    // miniaturas/hero/knobs del modelo
                    rebuildKnobs();
                }
                listFollow(idx[n]);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (wp == VK_SPACE) {
            g_audioEnabled.store(!g_audioEnabled.load());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_LEFT || wp == VK_RIGHT) {
            const int total = presetTotal();
            if (total > 0) {
                int idx = g_presetIdx + (wp == VK_RIGHT ? 1 : -1);
                if (idx < 0) idx = total - 1;
                if (idx >= total) idx = 0;
                applyPreset(idx);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        // Buscador de la cabecera: se escribe en el y la lista se filtra.
        if (g_topOpen) {
            const size_t n = wcslen(g_topQuery);
            if (wp == L'\b') {
                if (n) g_topQuery[n - 1] = 0;
            } else if (wp == L'\r' || wp == L'\n') {
                return 0;                      // Enter lo resuelve WM_KEYDOWN
            } else if (wp >= 32 && wp != 127 && n < 39) {
                g_topQuery[n]     = (WCHAR)wp;
                g_topQuery[n + 1] = 0;
            } else {
                return 0;
            }
            topFilter();
            g_topSel = g_topCount ? 0 : -1;
            g_topScroll = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Escritura en el modal de nombre (tiene prioridad sobre todo lo demas)
        if (g_renameOpen) {
            const size_t n = wcslen(g_renameBuf);
            if (wp == L'\b') {
                if (n) g_renameBuf[n - 1] = 0;
            } else if (wp == L'\r' || wp == L'\n') {
                if (g_renameSave) savePresetAs(g_renameBuf);
                else              renameUserPreset(g_renameIdx, g_renameBuf);
                g_renameOpen = false; g_renameSave = false; g_renameIdx = -1; g_delArm = -1;
            } else if (wp >= 32 && wp != 127 && n < 60) {
                g_renameBuf[n]     = (WCHAR)wp;
                g_renameBuf[n + 1] = 0;
            } else {
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Escritura en el buscador del navegador de presets (paso 18). El
        // navegador es modal: si esta abierto y su buscador NO tiene el foco, la
        // tecla se queda en el (no cae al buscador de la lista izquierda).
        if (g_brwOpen) {
            if (g_brwEdit != 0) {          // un campo del Info pane (nombre/autor/comentario)
                const size_t ne = wcslen(g_brwEditBuf);
                if (wp == L'\b') {
                    if (ne) g_brwEditBuf[ne - 1] = 0;
                } else if (wp == L'\r' || wp == L'\n') {
                    brwCommitEdit();
                } else if (wp >= 32 && wp != 127 && ne < 60) {
                    g_brwEditBuf[ne]     = (WCHAR)wp;
                    g_brwEditBuf[ne + 1] = 0;
                } else {
                    return 0;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (!g_brwFocus) return 0;
            const size_t nb = wcslen(g_brwText);
            if (wp == L'\b') {
                if (nb) g_brwText[nb - 1] = 0;
            } else if (wp == L'\r' || wp == L'\n') {
                g_brwFocus = false;
            } else if (wp >= 32 && wp != 127 && nb < 39) {
                g_brwText[nb]     = (WCHAR)wp;
                g_brwText[nb + 1] = 0;
            } else {
                return 0;
            }
            g_brwScroll = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Escritura en el explorador TONE3000: buscador (foco 1) o clave (foco 2)
        if (g_t3kOpen && g_t3kFocus) {
            WCHAR* buf = (g_t3kFocus == 1) ? g_t3kQuery : g_t3kKey;
            const size_t cap = (g_t3kFocus == 1) ? 63u : 199u;
            size_t n = wcslen(buf);
            if (wp == L'\b') {
                if (n) buf[n - 1] = 0;
            } else if (wp == L'\r' || wp == L'\n') {
                if (g_t3kFocus == 1) { g_t3kFocus = 0; t3kDoSearch(); }
                else                 { g_t3kFocus = 0;
                                       t3kDoConnect(w8u(std::wstring(g_t3kKey))); }
            } else if (wp >= 32 && wp != 127 && n < cap) {
                buf[n] = (WCHAR)wp;
                buf[n + 1] = 0;
            } else {
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Escritura en la caja de busqueda de la lista izquierda
        if (!g_searchFocus) break;
        const size_t n = wcslen(g_ampFilter);
        if (wp == L'\b') {
            if (n) g_ampFilter[n - 1] = 0;
        } else if (wp == L'\r' || wp == L'\n') {
            g_searchFocus = false;
        } else if (wp >= 32 && wp != 127 && n < 39) {
            g_ampFilter[n]     = (WCHAR)wp;
            g_ampFilter[n + 1] = 0;
        } else {
            return 0;
        }
        g_listScroll = 0;
        g_scene.valid = false;   // el texto del buscador vive en la escena estatica
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO: {   // que ningun control se aplaste por debajo del minimo
        MINMAXINFO* mi = (MINMAXINFO*)lp;
        RECT fz2 = { 0, 0, 0, 0 };
        AdjustWindowRectEx(&fz2, (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE), FALSE, 0);
        mi->ptMinTrackSize.x = WIN_MIN_W + (fz2.right - fz2.left);
        mi->ptMinTrackSize.y = WIN_MIN_H + (fz2.bottom - fz2.top);
        return 0;
    }

    case WM_SIZE:   // la escala de la interfaz cambia con el cliente
        uiCompute((int)LOWORD(lp), (int)HIWORD(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DESTROY:
        recStop();          // cierra el WAV en curso con su cabecera parcheada
        saveIni();          // persiste audio + DSP + presets de usuario
        g_volWatchStop.store(true);
        g_audioQuit.store(true);
        g_deviceChanged.store(true);
        freeStaticScene();
        freeBackbuffer();
        freeAmpArt();
        freeSlotTemplates();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  WinMain (excluido en el build de medicion: /DGA_HARNESS usa main())
// ---------------------------------------------------------------------------
#ifndef GA_HARNESS
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();

    G::GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken = 0;
    G::GdiplusStartup(&gdiplusToken, &gsi, nullptr);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXA wc = { sizeof(wc) };
    // Icono de la casa (GuitarAmp.rc, id 1), no el generico de Windows: es lo
    // unico de la ventana que no se maqueta y lo primero que se ve en la barra
    // de tareas y en Alt+Tab. El mismo recurso pinta el .exe en el Explorador.
    HICON hAppIcon = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 0, 0,
                                       LR_DEFAULTSIZE | LR_SHARED);
    if (!hAppIcon) hAppIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "GuitarAmpNativeClass";
    wc.hIcon         = hAppIcon;
    wc.hIconSm       = hAppIcon;
    wc.style         = CS_DBLCLKS;   // el doble clic devuelve el mando a su valor
    RegisterClassExA(&wc);

    // Ventana REDIMENSIONABLE con un tamano que SIEMPRE cabe en pantalla.
    // El diseno es 1440x956: en un portatil 1366x768 una ventana fija de ese
    // tamano se salia por arriba y por abajo (centraba en y negativa) y el
    // bridge y la linea de estado quedaban fuera, sin scroll que los llegase.
    // El marco se calcula con AdjustWindowRectEx, que ya suma SM_CXPADDEDBORDER:
    // antes se ignoraba y el area cliente quedaba 1432x948 en vez de 1440x956.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                        WS_MAXIMIZEBOX | WS_THICKFRAME;
    HDC shdc = GetDC(nullptr);
    const float dpiS = (float)GetDeviceCaps(shdc, LOGPIXELSX)/96.0f;
    ReleaseDC(nullptr, shdc);
    RECT wa = { 0, 0, 0, 0 };
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    const int waW = wa.right - wa.left, waH = wa.bottom - wa.top;
    RECT fz = { 0, 0, 0, 0 };
    AdjustWindowRectEx(&fz, style, FALSE, 0);
    const int frameW = fz.right - fz.left, frameH = fz.bottom - fz.top;
    int wantW = waW - frameW, wantH = waH - frameH;          // lo que cabe de verdad
    const int prefW = (int)(WIN_W*dpiS + 0.5f), prefH = (int)(WIN_H*dpiS + 0.5f);
    if (wantW > prefW) wantW = prefW;                        // el diseno si cabe
    if (wantH > prefH) wantH = prefH;
    if (wantW < WIN_MIN_W && WIN_MIN_W < waW) wantW = WIN_MIN_W;
    if (wantH < WIN_MIN_H && WIN_MIN_H < waH) wantH = WIN_MIN_H;
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "GUITAR ONE",
                                style | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                wantW + frameW, wantH + frameH,
                                nullptr, nullptr, hInst, nullptr);
    g_hwnd = hwnd;      // avisos desde los hilos del explorador TONE3000
    // Centrar en el AREA DE TRABAJO de la pantalla primaria (no en toda la
    // pantalla: con SM_CXSCREEN la ventana acababa bajo la barra de tareas)
    SetWindowPos(hwnd, nullptr, wa.left + (waW - (wantW + frameW))/2,
                 wa.top + (waH - (wantH + frameH))/2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    // Barra de titulo oscura acorde a la interfaz (Windows 10/11)
    {
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            (void)DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
    // El icono tambien en la ventana: el shell cachea el de clase por nombre de
    // clase, y sin esto la barra de tareas se queda con el generico.
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);

    // Idioma de la primera vez: el del sistema. Un Windows en espanol abre en
    // espanol; cualquier otro, en ingles. El .ini lo puede cambiar despues.
    g_lang = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH) ? 0 : 1;
    EnumerateDevices();
    initDspDefaults();    // bancos de 12 amps + fx + cadena por defecto
    g_params.meterIn.store(0.0f);
    g_params.meterOut.store(0.0f);
    g_params.audioActive.store(false);
    g_audioEnabled.store(false);   // arranca apagado: el usuario debe encenderlo
    applyPreset(0);       // publica su cadena y reconstruye los knobs
    loadIni();            // restaura audio, DSP, cadena y presets de usuario
    // Los presets de TONE3000 se leen AQUI, una vez (140 ms con 290 ficheros), y
    // luego quedan en cache: si se leyeran al abrir el navegador, el primer clic
    // en PRESETS se comeria ese medio segundo con la ventana ya pintada.
    t3kLoadLocalPresets();
    loadAmpArt();         // frontales del zip (si faltan: cara procedural)
    rebuildKnobs();       // la cadena cargada puede diferir de la del preset 0

    HANDLE hThread = CreateThread(nullptr, 0, AudioThread, nullptr, 0, nullptr);
    if (hThread) CloseHandle(hThread);

    // La vigilancia de volumen es un hilo propio (solo LEE el nivel del
    // endpoint, nunca lo escribe): vive lo mismo que el motor y anota en
    // GuitarAmp-audio.log quien mueve el nivel mientras suena.
    HANDLE hVolWatch = CreateThread(nullptr, 0, volWatchProc, nullptr, 0, nullptr);
    if (hVolWatch) CloseHandle(hVolWatch);

    SetTimer(hwnd, 1, 16, nullptr);   // ~60 fps: medidores y scope fluidos
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_volWatchStop.store(true);
    g_audioQuit.store(true);
    g_deviceChanged.store(true);
    CoUninitialize();
    G::GdiplusShutdown(gdiplusToken);
    return 0;
}
#endif   // !GA_HARNESS

#ifdef GA_HARNESS
// ============================================================================
//  Harness offline de medida del DSP (sin UI, sin audio):
//     cl /O2 /EHsc /std:c++17 /DGA_HARNESS GuitarAmpNative.cpp /Fe:_dsp_test.exe
//     _dsp_test.exe [resp|thd|alias|wav|ir|fx|perf|extra|selftest|nam2|all] -> _dsp_out/
//  Mide respuesta en frecuencia, THD, energia no armonica (aliasing) e imprime
//  WAVs con un DI sintetico para poder escuchar cada modelo.
//  extra      -> hueco impreso de cada ampli y su destino; 0 huecos sin accion
//  selftest   -> autotest de las correcciones (sale != 0 si algo falla)
//  nam2 <f>   -> compara nam.h con el core OFICIAL (_nmaref/ref.exe) muestra a muestra
// ============================================================================
#include <cstdio>
#include <cstring>

static const double kHRespF[19] = { 40,60,80,100,140,200,300,450,700,1000,1500,2200,3000,4000,5000,6300,8000,10000,12000 };

static void hWav16(const char* path, const std::vector<float>& x, int fs) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) { printf("[wav] no se pudo escribir %s\n", path); return; }
    const uint32_t n = (uint32_t)x.size(), db = n*2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + db); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32((uint32_t)fs);
    u32((uint32_t)fs*2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(db);
    for (uint32_t i = 0; i < n; ++i) {
        int s = (int)lrintf(x[i]*32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        int16_t v = (int16_t)s;
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

static double hRms(const std::vector<float>& x, size_t i0, size_t n) {
    double a = 0.0;
    for (size_t i = 0; i < n && i0 + i < x.size(); ++i) { double v = x[i0+i]; a += v*v; }
    return std::sqrt(a/(double)(n ? n : 1));
}

// Magnitud DFT en frecuencia arbitraria, ventana Hann, normalizada a amplitud
static double hDft(const std::vector<float>& x, size_t i0, size_t n, double f, double fs) {
    double re = 0.0, im = 0.0, ws = 0.0;
    if (n < 8) return 0.0;
    for (size_t i = 0; i < n && i0 + i < x.size(); ++i) {
        const double w  = 0.5 - 0.5*std::cos(2.0*M_PI*(double)i/(double)(n-1));
        const double ph = 2.0*M_PI*f*(double)i/fs;
        re += w*x[i0+i]*std::cos(ph);
        im -= w*x[i0+i]*std::sin(ph);
        ws += w;
    }
    return 2.0*std::sqrt(re*re + im*im)/(ws + 1e-12);
}

// Prepara la cadena con estado limpio y los knobs de fabrica del modelo
static void hPrepare(int fs, int model, int cab = -1, int mic = -1, int dist = -1) {
    initDspDefaults();
    for (int f = 0; f < 18; ++f) g_params.fxOn[f].store(false);
    g_params.master.store(55.0f);
    g_params.mixCab.store(100.0f);
    g_params.model.store(model);
    if (cab  >= 0) g_params.cabModel.store(cab);
    if (mic  >= 0) g_params.cabMic.store(mic);
    if (dist >= 0) g_params.cabDist.store(dist);
    g_amp.prepare((float)fs);
}

static std::vector<float> hRender(const std::vector<float>& in) {
    std::vector<float> out(in.size(), 0.0f);
    static const std::vector<int> kNoPre, kNoPost;
    for (size_t i = 0; i < in.size(); ++i) out[i] = g_amp.process(in[i], kNoPre, kNoPost);
    return out;
}

static std::vector<float> hTone(double f, double lvlDb, double sec, int fs) {
    std::vector<float> x((size_t)(fs*sec), 0.0f);
    const double A = std::pow(10.0, lvlDb/20.0);
    for (size_t k = 0; k < x.size(); ++k) x[k] = (float)(A*std::sin(2.0*M_PI*f*(double)k/fs));
    return x;
}

// Respuesta en frecuencia (dB) por tonos escalonados a nivel lineal
static void hRespLine(const char* tag, int fs) {
    printf("%-18s", tag);
    for (int i = 0; i < 19; ++i) {
        std::vector<float> in  = hTone(kHRespF[i], -26.0, 1.0, fs);
        std::vector<float> out = hRender(in);
        const double gi = hRms(in,  in.size()/2, in.size()/2 - 16);
        const double go = hRms(out, out.size()/2, out.size()/2 - 16);
        printf(" %6.1f", 20.0*std::log10((go + 1e-12)/(gi + 1e-12)));
    }
    printf("\n");
}

// THD+N (%) de 220 Hz frente al nivel de entrada
static void hThdLine(const char* tag, int fs, double f0 = 220.0) {
    printf("%-18s", tag);
    for (double lvl = -42.0; lvl <= -6.0; lvl += 6.0) {
        std::vector<float> in  = hTone(f0, lvl, 1.0, fs);
        std::vector<float> out = hRender(in);
        const size_t i0 = in.size()/2, n = in.size()/2 - 16;
        const double h1 = hDft(out, i0, n, f0, fs);
        double acc = 0.0;
        for (int h = 2; h <= 10; ++h) { const double m = hDft(out, i0, n, f0*h, fs); acc += m*m; }
        printf(" %7.2f%%", 100.0*std::sqrt(acc)/(h1 + 1e-12));
    }
    printf("\n");
}

// Energia NO armonica (aliasing + basura) con tono agudo y gain alto: %
static void hAliasLine(const char* tag, int fs) {
    printf("%-18s", tag);
    for (int g = 50; g <= 90; g += 20) {
        g_params.amp[g_params.model.load()][0].store((float)g);   // GAIN
        std::vector<float> in  = hTone(3300.0, -20.0, 1.0, fs);
        std::vector<float> out = hRender(in);
        const size_t i0 = in.size()/2, n = in.size()/2 - 16;
        const double tot = hRms(out, i0, n);
        double hAcc = 0.0;
        for (int h = 1; h*3300.0 < fs/2.0; ++h) { const double m = hDft(out, i0, n, 3300.0*h, fs); hAcc += m*m*0.5; }
        const double nonHarm = std::sqrt(std::max(0.0, tot*tot - hAcc));
        printf(" %6.2f%%", 100.0*nonHarm/(tot + 1e-12));
    }
    printf("\n");
}

// Pluck Karplus-Strong (DI sintetico) para escuchar los modelos
static void hPluck(std::vector<float>& out, double fs, double f, double t0, double dur, float amp, uint32_t& seed) {
    const size_t L = (size_t)(fs/f);
    if (L < 4) return;
    std::vector<float> b(L);
    for (size_t i = 0; i < L; ++i) {
        seed = seed*1664525u + 1013904223u;
        b[i] = (float)((seed >> 9) & 0xFFFF)/32768.0f - 1.0f;
    }
    const size_t i0 = (size_t)(t0*fs), n = (size_t)(dur*fs);
    size_t p = 0;
    float prev = 0.0f, lp = 0.0f;
    const float lpc = 1.0f - std::exp(-2.0f*kPi*4500.0f/(float)fs);
    for (size_t i = 0; i < n; ++i) {
        const float y = b[p];
        b[p] = 0.5f*(b[(p+1) % L] + prev)*0.9975f;
        prev = y; p = (p+1) % L;
        lp += lpc*(y - lp);
        const size_t k = i0 + i;
        if (k < out.size()) out[k] += amp*lp;
    }
}

static std::vector<float> hMakeDI(int fs, double dur) {
    std::vector<float> x((size_t)(fs*dur), 0.0f);
    uint32_t seed = 0xC0FFEEu;
    struct N { double f, t, d; };
    static const N riff[] = {
        { 82.41, 0.00, 1.15 }, { 123.47, 0.00, 1.15 }, { 164.81, 0.00, 1.15 },
        {  82.41, 0.30, 0.14 }, {  82.41, 0.48, 0.14 }, {  82.41, 0.66, 0.14 },
        { 110.00, 1.40, 1.15 }, { 164.81, 1.40, 1.15 }, { 220.00, 1.40, 1.15 },
        { 146.83, 2.80, 1.30 }, { 220.00, 2.80, 1.30 }, { 293.66, 2.80, 1.30 },
        {  98.00, 4.30, 1.80 }, { 146.83, 4.30, 1.80 }, { 196.00, 4.30, 1.80 },
        {  82.41, 6.30, 0.16 }, {  82.41, 6.50, 0.16 }, {  82.41, 6.70, 0.16 },
    };
    for (const N& n : riff) hPluck(x, fs, n.f, n.t, n.d, 0.30f, seed);
    float mx = 1e-6f;
    for (float v : x) mx = std::max(mx, std::fabs(v));
    for (float& v : x) v *= 0.28f/mx;
    return x;
}

// ============================================================================
//  Autotest de las correcciones (_dsp_test.exe selftest)
//  Cada prueba reproduce el defecto por su sintoma observable; se espera RED
//  antes del arreglo y verde despues. Sale con codigo != 0 si algo falla.
// ============================================================================
static int hFail = 0;
static void hCheck(bool ok, const char* what, const char* detail) {
    printf("  [%s] %-52s %s\n", ok ? "OK  " : "FALLA", what, detail ? detail : "");
    if (!ok) ++hFail;
}

// B1: el afinador. Rellena el anillo con una sinusoide YA decimada (como hace
// el hilo de audio) y analiza: la frecuencia informada debe ser la real.
static void hTunerFeed(double f, int fs) {
    tunerPrepare((float)fs);                    // la MISMA ruta que el hilo de audio
    const int dec = tunDec;
    for (int i = 0; i < TUN_N; ++i)
        tunBuf[i] = (float)(0.25*std::sin(2.0*M_PI*f*(double)i*(double)dec/(double)fs));
    tunFill = TUN_N;
    tunerAnalyze();
}
static void hTestTuner(int fs) {
    static const double kStr[6] = { 82.41, 110.00, 146.83, 196.00, 246.94, 329.63 };
    int bad = 0; char d[300] = ""; char one[48];
    for (int i = 0; i < 6; ++i) {
        hTunerFeed(kStr[i], fs);
        const double got = (double)g_tunerHz.load();
        const double c = (got > 1.0) ? 1200.0*std::log2(got/kStr[i]) : 1.0e9;
        if (std::fabs(c) > 25.0) ++bad;
        sprintf_s(one, "%.1f->%.1f(%+.0f)", kStr[i], got, c);
        if (i) strcat_s(d, " ");
        strcat_s(d, one);
    }
    hCheck(bad == 0, "B1 afinador informa la frecuencia real", d);
}

// B2: rechazo del filtro de diezmado del sobremuestreo. Un tono de 30 kHz en el
// dominio 4x pliega sobre 18 kHz al volver a la tasa base: su nivel medido ahi
// ES el rechazo del filtro. Es la propiedad que justifica el 4x.
static double hOs4RejectDb(int fs, double f) {
    Os4 os; os.prepare((float)fs); os.reset();
    const double fs4 = (double)fs*4.0;
    const int settle = 8192, N = fs/2;
    double acc = 0.0;
    for (int i = 0; i < settle + N; ++i) {
        float o[4];
        for (int k = 0; k < 4; ++k)
            o[k] = (float)std::sin(2.0*M_PI*f*((double)i*4.0 + (double)k)/fs4);
        const double y = os.down(o);
        if (i >= settle) acc += y*y;
    }
    const double rms = std::sqrt(acc/(double)N);
    return 20.0*std::log10(rms*std::sqrt(2.0) + 1e-15);
}

// A1: cargar un IR de usuario no puede encolar un IR de tabla estatica para
// que el hilo de UI le haga delete. De paso comprueba que un WAV PCM16
// estandar (el que exporta cualquier DAW) se carga de verdad.
static void hTestStaticIR(int fs) {
    genProcCabsV2((float)fs);
    selectCabEx(2, 0, 1);                       // IR procedural activo (motor por defecto)
    CabIR* active = g_cabIRPtr.load();
    std::vector<float> imp(512, 0.0f); imp[0] = 1.0f;
    hWav16("_dsp_out/_selftest_ir.wav", imp, fs);
    const bool loaded = loadIRFromWav(L"_dsp_out/_selftest_ir.wav");
    bool statik = false;
    for (CabIR* r : g_cabRetirados)
        if ((r >= g_procIR && r < g_procIR + 6) || (r >= g_procIR2 && r < g_procIR2 + 54)) statik = true;
    char d[220];
    sprintf_s(d, "WAV PCM16 cargado=%d, retirados=%d, estatico_encolado_para_delete=%d",
              loaded ? 1 : 0, (int)g_cabRetirados.size(), statik ? 1 : 0);
    hCheck(loaded, "A1a un WAV PCM16 estandar se carga como IR", d);
    hCheck(loaded && !statik, "A1b no se encola un IR estatico para delete", d);
    g_cabRetirados.clear();                     // el harness nunca ejecuta el delete
    g_cabIRPtr.store(active);
    g_userIRPtr = nullptr;
}

// Controles IMPRESOS de cada ampli: un hueco de la chapa o tiene accion o no
// debe estar. Imprime el destino de cada hueco y cuenta los que no hacen nada
// (0 = todos los amplis tienen sus controles con accion).
static void hExtraReport(int fs) {
    (void)fs;
    static const char* kFn[8] = { "SIN ACCION", "resonancia", "nivel salida",
                                  "tremolo prof.", "tremolo vel.", "chorus vel.",
                                  "chorus prof.", "compresion" };
    printf("== controles impresos: hueco -> destino ==\n");
    for (int m = 0; m < 13; ++m) {
        char lab[700] = "";
        int dead = 0;
        for (int i = 0; i < kAmpArt[m].nall && i < 10; ++i) {
            int kind = 0, mapped = -1;
            for (int k = 0; k < 6; ++k) if (kAmpArt[m].map[k] == i) { mapped = k; break; }
            char one[140];
            const WCHAR* lbl = kAmpHoleLabels[m][i];
            if (!lbl || !lbl[0]) lbl = L"\\?";
            if (mapped >= 0)      sprintf_s(one, " [%ls]=motor%d", lbl, mapped);
            else if ((kind = ampExtraKindForLabel(lbl)) != 0)
                                  sprintf_s(one, " [%ls]=%s", lbl, kFn[kind]);
            else                  { sprintf_s(one, " [%ls]=SIN ACCION", lbl); ++dead; }
            if (strlen(lab) + strlen(one) < 660) strcat_s(lab, one);
        }
        int hp[4]; const int nP = ampExtraParams(m, hp, 4);
        printf("  %-14s%s\n                 motor sin hueco: %d (mando pequeno)\n",
               kVoices[m].name, lab, nP);
        if (dead) { printf("                 HOYOS SIN ACCION: %d\n", dead); hFail += dead; }
    }
}

// C2: al cambiar de ampli no puede quedar sonando el pico de RESONANCE (o DEEP)
// del anterior. Se mide el grave del ampli nuevo tras cambiar desde uno con el
// control al tope, y se compara con el mismo ampli preparado de cero.
static double hLow100(int fs) {
    std::vector<float> in  = hTone(100.0, -20.0, 1.0, fs);
    std::vector<float> out = hRender(in);
    const size_t i0 = out.size()/2, n = out.size()/2 - 16;
    return 20.0*std::log10(hRms(out, i0, n) + 1e-12);
}
static void hTestExtraLeak(int fs) {
    hPrepare(fs, 0, kVoices[0].cab, kVoices[0].mic, kVoices[0].dist);
    const double ref = hLow100(fs);                       // SPARKLE TWIN de cero
    // Diagnostico: el control extra de HAVOC tiene que mover el grave de verdad
    int slot = -1;
    for (int i = 0; i < 2; ++i) if (ampExtraKindAt(11, i) == 1) slot = 6 + i;
    hPrepare(fs, 11, kVoices[11].cab, kVoices[11].mic, kVoices[11].dist);
    const double h50 = hLow100(fs);
    if (slot >= 0) g_params.amp[11][slot].store(95.0f);
    const double h95 = hLow100(fs);
    printf("    [diag] HAVOC RESONANCE slot=%d  a 100 Hz: 50->%.2f dB  95->%.2f dB\n",
           slot, h50, h95);
    g_params.model.store(0);                              // el usuario cambia de ampli
    const double got = hLow100(fs);
    char d[220];
    sprintf_s(d, "SPARKLE TWIN a 100 Hz: %.2f dB de cero vs %.2f dB tras cambiar (dif %.2f)",
              ref, got, got - ref);
    hCheck(std::fabs(got - ref) < 1.0, "C2 cambiar de ampli no arrastra el RESONANCE anterior", d);
}

// Verificacion de un .nam contra el core OFICIAL (el mismo metodo con el que se
// verifico A1). El harness escribe la entrada, llama al oraculo (nmakef/ref.exe,
// compilado del core de sdatkinson) y compara muestra a muestra.
//   _dsp_test.exe nam2 <modelo.nam>
static void hNamVerify(const char* modelPath, int fs) {
    const int N = 4096, skip = 512;          // se compara despues del precalentamiento
    std::vector<float> in((size_t)N, 0.0f);
    nam::Model m;
    if (!m.load(modelPath)) {
        char d[220];
        sprintf_s(d, "%s -> %s", modelPath, m.err.c_str());
        hCheck(false, "D el modelo se carga (nam.h)", d);
        return;
    }
    m.reset();
    for (int i = 0; i < m.prewarmSamples; ++i) m.tick(0.0f);
    uint32_t sd = 12345u;                    // entrada determinista: tono + ruido
    for (int i = 0; i < N; ++i) {
        sd = sd*1664525u + 1013904223u;
        const float r = (float)((sd >> 9) & 0xFFFF)/32768.0f - 1.0f;
        in[(size_t)i] = 0.25f*(float)std::sin(2.0*M_PI*220.0*(double)i/(double)fs) + 0.05f*r;
    }
    const int chn = m.outChannels;
    std::vector<std::vector<float>> mine((size_t)chn, std::vector<float>((size_t)N, 0.0f));
    for (int i = 0; i < N; ++i) {
        m.tick(in[(size_t)i]);
        const std::vector<float>& ob = m.outBuffer();
        for (int c = 0; c < chn && c < (int)ob.size(); ++c) mine[(size_t)c][(size_t)i] = ob[(size_t)c];
    }

    FILE* f = nullptr;
    if (fopen_s(&f, "_nmaref/in.f32", "wb") != 0 || !f) { printf("    (no se pudo escribir la entrada)\n"); return; }
    std::fwrite(in.data(), sizeof(float), (size_t)N, f);
    std::fclose(f);
    if (fopen_s(&f, "_nmaref/mine.f32", "wb") == 0 && f) {     // mismo plano por canal
        for (int c = 0; c < chn; ++c) std::fwrite(mine[(size_t)c].data(), sizeof(float), (size_t)N, f);
        std::fclose(f);
    }

    // El oraculo: mismo fichero de entrada, salida del core oficial.
    std::string cmd = std::string("_nmaref\\ref.exe \"") + modelPath + "\" _nmaref/in.f32 _nmaref/ref.f32";
    if (system(cmd.c_str()) != 0) { printf("    (el oraculo no pudo procesar el fichero)\n"); hFail++; return; }

    std::vector<float> ref((size_t)N*(size_t)chn, 0.0f);
    if (fopen_s(&f, "_nmaref/ref.f32", "rb") != 0 || !f) { printf("    (sin salida de referencia)\n"); hFail++; return; }
    const size_t got = std::fread(ref.data(), sizeof(float), (size_t)N*(size_t)chn, f);
    std::fclose(f);
    if (got != (size_t)N*(size_t)chn) { printf("    (referencia de tamanyo distinto)\n"); hFail++; return; }

    double mx = 0.0, pk = 1e-12, mxHead = 0.0;
    int    mxC = 0;
    for (int c = 0; c < chn; ++c)
        for (int i = 0; i < N; ++i) {
            const double df = std::fabs((double)mine[(size_t)c][(size_t)i] - (double)ref[(size_t)c*(size_t)N + (size_t)i]);
            if (i < skip) { if (df > mxHead) mxHead = df; }
            else if (df > mx) { mx = df; mxC = c; }
            const double a = std::fabs((double)ref[(size_t)c*(size_t)N + (size_t)i]);
            if (a > pk) pk = a;
        }
    char d[280];
    sprintf_s(d, "%s: %d canales (outBuf %d), dif maxima %.3e (canal %d) / pico %.3f = %.1e rel (arranque %.1e)",
              modelPath, chn, (int)m.outBuffer().size(), mx, mxC, pk, mx/pk, mxHead);
    hCheck(mx/pk < 1e-5, "D coincide con el core oficial muestra a muestra", d);
}

// G: pegar desde el portapapeles (Ctrl+V). Se prueba de verdad: se deja un texto
// en el portapapeles y se comprueba que llega al campo. Lo que se verifica de
// fondo es la limpieza: un token copiado de la web trae salto de linea al final y
// ese salto bastaba para que el servidor lo rechazara.
static void hTestPaste() {
    if (!OpenClipboard(nullptr)) { printf("    (portapapeles ocupado: no se puede probar)\n"); return; }
    EmptyClipboard();
    static const WCHAR* kText = L"  t3k_cs_ABC\n123 \tDEF \r\n";
    const size_t nb = (wcslen(kText) + 1)*sizeof(WCHAR);
    bool put = false;
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, nb)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, kText, nb);
            GlobalUnlock(h);
            put = (SetClipboardData(CF_UNICODETEXT, h) != nullptr);
        }
    }
    CloseClipboard();
    if (!put) { printf("    (no se pudo dejar texto en el portapapeles)\n"); return; }

    WCHAR tok[64] = L"prefijo_";
    WCHAR nom[64] = L"";
    const bool okT = pasteIntoField(tok, 64, true);
    const bool okN = pasteIntoField(nom, 64, false);
    char d[260];
    sprintf_s(d, "token pegando=\"%ls\" | nombre pegando=\"%ls\"", tok, nom);
    hCheck(okT && wcscmp(tok, L"prefijo_t3k_cs_ABC123DEF") == 0 && okN && nom[0] != 0,
           "G pegar del portapapeles (token sin espacios ni saltos)", d);
}

// E: un .nam manipulado tiene que RECHAZARSE, no ejecutarse. Una captura llega
// de fuera (el .nam que baja el usuario o TONE3000), asi que sus numeros no son
// de fiar: si una dimension o una dilatacion llegan en negativo o disparatadas,
// el motor reservaba de mas o el anillo leia fuera del vector --en el hilo de
// audio--. Se carga el modelo de referencia tal cual (linea base: tiene que
// seguir entrando, si no el resto de la prueba no probaria nada) y luego el
// mismo texto con un campo envenenado: tiene que salir con mensaje.
static bool hNamRead(const char* path, std::string& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return !out.empty();
}
static bool hNamSubst(const std::string& src, const char* from, const char* to, std::string& out) {
    const size_t p = src.find(from);
    if (p == std::string::npos) return false;
    out = src.substr(0, p) + to + src.substr(p + strlen(from));
    return true;
}
static void hNamRejects() {
    const char* ref = "_nmaref/m_wavenet.nam";
    std::string base;
    if (!hNamRead(ref, base)) {
        hCheck(false, "E0 se puede leer el modelo de referencia", ref);
        return;
    }
    char d[300];
    // Linea base: el fichero intacto tiene que cargar. Si este check falla, los
    // de abajo no harian falta: estarian rechazando por cualquier otra cosa.
    {
        nam::Model m;
        const bool ok = m.loadText(base);
        sprintf_s(d, "%s -> %s", ref, ok ? "carga" : m.err.c_str());
        hCheck(ok, "E0 el modelo de referencia carga (linea base)", d);
    }
    struct Case { const char* what; const char* from; const char* to; };
    static const Case cases[] = {
        { "E1 dilatacion negativa",  "\"dilations\": [1, 2]", "\"dilations\": [-1000]" },
        { "E2 dilatacion disparatada", "\"dilations\": [8]",   "\"dilations\": [99999999]" },
        { "E3 dimension negativa",   "\"channels\": 3",        "\"channels\": -1" },
        { "E4 dimension disparatada","\"channels\": 3",        "\"channels\": 99999999" },
    };
    for (const Case& c : cases) {
        std::string bad;
        if (!hNamSubst(base, c.from, c.to, bad)) {
            sprintf_s(d, "no se encontro \"%s\" en el modelo de referencia", c.from);
            hCheck(false, c.what, d);
            continue;
        }
        nam::Model m;
        const bool ok = m.loadText(bad);
        // Ademas de rechazar, tiene que decir POR QUE: el criterio del motor es
        // "nunca se adivina ni se deja sonar mal", y el mensaje va a pantalla.
        sprintf_s(d, "%s -> %s", ok ? "CARGADO (fallo: deberia rechazar)" : m.err.c_str(),
                  m.err.empty() && !ok ? "PERO SIN MENSAJE" : "");
        hCheck(!ok && !m.err.empty(), c.what, d);
    }
}

// P1: la recuperacion por NaN llama prepare(fs, false). genProcCabs y
// genProcCabsV2 ya se auto-protegen (si la frecuencia no cambio, no regeneran),
// asi que la bandera no ahorra trabajo HOY: deja explicito que esta llamada no
// puede reconstruir las IR, en vez de depender de un invariante que vive en otra
// funcion. Lo que se comprueba es que no la empeora: prepare(false) tiene que
// comportarse como prepare(true), dentro del ruido de prepare().
//
// Dato de paso: prepare() NO es idempotente (dos llamadas completas dan
// seÃ±ales distintas, dab de arriba). Viene de antes de este cambio y es la
// razon por la que dab no puede usarse como referencia. Lo que prepare() hace
// al RESETEAR g_cabConv.irCur es forzar ensureFor() a tirar el historial de la
// overlap-save y reasignar seis vectores: por eso llamarlo por muestra era lo
// que de verdad rompia el sonido, no el coste.
static void hPrepareRebuild(int fs) {
    hPrepare(fs, 5);                                   // estado limpio, knobs de fabrica
    std::vector<float> in = hTone(220.0, -18.0, 0.35, fs);
    std::vector<float> a  = hRender(in);               // con las IR regeneradas
    g_amp.prepare((float)fs);                          // prepare completo otra vez
    std::vector<float> b  = hRender(in);
    g_amp.prepare((float)fs, false);                   // lo que hace la recuperacion
    std::vector<float> c  = hRender(in);
    double dab = 0.0, dIn = 0.0, dOut = 0.0, pk = 0.0;
    const size_t q = a.size()/4;
    for (size_t i = 0; i < a.size(); ++i) {
        dab = std::max(dab, std::fabs((double)a[i] - (double)b[i]));
        const double d = std::fabs((double)b[i] - (double)c[i]);
        if      (i < q)        dIn  = std::max(dIn,  d);   // transitorio
        else if (i >= 3*q)      dOut = std::max(dOut, d);   // regimen permanente
        pk  = std::max(pk,  std::fabs((double)a[i]));
    }
    // Saltarse la reconstruccion no puede empeorar la recuperacion: tiene que
    // estar dentro del ruido que ya introduce prepare() de por si.
    char d[300];
    sprintf_s(d, "sin-rehacer vs completo = %.3e (arranque %.3e) | dos completos entre si = %.3e | pico %.3f",
              dOut, dIn, dab, pk);
    hCheck(dOut <= dab, "P1 prepare(false) no empeora la recuperacion: mismo motor", d);
}

// P2 y P3: lo que de verdad importa de prepare().
//
// P2 -- la recuperacion. Se ensucia el motor con un bloque de NaN, se llama a
// lo que llama el hilo de audio cuando lo detecta (prepare(fs,false)) y se
// comprueba que la senal vuelve a ser la de un motor limpio. Es la pregunta que
// justifies el cambio de por-muestra a por-bloque.
//
// P3 -- la idempotencia. Se mide desde un motor YA ASENTADO, que es lo unico que
// importa: si se mide desde el primer render, lo que sale es el transitorio que
// deja el estado anterior, no que prepare() deje algo sin limpiar.
static void hPrepareRecovery(int fs) {
    hPrepare(fs, 5);
    std::vector<float> in = hTone(220.0, -18.0, 0.35, fs);
    std::vector<float> ref = hRender(in);            // motor limpio

    std::vector<float> junk = in;
    for (size_t i = junk.size()/2; i < junk.size()/2 + 256; ++i)
        junk[i] = std::nanf("");                     // la senal se envenena
    bool sawNaN = false;
    for (float v : hRender(junk)) if (!std::isfinite(v)) { sawNaN = true; break; }

    g_amp.prepare((float)fs, false);                 // lo que hace el hilo de audio
    std::vector<float> rec = hRender(in);

    const size_t q = ref.size()/4;
    double d = 0.0, pk = 0.0;
    for (size_t i = 3*q; i < ref.size(); ++i) {      // solo regimen permanente
        d = std::max(d, std::fabs((double)rec[i] - (double)ref[i]));
        pk = std::max(pk, std::fabs((double)ref[i]));
    }
    char d1[260];
    sprintf_s(d1, "el NaN llego a salir del motor=%d | dif. permanente %.3e sobre pico %.3f (%.2f %%)",
              (int)sawNaN, d, pk, (pk > 0.0) ? 100.0*d/pk : 0.0);
    hCheck(sawNaN && d < 0.01*pk, "P2 tras un NaN, prepare(false) devuelve el motor al estado bueno", d1);
}

// R1: el anillo de grabacion se reserva con el dispositivo abierto y a partir de
// ahi solo se limpian los indices. ensure() NO puede encoger: si lo hiciera,
// reasignaria el vector y el hilo de audio --que esta empujando-- se quedaria
// escribiendo sobre el buffer anterior. clear() solo pone los indices a cero.
static void hRingContract() {
    RingBuffer b;
    b.ensure(1000);
    const bool grew = (b.data.size() == 1000);
    b.ensure(500);                                  // pedir menos NO encoge
    const bool noShrink = (b.data.size() == 1000);
    std::vector<float> v(1000, 1.0f);
    b.push(v.data(), v.size());
    b.clear();
    float out[1000];
    const size_t got = b.pop(out, 1000);            // tras limpiar, esta vacio
    b.push(v.data(), 800);
    const size_t got2 = b.pop(out, 1000);           // y sigue admitiendo empujar
    char d[220];
    sprintf_s(d, "reserva 1000=%d | ensure(500) no encoge=%d (sigue %d) | "
                 "pop tras clear=%d | push+pop=%d",
              (int)grew, (int)noShrink, (int)b.data.size(), (int)got, (int)got2);
    hCheck(grew && noShrink && got == 0 && got2 == 800,
           "R1 el anillo reserva capacidad y clear() no reasigna", d);
}

// I1..I5: el contrato del .ini (codificacion, rutas portables, claves muertas).
// Todo corre contra un temporal: g_iniPath se desvia y se restaura al final, de
// modo que el GuitarAmp.ini real del usuario no se abre ni se toca.
static void hIniContract() {
    // El temporal va dentro de _dsp_out/ (la carpeta que el harness ya crea y que
// esta en el .gitignore), no a %TEMP%: el binario puede correr con el acceso al
// disco restringido y ahi no hay forma de saber si el fallo es mio.
    const std::wstring tmpDir = exeDirW() + L"\\_dsp_out";
    CreateDirectoryW(tmpDir.c_str(), nullptr);
    const std::wstring tmp = tmpDir + L"\\ini_contract.txt";
    const std::wstring realPath = g_iniPath;
    const std::wstring realIR   = g_cabIRPath;
    DeleteFileW(tmp.c_str());

    const std::wstring kAcento = L"Micr\xf3" L"fono omnidireccional (GTRS)";
    const std::wstring kCJK    = L"\u65e5\u672c \u00e9\u00fc\u20ac";  // fuera de CP1252

    auto rd = [&tmp](const wchar_t* sec, const wchar_t* key, int n) -> std::wstring {
        wchar_t b[MAX_PATH]; b[0] = L'\0';
        GetPrivateProfileStringW(sec, key, L"", b, n, tmp.c_str());
        return std::wstring(b);
    };
    auto wr = [&tmp](const wchar_t* sec, const wchar_t* key, const wchar_t* val) {
        WritePrivateProfileStringW(sec, key, val, tmp.c_str());
    };
    auto bom = [&tmp]() {
        unsigned char b[2] = { 0, 0 };
        HANDLE r = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (r == INVALID_HANDLE_VALUE) return (int)0;
        DWORD got = 0;
        ReadFile(r, b, 2, &got, nullptr);
        CloseHandle(r);
        return (b[0] == 0xFF && b[1] == 0xFE) ? 1 : 0;
    };

    // I1: un .ini ANSI sin BOM se migra a UTF-16 sin perder el acento.
    {
        // El fichero viejo se construye desde kAcento pasando por CP_ACP, que
        // es lo que hizo la app al guardarlo: "o" sale un solo byte 0xF3. Asi
        // el esperado y lo escrito no pueden separarse nunca mas.
        char ansi[512];
        const int na = WideCharToMultiByte(CP_ACP, 0, kAcento.c_str(), -1, ansi, 512, nullptr, nullptr);
        std::string doc = "[audio]\r\ninName=";
        doc.append(ansi, (na > 0 ? na - 1 : 0));
        doc.append("\r\n");
        HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wrc = 0;
        WriteFile(f, doc.data(), (DWORD)doc.size(), &wrc, nullptr);
        CloseHandle(f);

        g_iniPath = tmp;
        iniEnsureUtf16();
        const int hasBom = bom();
        const std::wstring nm = rd(L"audio", L"inName", 256);
        char d[220];
        sprintf_s(d, "BOM=%d | acento leido intacto=%d (%d car.)", hasBom,
                  (int)(nm == kAcento), (int)nm.size());
        hCheck(hasBom == 1 && nm == kAcento,
               "I1 ANSI sin BOM migra a UTF-16 y conserva el acento", d);
    }

    // I2: lo que CP1252 no puede representar tiene que sobrevivir la escritura.
    //     Antes salia '?' y se perdia para siempre.
    {
        iniEnsureUtf16();
        wr(L"audio", L"outName", kCJK.c_str());
        const std::wstring nm = rd(L"audio", L"outName", 256);
        char d[220];
        sprintf_s(d, "valor=%d car. | leido=%d (%d car., el '?' de antes era 1 car. de mas)",
                  (int)kCJK.size(), (int)(nm == kCJK), (int)nm.size());
        hCheck(nm == kCJK, "I2 un caracter fuera de CP1252 no se pierde al guardar", d);
    }

    // I3: dentro de la carpeta del exe la ruta se guarda relativa; fuera, absoluta.
    {
        const std::wstring inside  = exeDirW() + L"\\perfiles\\x.nam";
        const std::wstring outside = L"C:\\Descargas\\y.nam";
        const std::wstring sIn  = iniPathStore(inside);
        const std::wstring sOut = iniPathStore(outside);
        const bool rel      = (sIn == L"perfiles\\x.nam");
        const bool absKept  = (sOut == outside);
        const bool rebuilt  = (iniPathLoad(sIn) == inside);
        const bool oldAbs   = (iniPathLoad(sOut) == outside);   // upgrade del .ini viejo
        char d[300];
        std::string rel8;
        for (wchar_t ch : sIn) rel8.push_back((ch < 128) ? (char)ch : '?');
        sprintf_s(d, "guardada como \"%s\" | fuera sigue absoluta=%d | "
                     "se reconstruye=%d | absoluta antigua intacta=%d", rel8.c_str(),
                  (int)absKept, (int)rebuilt, (int)oldAbs);
        hCheck(rel && absKept && rebuilt && oldAbs,
               "I3 las rutas de dentro son relativas y las de fuera absolutas", d);
    }

    // I4 + I5: vuelta completa por la API real. Se siembran tres bloques y se
    //      guarda con la cadena vacia: count baja a 0 y las claves muertas se
    //      borran (antes se acumulaban para siempre).
    {
        wr(L"chain", L"count", L"3");
        for (int i = 0; i < 3; ++i) {
            wchar_t k[16]; swprintf_s(k, L"p%d", i);
            wr(L"chain", k, (exeDirW() + L"\\perfiles\\z.nam").c_str());
            swprintf_s(k, L"on%d", i); wr(L"chain", k, L"1");
            swprintf_s(k, L"lv%d", i); wr(L"chain", k, L"1.00");
        }
        { NamChain vacia; { std::lock_guard<std::mutex> lk(g_namMutex); g_namUi = vacia; } }
        saveIni();

        const int cnt = iniGetInt(L"chain", L"count", -1);
        const std::wstring p0 = rd(L"chain", L"p0", 64);
        const std::wstring lv2 = rd(L"chain", L"lv2", 64);
        char d[260];
        sprintf_s(d, "count 3->%d | p0 borrada=%d | lv2 borrada=%d", cnt,
                  (int)p0.empty(), (int)lv2.empty());
        hCheck(cnt == 0 && p0.empty() && lv2.empty(),
               "I4 al encoger la cadena se borran las claves que sobran", d);

        // I5: una ruta relativa escrita por la API vuelve a su forma absoluta.
        wr(L"chain", L"count", L"1");
        wr(L"chain", L"p0", L"perfiles\\z.nam");
        const std::wstring back = iniPathLoad(rd(L"chain", L"p0", MAX_PATH));
        const bool okBack = (back == exeDirW() + L"\\perfiles\\z.nam");
        const int b2 = bom();
        sprintf_s(d, "vuelta a absoluta=%d | sigue en UTF-16=%d", (int)okBack, b2);
        hCheck(okBack && b2 == 1, "I5 la ruta relativa guardada se resuelve al arrancar", d);
    }

    // Un .ini inexistente tambien nace con BOM: si no, la primera clave que
    // escribiera la API de perfiles saldria en la pagina del sistema.
    {
        DeleteFileW(tmp.c_str());
        g_iniPath = tmp;
        iniEnsureUtf16();
        const int b0 = bom();
        wr(L"audio", L"inName", kAcento.c_str());
        const bool ida = (rd(L"audio", L"inName", 256) == kAcento);
        char d[200];
        sprintf_s(d, "recien creado con BOM=%d | primer guardado sin perdida=%d", b0, (int)ida);
        hCheck(b0 == 1 && ida, "I6 un .ini nuevo nace en UTF-16", d);
    }

    // I7: recargar la cadena del .ini SUSTITUYE la que hubiera en memoria, no se
    //     suma a ella. namChainClear() existia sin ningun llamador: la recarga se
    //     apilaba encima de lo ya cargado y la cadena podia duplicarse al releer.
    {
        wr(L"chain", L"count", L"0");
        {
            auto sl = std::make_shared<NamSlot>();
            sl->path = L"fantasma"; sl->name = L"fantasma";
            std::lock_guard<std::mutex> lk(g_namMutex);
            g_namUi.push_back(sl);
            g_namSel = 0;
        }
        namChainLoadFromIni();
        int n = -1, sel = 0;
        { std::lock_guard<std::mutex> lk(g_namMutex); n = (int)g_namUi.size(); sel = g_namSel; }
        char d[200];
        sprintf_s(d, "celdas tras recargar: %d (esperadas 0); seleccion %d (esperada -1)", n, sel);
        hCheck(n == 0 && sel == -1, "I7 recargar la cadena sustituye, no acumula", d);
    }

    g_iniPath = realPath;
    g_cabIRPath = realIR;
    DeleteFileW(tmp.c_str());
}

// J: el lenguaje plano dibuja lo que dice. Se comprueba con PIXELES reales, no
// de vista: una superficie plana es un tono exacto, no lleva sombra, y el acento
// solo aparece donde debe. Sin esta prueba, un degradado que se cuele o un arco
// que se salga de su pista pasarian desapercibidos hasta abrir la app.
static void hTestFlat() {
    G::GdiplusStartupInput gsi;
    ULONG_PTR tok = 0;
    if (G::GdiplusStartup(&tok, &gsi, nullptr) != G::Ok) {
        hCheck(false, "J0 el lenguaje plano arranca", "GDI+ no arranco");
        return;
    }
    // TODO lo de GDI+ (lienzo, pinceles, el dibujo y las lecturas de pixel) vive
    // en este bloque: GdiplusShutdown exige que no quede ningun objeto GDI+ con
    // vida. Con el bitmap y el Graphics a nivel de funcion, el apagado movia
    // tierra bajo ellos y el proceso caia con 0xC0000005.
    {
        const int W = 230, H = 130;
        G::Bitmap bmp(W, H, PixelFormat32bppARGB);
        G::Graphics g(&bmp);
        g.Clear(kFBg);

        auto px = [&bmp](int x, int y) -> G::Color {
            G::Color c; bmp.GetPixel(x, y, &c); return c;
        };
        auto same = [](G::Color c, G::Color t, int tol) -> bool {
            int dr = (int)c.GetR() - (int)t.GetR();
            int dg = (int)c.GetG() - (int)t.GetG();
            int db = (int)c.GetB() - (int)t.GetB();
            if (dr < 0) dr = -dr; if (dg < 0) dg = -dg; if (db < 0) db = -db;
            return dr <= tol && dg <= tol && db <= tol;
        };
        // el ambar del sistema, contado en un cuadro (robusto al antialiasing)
        auto countAcc = [&](int x0, int y0, int x1, int y1) -> int {
            int n = 0;
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
                const G::Color c = px(x, y);
                if (c.GetR() > 190 && c.GetG() > 120 && c.GetG() < 215 && c.GetB() < 130) ++n;
            }
            return n;
        };
        char d[180];

        // J1: una superficie plana es UN tono, y no proyecta nada sobre el lienzo
        flatPanel(g, 12, 12, 60, 40, 8.0f);
        const bool pTono = same(px(42, 32), kFSurf, 2);
        const bool pLimp = same(px(9, 32), kFBg, 0);      // 3px a la izquierda: sin sombra
        sprintf_s(d, "centro = kFSurf (%d), lienzo limpio a la izquierda (%d)", pTono, pLimp);
        hCheck(pTono && pLimp, "J1 flatPanel pinta un tono plano y sin sombra", d);

        // J2: el pozo plano es el lienzo: la profundidad la da el tono
        flatWell(g, 80, 12, 60, 40, 8.0f);
        const bool pPozo = same(px(110, 32), kFBg, 0);
        hCheck(pPozo, "J2 flatWell pinta el tono del lienzo",
               pPozo ? "centro = kFBg" : "el centro no es kFBg");

        // J3: la pastilla activa tiÃ±e su borde del color del piloto y enciende un LED
        flatPillBody(g, 150, 12, 60, 40, false, true, kAcc);
        const int led = countAcc(150, 12, 210, 52);
        hCheck(led > 0, "J3 la pastilla activa lleva piloto ambar", led ? "LED presente" : "sin LED");

        // J4: el mando plano = disco liso + arco de valor + puntero
        flatKnob(g, 45, 95, 22, 1.0f, kAcc, false, false);
        const bool disco = same(px(45, 87), kFSurf2, 3);   // dentro del disco, no en el puntero
        const int arco = countAcc(20, 70, 70, 120);
        sprintf_s(d, "disco liso (%d), px de arco ambar (%d)", disco, arco);
        hCheck(disco && arco >= 8, "J4 el mando plano dibuja disco y arco de valor", d);

        // J5: el medidor plano rellena continuo, sin marco alrededor
        flatWell(g, 95, 86, 120, 5, 2.5f);
        flatMeterFill(g, 95, 75, 120, 0.8f, 0.8f);
        const int rell = countAcc(95, 82, 215, 96);
        hCheck(rell > 0, "J5 el medidor plano rellena en ambar", rell ? "relleno presente" : "sin relleno");

        // J6: la mezcla de color es estable y tira a ambar (nada de tintes a ojo)
        const G::Color t1 = flatTint(kFSurf3, kAcc, 0.30f);
        const G::Color t2 = flatTint(kFSurf3, kAcc, 0.30f);
        const bool tOk = same(t1, t2, 0) && t1.GetR() > t1.GetG() && t1.GetG() > t1.GetB();
        hCheck(tOk, "J6 el tinte de activo es estable y tira a ambar",
               tOk ? "flatTint es estable" : "flatTint no es estable");

        // J7: el mango de MEZCLA corre DENTRO de su pista. El recorrido se
        // calculaba sobre el ancho entero, asi que al 100% (el valor de fabrica)
        // medio mango quedaba fuera del carril.
        {
            const G::REAL m0 = mixCapX(0.0f), m1 = mixCapX(1.0f);
            const bool dentro =
                (m0 - (G::REAL)MIX_HND_W*0.5f) > (G::REAL)MIX_SL_X &&
                (m1 + (G::REAL)MIX_HND_W*0.5f) < (G::REAL)(MIX_SL_X + MIX_SL_W);
            const float ida = mixSliderFrac((int)mixCapX(0.5f));   // ida y vuelta
            const bool vuelta = (ida > 0.49f && ida < 0.51f);
            sprintf_s(d, "centro a 0%% = %.0f, a 100%% = %.0f, ida y vuelta = %.2f",
                      (double)m0, (double)m1, (double)ida);
            hCheck(dentro && vuelta, "J7 el mango de MEZCLA corre dentro de su pista", d);
        }

        // cuantos pixeles TIRAN A AMBAR hay en un cuadro (el borde apagado de la
        // pastilla no llega al umbral del ambar vivo, pero tiene que ser calido)
        auto warm = [&](int x0, int y0, int x1, int y1) -> int {
            int n = 0;
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
                const G::Color c = px(x, y);
                if ((int)c.GetR() - (int)c.GetB() > 40 && (int)c.GetR() - (int)c.GetG() > 20) ++n;
            }
            return n;
        };
        // el piloto: solo cuenta el ambar CLARO (0xffdd9a), que no es el del rotulo
        auto isLit = [&](int x0, int y0, int x1, int y1) -> int {
            int n = 0;
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
                const G::Color c = px(x, y);
                if (c.GetR() > 220 && c.GetG() > 200 && c.GetB() > 120) ++n;
            }
            return n;
        };

        // J8: la pastilla del estado de audio sin sesion. El color lo pone quien
        // la llama (ambar = suena, rojo de aviso = apagado); lo que se comprueba
        // aqui es que el PILOTO no se encienda: el punto solo dice "hay sesion".
        {
            G::Font fp(kFontUI, kTCuerpo);
            const int wA = (int)statPill(g, 10.0f, 106.0f, L"SILENCIO",
                                         G::Color(0x96, 0xff, 0xa5, 0x1f), false, fp);
            const int amb = warm(10, 103, 10 + wA, 125);
            const int led = isLit(18, 112, 26, 120);
            sprintf_s(d, "ancho=%d, px calidos en el cuerpo=%d, px de piloto=%d", wA, amb, led);
            hCheck(amb > 0 && led == 0,
                   "J8 la pastilla del estado no enciende el piloto si no hay sesion", d);
        }
        // J9: con la sesion en linea el piloto se enciende. Es lo unico que
        // distingue ONLINE de SILENCIO a un metro de la pantalla.
        {
            G::Font fp(kFontUI, kTCuerpo);
            statPill(g, 10.0f, 106.0f, L"ONLINE", kAcc, true, fp);
            const int led = isLit(18, 112, 26, 120);
            sprintf_s(d, "px de piloto encendido=%d", led);
            hCheck(led > 0, "J9 con la sesion en linea el piloto de la pastilla se enciende", d);
        }
    }
    G::GdiplusShutdown(tok);
}

// K: la tabla unica de zonas. Es lo que impide que el clic y el dibujo se
// separen otra vez: se comprueba que las zonas existen donde deben, que caben
// en el lienzo y que con un modal abierto NO queda ninguna viva (un clic a
// ciegas sobre un control tapado por el modal es justo lo que la regla de un
// solo modal evita).
static void hTestZones() {
    const bool sT3k = g_t3kOpen, sSet = g_settingsOpen, sAdd = g_addOpen;
    const bool sTun = g_tunerOpen.load();

    g_t3kOpen = false; g_settingsOpen = false; g_addOpen = false; g_tunerOpen.store(false);
    buildZones();

    static const int ids[15] = { 4, 5, 6, 74, 75, 91, 76, 92, 77, 93, 101, 70, 71, 72, 73 };
    int faltan = 0, fuera = 0;
    for (int i = 0; i < 15; ++i) {
        const RECT r = zoneRect(ids[i]);
        if (r.right - r.left <= 0 || r.bottom - r.top <= 0) ++faltan;
        else if (r.left < 0 || r.top < 0 || r.right > WIN_W || r.bottom > WIN_H) ++fuera;
    }
    char d[200];
    sprintf_s(d, "sin zona=%d, fuera del lienzo=%d", faltan, fuera);
    hCheck(faltan == 0 && fuera == 0, "K1 las quince zonas existen y caben en el lienzo", d);

    // el rectangulo de la tabla es el MISMO que documenta la constante: si
    // alguien teclea mal un id en buildZones, esto lo caza
    const RECT a = zoneRect(4);
    const bool same4 = (a.left == TOP_AUDIO_X && a.top == 14 &&
                        a.right == TOP_AUDIO_X + 100 && a.bottom == 54);
    sprintf_s(d, "zona 4 = [%d,%d,%d,%d]", (int)a.left, (int)a.top, (int)a.right, (int)a.bottom);
    hCheck(same4, "K2 la zona del boton AUDIO cae en su constante", d);

    // clic y hover leen lo mismo: el centro de una zona SIEMPRE acierta
    int cx = 0, cy = 0;
    zoneCenter(6, cx, cy);
    hCheck(zoneHitAt(6, cx, cy), "K3 el centro de una zona siempre responde",
           zoneHitAt(6, cx, cy) ? "el centro acierta" : "el centro falla");

    // con un modal abierto no queda ninguna zona viva
    g_settingsOpen = true;
    buildZones();
    int vivas = 0;
    for (int i = 0; i < 15; ++i) { const RECT r = zoneRect(ids[i]); if (r.right > r.left) ++vivas; }
    g_settingsOpen = false;
    sprintf_s(d, "zonas vivas con el panel de ajustes abierto = %d", vivas);
    hCheck(vivas == 0, "K4 un modal abierto deja la tabla vacia", d);

    // L: los rectangulos DERIVADOS (navegador y pedalboard). Antes vivian en dos
    // sitios con numeros distintos: la pintura de la fila media 4px menos que su
    // banda de pulsacion, asi que el clic desbordaba lo que se veia. Aqui quedan
    // clavados a las constantes para que no vuelvan a separarse.
    const RECT rw0 = navRowRect(0), rp0 = navRowPaintRect(0);
    sprintf_s(d, "fila=[%d,%d,%d,%d] pintada=[%d,%d,%d,%d]",
              (int)rw0.left, (int)rw0.top, (int)rw0.right, (int)rw0.bottom,
              (int)rp0.left, (int)rp0.top, (int)rp0.right, (int)rp0.bottom);
    hCheck(rw0.left == COL_L_X + 8 && rw0.top == LIST_Y &&
           rw0.right == COL_L_X + 8 + (COL_L_W - 16) && rw0.bottom == LIST_Y + LIST_ROW_H &&
           rp0.top == rw0.top && rp0.bottom == rw0.bottom - 4,
           "L1 la fila y su version pintada difieren solo en el aire", d);

    const RECT bA = navStarRect(2);
    const int bx = COL_L_X + 8, bw = COL_L_W - 16, by = LIST_Y + 2*LIST_ROW_H;
    sprintf_s(d, "estrella=[%d..%d] alto=[%d..%d]",
              (int)bA.left, (int)bA.right, (int)bA.top, (int)bA.bottom);
    hCheck(bA.left == bx + bw - 28 && bA.right == bx + bw - 2 &&
           bA.top == by + 4 && bA.bottom == by + LIST_ROW_H - 8,
           "L2 la estrella de favoritos cae en su columna del borde derecho", d);

    const RECT sb = pedSlotBox((int)pedSlotX(3), PED_ROW_Y[0]);
    sprintf_s(d, "slot3=[%d,%d,%d,%d]", (int)sb.left, (int)sb.top, (int)sb.right, (int)sb.bottom);
    hCheck(sb.left == (int)pedSlotX(3) && sb.right == (int)pedSlotX(3) + PED_SLOT_W &&
           sb.top == PED_ROW_Y[0] && sb.bottom == PED_ROW_Y[0] + PED_SLOT_H,
           "L3 la caja del slot de pedal cubre el slot entero", d);

    // el estado original del proceso queda intacto
    g_t3kOpen = sT3k; g_settingsOpen = sSet; g_addOpen = sAdd; g_tunerOpen.store(sTun);
    buildZones();
}

// ---------------------------------------------------------------------------
//  R: los detalles del paso 16, clavados a numeros comprobables (no hay pixeles
//  aqui: son las constantes que la pintura y el clic comparten).
//   - el AIRE del pie y el de la tarjeta de pedal,
//   - la JERARQUIA de la cabecera (el filete va JUSTO en medio del hueco: si
//     alguien mueve un boton, el filete deja de estar centrado),
//   - la PROPORCION del visualizador (pantalla, no cinta) y su centrado.
// ---------------------------------------------------------------------------
static void hTestDesign() {
    char d[200];

    // R1: el pie respira igual arriba y abajo, y los mandos NO se han movido. El
    // aire del pie se gano bajando 4px el alto de los controles, no moviendolos:
    // si alguien "arregla" el aire moviendo la linea, esto lo caza.
    const bool r1a = (BOT_LBL_Y - FOOT_Y) == 6;                                   // 6 arriba
    const bool r1b = (FOOT_Y + FOOT_H) - (BOT_BOX_Y + 40) == 6;                   // 6 abajo
    const bool r1c = (BOT_BOX_Y + 20) == (FOOT_Y + 44);                           // centro intacto
    const bool r1d = (MASTER_Y + MASTER_R) == (BOT_BOX_Y + 40) &&
                     (IN_TRIM_Y + IN_TRIM_R) <= (BOT_BOX_Y + 40);                 // nada se sale
    sprintf_s(d, "arriba=%d abajo=%d centro=%d caben=%d",
              (int)(BOT_LBL_Y - FOOT_Y), (int)((FOOT_Y + FOOT_H) - (BOT_BOX_Y + 40)),
              (int)(BOT_BOX_Y + 20), (int)r1d);
    hCheck(r1a && r1b && r1c && r1d, "S1 el pie respira 6/6 con el centro de sus mandos intacto", d);

    // R2: el switch de la tarjeta de pedal deja 4px al canto (el criterio que se
    // pidio: la pista del bypass no se pega al borde de la chapa).
    const int swAir = PED_SLOT_H - (PED_TRK_Y + PED_TRK_H);
    sprintf_s(d, "aire bajo el switch = %d", swAir);
    hCheck(swAir == 4, "S2 el switch del pedal deja 4px al canto de su tarjeta", d);

    // R3: el filete de la cabecera parte el hueco entre los dos grupos en dos
    // mitades (y con sitio para respirar): dos grupos, no tres botones sueltos.
    const int gA = TOP_SEP_X - (TOP_SAVE_X + 100);
    const int gB = TOP_REC_X - TOP_SEP_X;
    sprintf_s(d, "hueco izquierdo=%d derecho=%d", gA, gB);
    hCheck(gA == gB && gA >= 6, "S3 el filete de la cabecera parte el hueco en dos", d);

    // R4: el visualizador es una PANTALLA centrada en su columna, no una cinta de
    // canto a canto: 3:2 (15/10) y el mismo margen por los dos lados.
    const int cl   = SCOPE_X - (COL_R_X + 8);
    const int cr   = (COL_R_X + COL_R_W - 8) - (SCOPE_X + SCOPE_W);
    const int prop = SCOPE_W*10/SCOPE_H;
    const int air  = PANEL_Y + PANEL_H - (SCOPE_Y + SCOPE_H);
    sprintf_s(d, "proporcion=%d/10 margenes=%d/%d aire al fondo=%d", prop, cl, cr, air);
    hCheck(cl == cr && prop >= 14 && prop <= 17 && air >= 4,
           "S4 el visualizador es una pantalla centrada y cabe en el panel", d);
}

// ---------------------------------------------------------------------------
//  Q: los PRESETS y su modelo. Lo que este bloque vigila:
//   - el banco de fabrica (seis lotes contiguos por ampli) y la variante de cada
//     lote dentro de su ampli,
//   - el ORDEN del catalogo, con los presets del usuario pegados a los suyos,
//   - que la lista del SIDEBAR sea solo de amplificadores (paso 19) y que su
//     estrella filtre los amplis marcados,
//   - la marca de MODIFICADO, que sigue al rig y no a la MEZCLA,
//   - los rotulos: la banda del cabezal dice el ampli y el lote, no el indice
//     global,
//   - el hueco del buscador, que dice lo que filtra.
//  Monta su propia lista de presets de usuario (guardada y restaurada al salir:
//  la lista real del usuario no se toca, y no escribe el .ini).
// ---------------------------------------------------------------------------
static void hTestPresets() {
    const std::vector<UserPreset> bak = g_userPresets;
    const int  bakIdx  = g_presetIdx;
    const bool bakFav  = g_favOnly;
    const unsigned char bakF0 = g_favPreset[3];
    WCHAR bakFilt[40];
    wcsncpy_s(bakFilt, g_ampFilter, _TRUNCATE);
    char d[320];

    // Q0: el banco de fabrica: los seis lotes de un ampli son contiguos y en
    // orden. Todo el agrupado se apoya en eso (y el generador lo promete).
    int malorgan = 0;
    for (int i = 0; i < kNumPresets; ++i) {
        if (presetAmp(i) != i / kPresetsPerAmp) ++malorgan;
        if (presetSlot(i) != (i % kPresetsPerAmp) + 1) ++malorgan;
    }
    sprintf_s(d, "13 x %d = %d presets, %d fuera de su ampli", kPresetsPerAmp, kNumPresets, malorgan);
    hCheck(kNumPresets == kAmpCount*kPresetsPerAmp && malorgan == 0,
           "Q0 los seis lotes de cada ampli son contiguos y en orden", d);

    // Lista propia, determinista: un preset en el ampli 5 y otro en el 12 (el
    // PERFIL, que el tope viejo de "model > 11" convertia en el ampli 0).
    g_userPresets.clear();
    { UserPreset u = {}; wcsncpy_s(u.name, L"PRUEBA EN EL 5", _TRUNCATE);      u.model = 5;  g_userPresets.push_back(u); }
    { UserPreset u = {}; wcsncpy_s(u.name, L"PRUEBA EN PERFIL", _TRUNCATE);    u.model = 12; g_userPresets.push_back(u); }
    { UserPreset u = {}; wcsncpy_s(u.name, L"OTRA EN EL 5", _TRUNCATE);        u.model = 5;  g_userPresets.push_back(u); }
    wcscpy_s(g_ampFilter, L"");
    g_favOnly = false;

    // Q1: la VARIANTE dentro de su ampli: 1..6 los de fabrica, y los del usuario
    // continuan la cuenta de su propio ampli (7, 8...).
    bool q1 = true;
    for (int s = 0; s < kPresetsPerAmp; ++s)
        if (presetInGroup(5*kPresetsPerAmp + s) != s + 1) q1 = false;
    const bool q1b = (presetInGroup(kNumPresets)     == kPresetsPerAmp + 1);   // PRUEBA EN EL 5
    const bool q1c = (presetInGroup(kNumPresets + 1) == kPresetsPerAmp + 1);   // PRUEBA EN PERFIL
    const bool q1e = (presetInGroup(kNumPresets + 2) == kPresetsPerAmp + 2);   // OTRA EN EL 5
    const bool q1d = (presetInGroup(kNumPresets - 1) == kPresetsPerAmp);       // ultimo de fabrica
    sprintf_s(d, "ampli 5: %d..%d; los del usuario = %d y %d; el del PERFIL = %d",
              presetInGroup(5*kPresetsPerAmp), presetInGroup(5*kPresetsPerAmp + 5),
              presetInGroup(kNumPresets), presetInGroup(kNumPresets + 2),
              presetInGroup(kNumPresets + 1));
    hCheck(q1 && q1b && q1c && q1d && q1e, "Q1 la variante cuenta dentro del ampli (1..6, el del usuario sigue)", d);

    // Q2: el ORDEN: por ampli, con los del usuario pegados a los suyos.
    int ord[kMaxListAll];
    const int nord = presetOrder(ord);
    int dup = 0, seen[kMaxListAll] = {};
    int posU5 = -1, posU5b = -1, posU12 = -1;
    for (int i = 0; i < nord; ++i) {
        if (ord[i] >= 0 && ord[i] < kMaxListEntries) { if (seen[ord[i]]) ++dup; seen[ord[i]] = 1; }
        if (ord[i] == kNumPresets)     posU5  = i;
        if (ord[i] == kNumPresets + 2) posU5b = i;
        if (ord[i] == kNumPresets + 1) posU12 = i;
    }
    const bool in5  = (posU5  > 0 && presetGroup(ord[posU5  - 1]) == 5  && presetGroup(ord[posU5])  == 5);
    const bool in5b = (posU5b > 0 && presetGroup(ord[posU5b - 1]) == 5  && presetGroup(ord[posU5b]) == 5);
    const bool in12 = (posU12 > 0 && presetGroup(ord[posU12 - 1]) == 12 && presetGroup(ord[posU12]) == 12);
    sprintf_s(d, "%d entradas (esperadas %d), duplicadas %d; los dos del ampli 5 en %d y %d y el del PERFIL en %d, pegados a su lote",
              nord, presetTotal(), dup, posU5, posU5b, posU12);
    hCheck(nord == presetTotal() && dup == 0 && in5 && in5b && in12,
           "Q2 la lista ordena por ampli y mete los del usuario con los suyos", d);

    // Q3: la lista del sidebar es SOLO de amplificadores (paso 19): una fila por
    // ampli, los trece, sin encabezados intercalados y sin presets. El defecto que
    // caza: que vuelva a colarse la lista de presets (que ya vive en el navegador)
    // o un encabezado de grupo en una lista que ya es plana.
    int rows[kMaxNavRows];
    const int nr = navRows(rows);
    int heads = 0, amps = 0, fuera = 0;
    for (int i = 0; i < nr; ++i) {
        if (rows[i] == -1) { ++heads; continue; }
        if (rows[i] >= 0 && rows[i] < kAmpCount) ++amps; else ++fuera;
    }
    sprintf_s(d, "%d filas = %d amplis + %d encabezados (%d fuera de rango)", nr, amps, heads, fuera);
    hCheck(nr == kAmpCount && amps == kAmpCount && heads == 0 && fuera == 0,
           "Q3 la lista del sidebar es solo de amplis: trece filas planas", d);

    // Q4: la estrella del sidebar filtra los AMPLIS marcados (paso 19: antes
    // filtraba la pestana activa y, con la pestana PRESETS retirada, solo queda
    // esta lista). Se marcan dos amplis; el filtro deja exactamente esos dos, y
    // los favoritos de PRESET (que son del navegador) no entran en la cuenta.
    const DWORD bakMask = g_favMask;
    g_favPreset[3] = 2;                       // un favorito de preset, en rojo
    g_favMask = (1u << 2) | (1u << 9);        // dos amplis marcados
    g_favOnly = true;
    int fi[kMaxListEntries], fc = 0;
    listFilterIdx(fi, &fc);
    int soloMarca = 1;
    for (int i = 0; i < fc; ++i) if (!ampFav(fi[i])) soloMarca = 0;
    const bool dosOk = (fc == 2 && ((fi[0] == 2 && fi[1] == 9) || (fi[0] == 9 && fi[1] == 2)));
    g_favOnly = false;
    int fi2[kMaxListEntries], fc2 = 0;
    listFilterIdx(fi2, &fc2);
    sprintf_s(d, "con el filtro: %d filas, todas marcadas = %d; los amplis 2 y 9 = %d; sin el: %d",
              fc, soloMarca, dosOk ? 1 : 0, fc2);
    hCheck(fc == 2 && soloMarca && dosOk && fc2 == kAmpCount,
           "Q4 el filtro del sidebar deja los amplis marcados y no toca los presets", d);
    g_favPreset[3] = 0;
    g_favMask = bakMask;

    // Q5: el punto de MODIFICADO sigue al rig, no a lo que no es parte del preset
    // (la MEZCLA). Recien marcado: limpio; una perilla: sucio; la MEZCLA: limpio.
    rigMark();
    const bool limpio = !presetDirty();
    const int mm = g_params.model.load();
    const float v0 = g_params.amp[mm][0].load();
    g_params.amp[mm][0].store(v0 + 1.0f);
    const bool sucio = presetDirty();
    g_params.amp[mm][0].store(v0);
    const float mix0 = g_params.mixCab.load();
    g_params.mixCab.store(mix0 > 50.0f ? mix0 - 10.0f : mix0 + 10.0f);
    const bool mixNo = !presetDirty();
    g_params.mixCab.store(mix0);
    sprintf_s(d, "recien aplicado limpio = %d; tras mover una perilla sucio = %d; tras la MEZCLA sigue limpio = %d",
              limpio ? 1 : 0, sucio ? 1 : 0, mixNo ? 1 : 0);
    hCheck(limpio && sucio && mixNo, "Q5 el punto de MODIFICADO sigue al rig, no a la MEZCLA", d);
    rigMark();

    // Q6: el rotulo: la banda del cabezal dice el ampli y el lote, no "PRESET 31"
    // (el indice global, que no es una posicion que nadie pueda buscar).
    WCHAR tag[48]; presetTagW(5*kPresetsPerAmp + 2, tag, 48);
    const std::wstring wt(tag);
    const bool tagOk = wt.find(kAmpNames[5]) != std::wstring::npos &&
                       wt.find(L"3") != std::wstring::npos &&
                       wt.find(L"PRESET") == std::wstring::npos;
    sprintf_s(d, "cabecera = \"%ls\"", tag);
    hCheck(tagOk, "Q6 el rotulo dice el ampli y el lote, no el indice global", d);

    // Q7: el hueco del buscador dice lo que la lista filtra: amplificadores. La
    // pestana PRESETS se retiro (paso 19), asi que ya no puede haber un hueco que
    // prometa presets en un campo que filtra amplis.
    const std::wstring h0 = searchHint();
    sprintf_s(d, "hueco del buscador = \"%ls\"", h0.c_str());
    hCheck(h0.find(L"amplificador") != std::wstring::npos &&
           h0.find(L"preset") == std::wstring::npos,
           "Q7 el buscador dice lo que filtra: amplificadores", d);

    // Q8: la estrella del filtro vive en la fila del buscador sin pisarlo, y no
    // baja hasta la lista (es un mando del buscador, no de la lista).
    const RECT sB = { SEARCH_X, SEARCH_Y, SEARCH_X + SEARCH_W, SEARCH_Y + SEARCH_H };
    const RECT st = searchStarRect();
    sprintf_s(d, "buscador [%d..%d]; estrella [%d..%d] alto %d..%d (la lista empieza en %d)",
              (int)sB.left, (int)sB.right, (int)st.left, (int)st.right,
              (int)st.top, (int)st.bottom, (int)LIST_Y);
    hCheck(sB.right <= st.left && st.right <= COL_L_X + COL_L_W &&
           st.bottom <= LIST_Y && st.left >= COL_L_X,
           "Q8 la estrella del filtro no pisa el buscador ni la lista", d);

    // la lista del usuario (y el filtro y el foco) quedan como estaban
    g_userPresets = bak;
    g_presetIdx = bakIdx;
    g_favOnly = bakFav;
    g_favPreset[3] = bakF0;
    wcsncpy_s(g_ampFilter, bakFilt, _TRUNCATE);
    rigMark();
}

// ---------------------------------------------------------------------------
//  T: el modelo del navegador de presets (paso 18).
//  El caracter de fabrica sale del lote del preset; los favoritos son COLORES y
//  viajan por el .ini; un preset del usuario lleva metadatos (autor, comentario,
//  etiquetas, fechas); y el orden/filtro de la ventana combina etiquetas. Se
//  prueba la vuelta completa por el .ini contra un temporal, como hIniContract.
// ---------------------------------------------------------------------------
static void hTestBrowse() {
    char d[380];

    // T1: el caracter del banco. Los cinco primeros lotes de cada ampli son el
    // arco del generador (limpio, limpio, crunch, lead, ambiente) y el lote 6 es
    // fuzz en los amplis 0,1,4,6,8 y "otros" en el resto.
    {
        static const unsigned char want5[kAmpCount]      = { 4,4,5,5,4,5,4,5,4,5,5,5,5 };
        static const unsigned char wantSlot[kPresetsPerAmp] = { 0,0,1,2,3,5 };
        int malos = 0;
        for (int i = 0; i < kNumPresets; ++i) {
            const int slot = i % kPresetsPerAmp;
            const int a    = i / kPresetsPerAmp;
            const unsigned char w = (slot == 5) ? want5[a] : wantSlot[slot];
            if (presetChar(i) != w) ++malos;
        }
        sprintf_s(d, "%d de %d con el caracter del lote equivocado", malos, kNumPresets);
        hCheck(kNumPresets == 78 && malos == 0,
               "T1 el caracter de fabrica sale del lote del preset", d);
    }

    // T2: la mascara de etiquetas. De fabrica es el bit de su caracter; un preset
    // del usuario lleva la que le pongan (0 = sin etiqueta).
    {
        const std::vector<UserPreset> bak = g_userPresets;
        g_userPresets.clear();
        UserPreset u = {}; wcsncpy_s(u.name, L"CON ETIQUETAS", _TRUNCATE);
        u.model = 3; u.tags = (1u << 1) | (1u << 4);
        g_userPresets.push_back(u);
        const bool fab = (presetTags(2) == (1u << 1));               // lote 3 = crunch
        const bool mio = (presetTags(kNumPresets) == ((1u << 1) | (1u << 4)));
        sprintf_s(d, "fabrica(crunch)=%u, mio=%u", presetTags(2), presetTags(kNumPresets));
        hCheck(fab && mio, "T2 la mascara de etiquetas es la del caracter o la del usuario", d);
        g_userPresets = bak;
    }

    // T3 + T4: la vuelta completa por el .ini y la migracion de un .ini viejo.
    {
        const std::wstring tmpDir = exeDirW() + L"\\_dsp_out";
        CreateDirectoryW(tmpDir.c_str(), nullptr);
        const std::wstring tmp = tmpDir + L"\\browse_contract.txt";
        const std::wstring realPath = g_iniPath;
        const std::wstring realIR   = g_cabIRPath;
        const std::vector<UserPreset> bakUsers = g_userPresets;
        unsigned char bakFav[kNumPresets];
        for (int i = 0; i < kNumPresets; ++i) bakFav[i] = g_favPreset[i];
        const int bakIdx = g_presetIdx;
        DeleteFileW(tmp.c_str());
        g_iniPath = tmp;

        // T3: un preset con TODOS los metadatos, guardado y releido por la via real.
        {
            g_userPresets.clear();
            UserPreset u = {};
            wcsncpy_s(u.name, L"MI CRUNCH", _TRUNCATE);
            u.model = 4; u.favColor = 3;
            wcsncpy_s(u.author,  L"YO", _TRUNCATE);
            wcsncpy_s(u.comment, L"Cuerpo del 800 con brillo", _TRUNCATE);
            u.tags = (1u << 1) | (1u << 4);
            u.created = 1700000000; u.used = 1700000123;
            g_userPresets.push_back(u);
            g_favPreset[5] = 2; g_favPreset[9] = 4;
            g_presetIdx = kNumPresets;
            saveIni();
            g_userPresets.clear();
            for (int i = 0; i < kNumPresets; ++i) g_favPreset[i] = 0;
            userPresetsLoadFromIni();
            presetFavLoadFromIni();
            bool ok = (g_userPresets.size() == 1);
            if (ok) {
                const UserPreset& r = g_userPresets[0];
                ok = wcscmp(r.name, L"MI CRUNCH") == 0 && r.model == 4 && r.favColor == 3 &&
                     wcscmp(r.author, L"YO") == 0 &&
                     wcscmp(r.comment, L"Cuerpo del 800 con brillo") == 0 &&
                     r.tags == ((1u << 1) | (1u << 4)) &&
                     r.created == 1700000000 && r.used == 1700000123;
            }
            const bool favOk = (g_favPreset[5] == 2 && g_favPreset[9] == 4 && g_favPreset[0] == 0);
            sprintf_s(d, "preset releido=%d (n=%d), favoritos 5=%d 9=%d 0=%d",
                      (int)ok, (int)g_userPresets.size(), g_favPreset[5], g_favPreset[9], g_favPreset[0]);
            hCheck(ok && favOk, "T3 metadatos y color de favorito vuelven del .ini intactos", d);
        }

        // T4: un .ini de ANTES del navegador (presetFav '0'/'1' y user fav=1, sin
        // ninguna clave nueva) tiene que cargar: color 1 (ambar) y metadatos vacios.
        {
            DeleteFileW(tmp.c_str());
            std::string doc = "[state]\r\npresetFav=";
            for (int i = 0; i < kNumPresets; ++i) doc.push_back(i == 3 ? '1' : '0');
            doc.append("\r\n[user]\r\ncount=1\r\n[user0]\r\nname=VIEJO\r\nmodel=5\r\nfav=1\r\n");
            HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD wrc = 0;
            if (f != INVALID_HANDLE_VALUE) {
                WriteFile(f, doc.data(), (DWORD)doc.size(), &wrc, nullptr);
                CloseHandle(f);
            }
            g_userPresets.clear();
            for (int i = 0; i < kNumPresets; ++i) g_favPreset[i] = 0;
            userPresetsLoadFromIni();
            presetFavLoadFromIni();
            const bool ok = (g_userPresets.size() == 1) &&
                            (g_userPresets[0].favColor == 1) &&
                            (g_userPresets[0].author[0] == 0) &&
                            (g_userPresets[0].tags == 0) &&
                            (g_userPresets[0].created == 0) &&
                            (g_favPreset[3] == 1) && (g_favPreset[2] == 0);
            sprintf_s(d, "n=%d favColor=%d fav3=%d autor='%ls'",
                      (int)g_userPresets.size(),
                      g_userPresets.empty() ? -1 : g_userPresets[0].favColor,
                      g_favPreset[3], g_userPresets.empty() ? L"?" : g_userPresets[0].author);
            hCheck(ok, "T4 un .ini de antes del navegador carga en color 1 (ambar)", d);
        }

        g_userPresets = bakUsers;
        for (int i = 0; i < kNumPresets; ++i) g_favPreset[i] = bakFav[i];
        g_presetIdx = bakIdx;
        g_iniPath = realPath;
        g_cabIRPath = realIR;
        DeleteFileW(tmp.c_str());
    }

    // T5: browseOrder. Los filtros se COMBINAN (AND entre grupos) y el orden se
    // aplica; el conjunto filtrado es el mismo que leen el pintado y el clic.
    {
        const std::vector<UserPreset> bak = g_userPresets;
        const int bs = g_brwSort, bc = g_brwColor, bo = g_brwOrigin;
        const unsigned int bch = g_brwChar, bam = g_brwAmp;
        WCHAR bt[64]; wcsncpy_s(bt, g_brwText, _TRUNCATE);

        g_userPresets.clear();
        { UserPreset u = {}; wcsncpy_s(u.name, L"ALFA", _TRUNCATE); u.model = 5; u.tags = (1u << 0); u.used = 100; g_userPresets.push_back(u); }
        { UserPreset u = {}; wcsncpy_s(u.name, L"ZETA", _TRUNCATE); u.model = 5; u.tags = (1u << 1); u.favColor = 2; u.used = 200; g_userPresets.push_back(u); }

        int o[kMaxListAll];
        g_brwSort = 0; g_brwColor = -1; g_brwOrigin = -1; g_brwChar = 0; g_brwAmp = 0; g_brwText[0] = 0;
        const int nAll = browseOrder(o);

        g_brwOrigin = 1;                                    // solo los mios
        const int nMio = browseOrder(o);

        g_brwChar = (1u << 1);                              // y solo el CRUNCH -> ZETA
        const int nCr = browseOrder(o);
        const bool crZeta = (nCr == 1 && wcscmp(g_userPresets[o[0] - kNumPresets].name, L"ZETA") == 0);

        g_brwChar = 0; g_brwColor = 2;                      // color rojo -> ZETA
        const int nCol = browseOrder(o);

        g_brwColor = -1; g_brwOrigin = -1; g_brwAmp = (1u << 5);   // ampli 5: 6 + 2
        const int nAmp = browseOrder(o);

        g_brwAmp = 0; g_brwText[0] = 0;
        wcsncpy_s(g_brwText, L"zeta", _TRUNCATE);           // el buscador
        const int nTxt = browseOrder(o);
        g_brwText[0] = 0;

        g_brwSort = 4;                                      // RECIENTES: ZETA, ALFA, ...
        const int nRec = browseOrder(o);
        const bool recOk = (nRec == presetTotal()) &&
                           wcscmp(g_userPresets[o[0] - kNumPresets].name, L"ZETA") == 0 &&
                           wcscmp(g_userPresets[o[1] - kNumPresets].name, L"ALFA") == 0;

        g_brwSort = 1;                                      // A-Z: el menor primero
        const int nAz = browseOrder(o);
        WCHAR menor[128] = L""; int mAmp = -1;
        for (int i = 0; i < presetTotal(); ++i) {
            WCHAR nm[128]; presetNameLow(i, nm, 128);
            if (!menor[0] || wcscmp(nm, menor) < 0) { wcscpy_s(menor, nm); mAmp = i; }
        }
        const bool azOk = (nAz == presetTotal() && o[0] == mAmp);

        sprintf_s(d, "todo=%d(=%d) mios=%d crunch+mios=%d(%d) rojo=%d ampli5=%d texto=%d recientes=%d(%d) A-Z=%d(%d)",
                  nAll, presetTotal(), nMio, nCr, (int)crZeta, nCol, nAmp, nTxt, nRec, (int)recOk, nAz, (int)azOk);
        hCheck(nAll == presetTotal() && nMio == 2 && nCr == 1 && crZeta && nCol == 1 &&
               nAmp == kPresetsPerAmp + 2 && nTxt == 1 && recOk && azOk,
               "T5 el navegador filtra combinando etiquetas y ordena", d);

        g_brwSort = bs; g_brwColor = bc; g_brwOrigin = bo;
        g_brwChar = bch; g_brwAmp = bam;
        wcsncpy_s(g_brwText, bt, _TRUNCATE);
        g_userPresets = bak;
    }

    // T6: la geometria del navegador cabe en su panel y no pisa el pie. El
    // defecto que caza: la rejilla de amplificadores se salia por abajo y el
    // ultimo chip se dibujaba encima del pie.
    {
        brwLayout();
        const int pieY = BRW_Y + BRW_H - 40;
        int fuera = 0, pisa = 0;
        RECT all[5 + kFavColors + kBrwOrgCount + kCharCount + kAmpCount + 5];
        int m = 0;
        all[m++] = g_brw.search; all[m++] = g_brw.hdr; all[m++] = g_brw.list;
        all[m++] = g_brw.close;  all[m++] = g_brw.info;
        for (int i = 0; i < kFavColors; ++i) all[m++] = g_brw.fav[i];
        for (int i = 0; i < kBrwOrgCount; ++i)          all[m++] = g_brw.org[i];
        for (int i = 0; i < kCharCount; ++i) all[m++] = g_brw.chr[i];
        for (int i = 0; i < kAmpCount; ++i)  all[m++] = g_brw.amp[i];
        for (int i = 0; i < 5; ++i)          all[m++] = g_brw.ord[i];
        for (int i = 0; i < m; ++i) {
            const RECT& r = all[i];
            if (r.left < BRW_X || r.right > BRW_X + BRW_W ||
                r.top < BRW_Y || r.bottom > BRW_Y + BRW_H) ++fuera;
            if (r.bottom > pieY) ++pisa;
        }
        sprintf_s(d, "%d de %d cajas fuera del panel, %d pisando el pie (y=%d)", fuera, m, pisa, pieY);
        hCheck(fuera == 0 && pisa == 0, "T6 la geometria del navegador cabe en su panel", d);
    }

    // T7: el clic cae donde el pintado puso la caja. Se pulsa el centro de un
    // chip de caracter, uno de amplificador, una pastilla de orden, un color de
    // favorito y un origen, y el estado cambia como si pintado y hit-test fueran
    // la misma tabla (que es justo lo que garantiza brwLayout).
    {
        const int bs = g_brwSort, bc = g_brwColor, bo = g_brwOrigin;
        const unsigned int bch = g_brwChar, bam = g_brwAmp;
        g_brwOpen = true;
        g_brwSort = 0; g_brwColor = -1; g_brwOrigin = -1; g_brwChar = 0; g_brwAmp = 0;
        brwLayout();
        const RECT& rc = g_brw.chr[2];       // LEAD
        brwClick((rc.left + rc.right)/2, (rc.top + rc.bottom)/2);
        const bool c1 = (g_brwChar == (1u << 2));
        const RECT& ra = g_brw.amp[7];
        brwClick((ra.left + ra.right)/2, (ra.top + ra.bottom)/2);
        const bool c2 = (g_brwAmp == (1u << 7));
        const RECT& ro = g_brw.ord[3];       // orden AMPLI
        brwClick((ro.left + ro.right)/2, (ro.top + ro.bottom)/2);
        const bool c3 = (g_brwSort == 3);
        const RECT& rf = g_brw.fav[2];       // favorito ROJO
        brwClick((rf.left + rf.right)/2, (rf.top + rf.bottom)/2);
        const bool c4 = (g_brwColor == 2);
        const RECT& rg = g_brw.org[1];       // origen FABRICA
        brwClick((rg.left + rg.right)/2, (rg.top + rg.bottom)/2);
        const bool c5 = (g_brwOrigin == 0);
        sprintf_s(d, "caracter=%d ampli=%d orden=%d favorito=%d origen=%d",
                  (int)c1, (int)c2, (int)c3, (int)c4, (int)c5);
        hCheck(c1 && c2 && c3 && c4 && c5,
               "T7 el clic del navegador cae donde el pintado puso la caja", d);
        g_brwOpen = false;
        g_brwSort = bs; g_brwColor = bc; g_brwOrigin = bo;
        g_brwChar = bch; g_brwAmp = bam;
    }

    // T8: el Info pane edita la FICHA del preset mio. Se abre la escritura de
    // cada campo, se teclean letras y se cierra: lo escrito tiene que quedar en
    // la ficha (y pasar por el .ini, que es de donde vuelve al arrancar). El
    // defecto que caza: un campo que se pinta pero no escribe en ninguna parte.
    {
        const std::wstring tmpDir = exeDirW() + L"\\_dsp_out";
        CreateDirectoryW(tmpDir.c_str(), nullptr);
        const std::wstring tmp = tmpDir + L"\\browse_edit.txt";
        const std::wstring realPath = g_iniPath;
        const std::vector<UserPreset> bak = g_userPresets;
        const int bakSel = g_brwSel, bakIdx = g_presetIdx, bakEdit = g_brwEdit;
        DeleteFileW(tmp.c_str());
        g_iniPath = tmp;

        g_userPresets.clear();
        { UserPreset u = {}; wcsncpy_s(u.name, L"VIEJO", _TRUNCATE); u.model = 5; g_userPresets.push_back(u); }
        g_brwOpen = true;
        g_brwSel  = kNumPresets;

        brwBeginEdit(2, presetAuthor(kNumPresets));            // AUTOR
        const WCHAR* who = L"YO MISMO";
        for (const WCHAR* p = who; *p; ++p) {
            const size_t n = wcslen(g_brwEditBuf);
            g_brwEditBuf[n] = *p; g_brwEditBuf[n + 1] = 0;
        }
        brwCommitEdit();
        const bool aOk = (wcscmp(presetAuthor(kNumPresets), L"YO MISMO") == 0);

        brwBeginEdit(3, presetComment(kNumPresets));           // COMENTARIO
        const WCHAR* cm = L"del puente y con sala";
        for (const WCHAR* p = cm; *p; ++p) {
            const size_t n = wcslen(g_brwEditBuf);
            g_brwEditBuf[n] = *p; g_brwEditBuf[n + 1] = 0;
        }
        brwCommitEdit();
        const bool cOk = (wcscmp(presetComment(kNumPresets), L"del puente y con sala") == 0);

        brwBeginEdit(1, L"VIEJO");                             // NOMBRE
        const bool loadOk = (wcscmp(g_brwEditBuf, L"VIEJO") == 0);   // el campo abre con lo que dice
        g_brwEditBuf[0] = 0;                                   // se borra lo viejo (como al teclear)
        const WCHAR* nm = L"MI 800";
        for (const WCHAR* p = nm; *p; ++p) {
            const size_t n = wcslen(g_brwEditBuf);
            g_brwEditBuf[n] = *p; g_brwEditBuf[n + 1] = 0;
        }
        brwCommitEdit();
        WCHAR got[128]; presetNameW(kNumPresets, got, 128);
        const bool nOk = (wcscmp(got, L"MI 800") == 0);

        // las etiquetas se PULSAN (enciende/apaga) y el color recorre los cinco
        brwToggleTag(kNumPresets, 2);
        const bool tOk1 = (presetTags(kNumPresets) == (1u << 2));
        brwToggleTag(kNumPresets, 2);
        const bool tOk2 = (presetTags(kNumPresets) == 0);
        brwCycleFav(kNumPresets);
        const bool fOk = (presetFavColor(kNumPresets) == 1);

        // y todo tiene que VOLVER del .ini
        g_userPresets.clear();
        userPresetsLoadFromIni();
        bool rt = (g_userPresets.size() == 1);
        if (rt) {
            const UserPreset& r = g_userPresets[0];
            rt = wcscmp(r.name, L"MI 800") == 0 &&
                 wcscmp(r.author, L"YO MISMO") == 0 &&
                 wcscmp(r.comment, L"del puente y con sala") == 0 &&
                 r.tags == 0 && r.favColor == 1;
        }
        sprintf_s(d, "autor=%d comentario=%d nombre=%d (abre=%d) etiquetas=%d/%d color=%d vuelta=%d",
                  (int)aOk, (int)cOk, (int)nOk, (int)loadOk, (int)tOk1, (int)tOk2, (int)fOk, (int)rt);
        hCheck(aOk && cOk && nOk && loadOk && tOk1 && tOk2 && fOk && rt,
               "T8 el Info pane escribe la ficha del preset mio y la ficha vuelve", d);

        g_brwEdit = bakEdit; g_brwEditBuf[0] = 0;
        g_brwOpen = false;
        g_brwSel = bakSel; g_presetIdx = bakIdx;
        g_userPresets = bak;
        g_iniPath = realPath;
        DeleteFileW(tmp.c_str());
    }

    // T9: GUARDAR COMO hereda los metadatos del preset que sonaba. El defecto que
    // caza: el preset nuevo nacia SIN etiqueta (solo salia en "sin etiqueta") y
    // habia que etiquetarlo a mano para poder encontrarlo.
    {
        const std::wstring tmpDir = exeDirW() + L"\\_dsp_out";
        CreateDirectoryW(tmpDir.c_str(), nullptr);
        const std::wstring tmp = tmpDir + L"\\browse_saveas.txt";
        const std::wstring realPath = g_iniPath;
        const std::vector<UserPreset> bak = g_userPresets;
        const int bakIdx = g_presetIdx;
        DeleteFileW(tmp.c_str());
        g_iniPath = tmp;

        g_userPresets.clear();
        // slot 2 de fabrica = CRUNCH (bySlot { 0,0,1,2,3,5 }); se le pone color 3
        // para ver que el color tambien viaja.
        g_favPreset[2] = 3;
        g_presetIdx = 2;
        const int before = (int)g_userPresets.size();
        savePresetAs(L"COPIA DEL CRUNCH");
        const int after = (int)g_userPresets.size();
        const bool ok = (after == before + 1) &&
                        (g_userPresets[after - 1].tags == presetTags(2)) &&
                        (g_userPresets[after - 1].tags == (1u << 1)) &&
                        (g_userPresets[after - 1].favColor == 3) &&
                        (g_userPresets[after - 1].created != 0);
        sprintf_s(d, "n=%d->%d etiquetas=%u (esperado %u) color=%d",
                  before, after,
                  (unsigned)g_userPresets[after - 1].tags, (unsigned)presetTags(2),
                  (int)g_userPresets[after - 1].favColor);
        hCheck(ok, "T9 GUARDAR COMO hereda el caracter y el color del que sonaba", d);

        g_favPreset[2] = 0;
        g_userPresets = bak;
        g_presetIdx = bakIdx;
        g_iniPath = realPath;
        DeleteFileW(tmp.c_str());
    }

    // T10: BORRAR en el Info pane (paso 19). El aspa que el sidebar tenia en la
    // fila de un preset mio bajo a la FICHA, y sigue pidiendo DOS clics: el
    // primero arma el boton y NO borra, el segundo borra. El defecto que caza:
    // que un solo roce se lleve un preset, y que el boton siga vivo en un preset
    // de fabrica (que no se borra).
    {
        const std::wstring tmpDir = exeDirW() + L"\\_dsp_out";
        CreateDirectoryW(tmpDir.c_str(), nullptr);
        const std::wstring tmp = tmpDir + L"\\browse_del.txt";
        const std::wstring realPath = g_iniPath;
        const std::vector<UserPreset> bak = g_userPresets;
        const bool bakOpen = g_brwOpen;
        const int  bakSel  = g_brwSel;
        const int  bakIdx  = g_presetIdx;
        const int  bakArm  = g_delArm;
        DeleteFileW(tmp.c_str());
        g_iniPath = tmp;

        g_userPresets.clear();
        { UserPreset u = {}; wcsncpy_s(u.name, L"UNO", _TRUNCATE); u.model = 3; g_userPresets.push_back(u); }
        { UserPreset u = {}; wcsncpy_s(u.name, L"DOS", _TRUNCATE); u.model = 5; g_userPresets.push_back(u); }
        g_brwOpen = true;
        g_brwSel  = kNumPresets;               // el primero de los mios
        g_delArm  = -1;
        brwLayout();
        const int bx = (g_brw.iDel.left + g_brw.iDel.right) / 2;
        const int by = (g_brw.iDel.top + g_brw.iDel.bottom) / 2;

        brwClick(bx, by);                      // primer clic: ARMA
        const int  n1  = (int)g_userPresets.size();
        const bool arm = (g_delArm == kNumPresets);

        brwClick(bx, by);                      // segundo clic: BORRA
        const int  n2  = (int)g_userPresets.size();
        const bool gone = (n2 == 1) && wcscmp(g_userPresets[0].name, L"DOS") == 0;
        const bool off  = (g_delArm == -1) && (g_brwSel == -1);

        // en un preset de FABRICA el boton no existe: pulsarlo no borra nada
        g_brwSel = 0;                          // SPARKLE TWIN, de fabrica
        const int n3 = (int)g_userPresets.size();
        brwClick(bx, by);
        const bool fabOk = ((int)g_userPresets.size() == n3) && (g_delArm == -1);

        sprintf_s(d, "un clic: n=%d armado=%d; dos clics: n=%d queda=%ls fuera=%d; en fabrica n=%d",
                  n1, (int)arm, n2, n2 ? g_userPresets[0].name : L"", (int)off,
                  (int)g_userPresets.size());
        hCheck(n1 == 2 && arm && n2 == 1 && gone && off && fabOk,
               "T10 BORRAR en el Info pane pide dos clics y no existe en un preset de fabrica", d);

        g_brwOpen = bakOpen; g_brwSel = bakSel; g_presetIdx = bakIdx; g_delArm = bakArm;
        g_userPresets = bak;
        g_iniPath = realPath;
        DeleteFileW(tmp.c_str());
    }

    // T11: la caja del rotulo del pie da cabida a la LINEA entera del texto. El
    // defecto que caza: los medidores rotulaban con el papel de CUERPO (Segoe UI
    // 10,5, linea de 20,4 px) dentro de una caja de 14 px, y el pintado les
    // recortaba la fila de abajo de las letras. Se veia como "el texto se corta
    // por abajo" en INPUT, OUTPUT y GATE, y solo en esos tres: los rotulos de los
    // mandos iban con el papel de rotulo, cuya linea si entraba en la caja.
    {
        G::GdiplusStartupInput gsi;
        ULONG_PTR tok = 0;
        if (G::GdiplusStartup(&tok, &gsi, nullptr) == G::Ok) {
            G::REAL lineH = 0, lineW = 0;
            {   // los objetos de GDI+ se sueltan ANTES del shutdown (trampa 0xC0000005)
                G::Bitmap bmp(8, 8, PixelFormat32bppARGB);
                G::Graphics g(&bmp);
                G::Font f(kFootLblFam, kFootLblPt);
                G::RectF box(0, 0, 1000, 1000), out;
                g.MeasureString(L"OUTPUT", -1, &f, box, nullptr, &out);
                lineH = out.Height;
                lineW = out.Width;
            }
            sprintf_s(d, "la linea de \"OUTPUT\" mide %.1f px en una caja de %d px (ancho %.1f)",
                      lineH, BOT_LBL_H, lineW);
            hCheck(lineH <= (G::REAL)BOT_LBL_H - 2.0f,
                   "T11 la caja del rotulo del pie da cabida a la linea entera", d);
            G::GdiplusShutdown(tok);
        }
    }
}

// Puesta en escena de la interfaz: los mismos pasos que da WinMain, sin hilos ni
// audio (loadIni es de solo lectura) y con el motor preparado antes de pintar (si
// no, el scope y los medidores salen a cero). Lo comparten el comando render y la
// prueba de escala, que necesitan exactamente la misma foto.
static void hUiStage() {
    initDspDefaults();
    applyPreset(0);
    loadIni();
    loadAmpArt();
    rebuildKnobs();
    g_amp.prepare(48000.0f);
}
// Ventana fuera de pantalla con el MISMO WndProc que la app. WS_POPUP y sin padre:
// el cliente mide exactamente w x h, que es de lo que depende la escala.
static HWND hUiOffscreenWindow(int w, int h) {
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "GuitarAmpOffscreen";
    RegisterClassExA(&wc);
    return CreateWindowExA(0, wc.lpszClassName, "GUITAR ONE", WS_POPUP,
                           -4000, -4000, w, h, nullptr, nullptr, wc.hInstance, nullptr);
}
// Color medio del buffer trasero alrededor de un punto de CLIENTE. Se promedia
// para que medio pixel de diferencia al reescalar no cuente como fallo.
static G::Color hBufAvg(int cx, int cy, int rad) {
    long sr = 0, sg = 0, sb = 0; int n = 0;
    for (int y = cy - rad; y <= cy + rad; ++y)
        for (int x = cx - rad; x <= cx + rad; ++x) {
            if (!g_bufBits || x < 0 || y < 0 || x >= g_bufW || y >= g_bufH) continue;
            const BYTE* p = (const BYTE*)g_bufBits + ((size_t)y*g_bufW + x)*4;
            sb += p[0]; sg += p[1]; sr += p[2]; ++n;
        }
    if (!n) return G::Color(0, 0, 0);
    return G::Color(255, (BYTE)(sr/n), (BYTE)(sg/n), (BYTE)(sb/n));
}
static int hColorDist(G::Color a, G::Color b) {
    int d = abs((int)a.GetR() - (int)b.GetR());
    const int dg = abs((int)a.GetG() - (int)b.GetG()), db = abs((int)a.GetB() - (int)b.GetB());
    if (dg > d) d = dg;
    if (db > d) d = db;
    return d;
}
// ---------------------------------------------------------------------------
//  M: el pintado escala ENTERO.
//  La ventana es redimensionable y todo el pintado va en coordenadas de diseno
//  bajo una transformacion de GDI+. Lo que se salga de esa transformacion -un
//  volcado de GDI, un rectangulo sucio en diseno- se ve perfecto a 1:1, donde el
//  factor es 1 y la transformacion es la identidad, y se descuelga en cuanto la
//  ventana se amplia. El defecto real: la tarjeta de pedal se volcaba con BitBlt y
//  al ampliar la ventana aparecia desencajada de su switch, sus perillas y su aspa.
//  La prueba pinta la MISMA escena a 1:1 y a 1.25 y compara puntos elegidos para
//  que su color delate si el elemento esta donde toca: la cinta y la pista vienen
//  de la plantilla cacheada, y el mando del bypass y el aspa van por GDI+ encima.
// ---------------------------------------------------------------------------
static void hTestScale() {
    G::GdiplusStartupInput gsi;
    ULONG_PTR tok = 0;
    if (G::GdiplusStartup(&tok, &gsi, nullptr) != G::Ok) {
        hCheck(false, "M0 GDI+ arranca para la prueba de escala", "GDI+ no arranco");
        return;
    }
    {
        const std::vector<int> savePre = g_chainPre, savePost = g_chainPost;
        bool saveOn[18];
        for (int t = 0; t < 18; ++t) {
            saveOn[t] = g_params.fxOn[t].load();
            g_params.fxOn[t].store(true);   // el mando del bypass, al final de su pista
        }
        HWND hwnd = nullptr;
        {
            hUiStage();   // carga el ini, el arte y los knobs de verdad
            // Cadena conocida: tres pedales antes del cabezal y dos despues, para
            // que las DOS filas tengan tarjeta que comparar.
            publishChain(std::vector<int>{ 0, 1, 2 }, std::vector<int>{ 3, 4 });
            rebuildKnobs();
            hwnd = hUiOffscreenWindow(WIN_W, WIN_H);
        }
        if (!hwnd) {
            hCheck(false, "M0 se abre la ventana fuera de pantalla", "CreateWindowExA fallo");
        } else {
            // Puntos de diseno, uno por fila: la cinta de tipo (en lo alto de la
            // chapa), la pista del bypass y el mando encendido al final de esa pista.
            // La cinta y la pista vienen de la plantilla (lo que se volcaba mal) y el
            // mando de la capa viva: si la tarjeta y sus mandos no comparten escala,
            // los tres no pueden estar bien a la vez.
            // Medido con el fallo puesto: cinta 89 y 105, pista 9 y 2, mando 0 y 9 (la
            // tarjeta no estaba; el mando siempre iba escalado). Ajustado: cinta 5,
            // pista 0 y 1, mando 0. De ahi las tolerancias: el aspa de quitar no vale
            // como punto, su disco es 3 unidades mas oscuro que el estante (solo se
            // mediria el aspa fina, que cambia de brillo al reescalar: 44 de desvio
            // sin que hubiera nada descolocado).
            struct Probe { int x, y, rad, tol; const char* what; };
            Probe pr[6]; int np = 0;
            for (int row = 0; row < 2; ++row) {
                const int sx = (int)pedSlotX(0), sy = PED_ROW_Y[row];
                pr[np++] = { sx + 56,                        sy + 3,  1, 12, "cinta" };
                pr[np++] = { sx + PED_TRK_X + 12,            sy + PED_TRK_Y + 7, 2, 8, "pista" };
                pr[np++] = { sx + PED_TRK_X + PED_TRK_W - 8, sy + PED_TRK_Y + 7, 2, 20, "mando" };
            }
            G::Color base[6];
            InvalidateRect(hwnd, nullptr, FALSE);
            SendMessageA(hwnd, WM_PAINT, 0, 0);
            for (int i = 0; i < np; ++i) base[i] = hBufAvg(pr[i].x, pr[i].y, pr[i].rad);
            const bool tenia1a1 = (g_uiS > 0.99f && g_uiS < 1.01f);

            // La misma escena con la ventana ampliada: ahora cada punto cae donde la
            // escala dice, y el color tiene que seguir siendo el mismo.
            SetWindowPos(hwnd, nullptr, 0, 0, 1800, 1200,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd, nullptr, FALSE);
            SendMessageA(hwnd, WM_PAINT, 0, 0);
            int malos = 0, peor = 0;
            char d[240], desv[160] = "";
            for (int i = 0; i < np; ++i) {
                const int cx = (int)((G::REAL)pr[i].x*g_uiS + g_uiOX + 0.5f);
                const int cy = (int)((G::REAL)pr[i].y*g_uiS + g_uiOY + 0.5f);
                const int dd = hColorDist(base[i], hBufAvg(cx, cy, pr[i].rad));
                if (dd > peor) peor = dd;
                if (dd > pr[i].tol) ++malos;
                // el desvio de cada punto, para que el margen quede a la vista
                sprintf_s(desv + strlen(desv), sizeof(desv) - strlen(desv),
                          "%s%s=%d(tol %d)", i ? " " : "", pr[i].what, dd, pr[i].tol);
            }
            sprintf_s(d, "%d puntos, %d fuera de tolerancia a escala %.3f | %s",
                      np, malos, g_uiS, desv);
            hCheck(tenia1a1, "M1 la foto de referencia se pinta a 1:1", tenia1a1 ? "escala 1.000" : "la escala no era 1");
            hCheck(malos == 0, "M1 al ampliar la ventana cada pieza cae donde dice la escala", d);

            freeStaticScene(); freeAmpArt(); freeSlotTemplates(); freeBackbuffer();
        }
        publishChain(savePre, savePost);
        for (int t = 0; t < 18; ++t) g_params.fxOn[t].store(saveOn[t]);
    }
    G::GdiplusShutdown(tok);
}

// ---------------------------------------------------------------------------
//  N1/N2/N3: tipografia. La escala entera se apoya en tres familias que TIENEN
//  que estar instaladas: si una faltara, GDI+ no se queja, pinta con la que
//  tenga y la jerarquia se pierde en silencio (mismo sintoma que un mapa de
//  bits descolocado, y sin un solo aviso de compilacion). De ahi que se mida.
// ---------------------------------------------------------------------------
static void hTestType() {
    G::GdiplusStartupInput gsi;
    ULONG_PTR tok = 0;
    if (G::GdiplusStartup(&tok, &gsi, nullptr) != G::Ok) {
        hCheck(false, "N0 GDI+ arranca para la prueba de tipografia", "GDI+ no arranco");
        return;
    }
    {
        G::Bitmap bmp(8, 8, PixelFormat32bppARGB);
        G::Graphics g(&bmp);
        const WCHAR* probe = L"AVISO 1959 Hz \xB7 -6,0 dB \xB7 100 %";

        // N1a: cada familia existe. N1b: ademas no son la misma cara (una
        // sustitucion silenciosa igualaria dos anchos).
        const WCHAR* fams[3] = { kFontUI, kFontTec, kFontMono };
        const char*  noms[3] = { "Segoe UI", "Bahnschrift SemiCondensed", "Cascadia Mono" };
        int faltan = 0;
        char falta[200] = "";
        for (int i = 0; i < 3; ++i) {
            G::FontFamily ff(fams[i]);
            if (ff.GetLastStatus() != G::Ok || !ff.IsAvailable()) {
                ++faltan;
                sprintf_s(falta + strlen(falta), sizeof(falta) - strlen(falta),
                          "%s%s", falta[0] ? ", " : "", noms[i]);
            }
        }
        hCheck(faltan == 0, "N1 las tres familias estan instaladas",
               faltan ? falta : "Segoe UI, Bahnschrift SemiCondensed, Cascadia Mono");

        G::REAL w[3] = { 0, 0, 0 };
        for (int i = 0; i < 3; ++i) {
            G::Font f(fams[i], kTValor, G::FontStyleBold);
            w[i] = measureSpaced(g, probe, f, 0.0f);
        }
        G::REAL mn = 1e9f;
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j) {
                const G::REAL mx = (w[i] > w[j]) ? w[i] : w[j];
                const G::REAL d = (mx > 0.01f) ? (fabsf(w[i] - w[j]) / mx) : 1.0f;
                if (d < mn) mn = d;
            }
        char dn[200];
        sprintf_s(dn, "anchos %.1f / %.1f / %.1f pt; la menor diferencia es %.2f%%",
                  w[0], w[1], w[2], (double)(mn*100.0f));
        hCheck(mn > 0.01f, "N1 las tres familias no son la misma cara", dn);

        // N2: la mono es de ancho FIJO para todo glifo: el sufijo ("Hz", "dB", "%")
        // no se mueve cuando cambia el valor. Las cifras de Segoe UI ya son
        // tabulares, asi que medir "1111" contra "8888" no distingue nada; lo que
        // distingue es que la "W" y el "." ocupen lo mismo que el "1".
        G::Font fm(kFontMono, kTValor, G::FontStyleRegular);
        const G::REAL ww = measureSpaced(g, L"W", fm, 0.0f);
        const G::REAL wi = measureSpaced(g, L"i", fm, 0.0f);
        const G::REAL wd = measureSpaced(g, L".", fm, 0.0f);
        const G::REAL w1 = measureSpaced(g, L"1111", fm, 0.0f);
        const G::REAL w8 = measureSpaced(g, L"8888", fm, 0.0f);
        const G::REAL dis = fabsf(ww - wi) > fabsf(ww - wd) ? fabsf(ww - wi) : fabsf(ww - wd);
        char dt[220];
        sprintf_s(dt, "W %.2f | i %.2f | . %.2f | 1111 %.2f | 8888 %.2f (mayor desvio %.2f)",
                  ww, wi, wd, w1, w8, dis);
        hCheck(dis < 0.1f && fabsf(w1 - w8) < 0.1f,
               "N2 el mono es de ancho fijo: el sufijo no se mueve", dt);

        // N3: el embudo pone LA MEDIDA Y LA FAMILIA del papel. Con sitio de sobra
        // no encoge, y la fuente con la que mide es la que dice la tabla (si
        // midiera con otra, el encaje se calcularia con una fuente y se pintaria
        // con otra). Tambien: al subir de papel no puede bajar la medida.
        int malos = 0, sube = 1;
        G::REAL prev = 0;
        char ds[240] = "";
        for (int r = 0; r < TxRoleCount; ++r) {
            G::REAL pt = 0, sp = 0;
            fitSpacedFont(g, probe, G::FontStyleBold, 0.5f, 100000.0f,
                          kTx[r].pt, kTx[r].min, pt, sp, kTx[r].fam);
            if (fabsf(pt - kTx[r].pt) > 0.01f || fabsf(sp - 0.5f) > 0.01f) ++malos;
            G::Font fe(kTx[r].fam, kTx[r].pt, G::FontStyleBold);
            const G::REAL we = measureSpaced(g, probe, fe, 0.5f);
            G::Font fv(kTx[r].fam, pt, G::FontStyleBold);
            const G::REAL wv = measureSpaced(g, probe, fv, sp);
            if (fabsf(we - wv) > 0.5f) ++malos;
            if (r && pt < prev - 0.01f) sube = 0;
            prev = pt;
            sprintf_s(ds + strlen(ds), sizeof(ds) - strlen(ds), "%s%.1f", r ? " " : "", pt);
        }
        char d3[300];
        sprintf_s(d3, "medidas de los %d papeles: %s; %d sitios fuera",
                  (int)TxRoleCount, ds, malos);
        hCheck(sube, "N3 la escala no baja al subir de papel",
               sube ? "micro < pico < rotulo < cuerpo < valor < nombre < titulo < marca"
                    : "hay un papel mayor con medida menor");
        hCheck(malos == 0, "N3 el embudo pone la medida y la familia del papel", d3);
    }
    G::GdiplusShutdown(tok);
}

// ---------------------------------------------------------------------------
//  T12/T13: el camino de tiempo real. Procesar un bloque de audio (cadena de
//  pedales + cabezal PERFIL con una captura de verdad) no puede coger cerrojos
//  ni reservar memoria, y la instantanea del perfil se coge UNA vez por bloque,
//  no por muestra. Un fallo aqui no suena como el clic de un mando: suena a
//  crujido suelto mientras se toca, porque el hilo de audio pierde el buffer
//  esperando a la interfaz o pagando un malloc.
// ---------------------------------------------------------------------------
static void hTestRtPath(int fs) {
    namChainClear();
    std::wstring msg;
    namChainAdd(L"perfiles/wavenet_condition_dsp.nam", msg);
    {
        std::lock_guard<std::mutex> lk(g_namMutex);
        char d[200];
        sprintf_s(d, "la cadena del PERFIL tiene %d captura(s)%s", (int)g_namUi.size(),
                  g_namUi.empty() ? " - no se pudo cargar el .nam" : "");
        hCheck(g_namUi.size() == 1, "T12 hay una captura en la cadena del PERFIL", d);
    }
    // Dos parejas de pedales publicadas: el flanger y el delay reservan sus
    // buffers la PRIMERA vez que suenan, asi que quedan dentro del calentamiento.
    const std::vector<int> pre  = { 3, 8 };
    const std::vector<int> post = { 4, 5 };
    publishChain(pre, post);
    initDspDefaults();
    g_params.model.store(12);
    g_amp.prepare((float)fs);

    const int    NB   = 512;
    const double step = 2.0*M_PI*220.0/(double)fs;
    // Calentamiento (dos bloques): la primera vuelta de cada pedal y del IR del
    // gabinete reserva lo suyo. Esa reserva es de una vez y no es lo que se mide.
    for (int b = 0; b < 2; ++b) {
        audioBlockBegin();
        for (int i = 0; i < NB; ++i)
            g_amp.process(0.25f*(float)std::sin(step*(double)i), g_preSnap, g_postSnap);
    }

    // T12: una sola lectura de la instantanea del perfil en todo el bloque.
    g_namSnapGets.store(0);
    double pk = 0.0;
    audioBlockBegin();
    for (int i = 0; i < NB; ++i) {
        const float y = g_amp.process(0.25f*(float)std::sin(step*(double)i),
                                      g_preSnap, g_postSnap);
        const double a = std::fabs((double)y);
        if (a > pk) pk = a;
    }
    {
        char d[240];
        sprintf_s(d, "%lld lectura(s) del perfil en %d muestras; pico de salida %.4f",
                  g_namSnapGets.load(), NB, pk);
        hCheck(g_namSnapGets.load() <= 1 && pk > 0.005,
               "T12 la instantanea del perfil se coge una vez por bloque", d);
    }
#ifdef GA_HARNESS
    // T13: cero reservas de memoria dentro del bloque.
    g_allocCount.store(0);
    g_allocAuditOn.store(true);
    audioBlockBegin();
    for (int i = 0; i < NB; ++i)
        g_amp.process(0.25f*(float)std::sin(step*(double)i), g_preSnap, g_postSnap);
    g_allocAuditOn.store(false);
    {
        char d[200];
        sprintf_s(d, "%lld reserva(s) de memoria en un bloque de %d muestras",
                  g_allocCount.load(), NB);
        hCheck(g_allocCount.load() == 0, "T13 procesar un bloque no reserva memoria", d);
    }
#endif
    // Deja el motor como estaba.
    namChainClear();
    publishChain(std::vector<int>(), std::vector<int>());
    hPrepare(fs, 0);
}

// ---------------------------------------------------------------------------
//  T14: la publicacion de la cadena es coherente bajo carrera. El hilo de audio
//  copia la instantanea SIN cerrojo: si esa lectura pudiera pillar una escritura
//  a medias, el bloque procesaria una cadena que nadie publico (por ejemplo el
//  PRE de una y el POST de otra). Aqui un hilo publica dos cadenas distintas sin
//  parar y el bloqueo comprueba que lo que lee es UNA de las dos, entera.
// ---------------------------------------------------------------------------
static void hTestChainPub(int fs) {
    const std::vector<int> aPre = { 0, 1, 2 },  aPost = { 4, 5 };
    const std::vector<int> bPre = { 9, 10, 11 }, bPost = { 16, 17, 0 };
    publishChain(aPre, aPost);
    std::atomic<bool>      stop{false};
    std::atomic<long long> pubs{0};
    std::thread w([&] {
        while (!stop.load()) { publishChain(aPre, aPost); publishChain(bPre, bPost); pubs.fetch_add(2); }
    });
    int malos = 0, vioA = 0, vioB = 0;
    const int vueltas = 100000;
    g_chainSnapMisses.store(0);
    for (int v = 0; v < vueltas; ++v) {
        audioBlockBegin();
        if      (g_preSnap == aPre && g_postSnap == aPost) ++vioA;
        else if (g_preSnap == bPre && g_postSnap == bPost) ++vioB;
        else                                                ++malos;
    }
    stop.store(true);
    w.join();
    char d[240];
    sprintf_s(d, "%d bloques con %lld publicaciones: %d cadena A, %d cadena B, %d mezclas (%lld sin cuadrar)",
              vueltas, pubs.load(), vioA, vioB, malos, g_chainSnapMisses.load());
    hCheck(malos == 0 && vioA > 0 && vioB > 0,
           "T14 la cadena publicada se lee entera (sin mezclas)", d);
    publishChain(std::vector<int>(), std::vector<int>());
    hPrepare(fs, 0);
}

// ---------------------------------------------------------------------------
//  T15: el hilo de audio pide MMCSS ("Pro Audio"). Con SetThreadPriority a secas,
//  el sistema puede desalojar el hilo en un pico (mientras la interfaz pinta, por
//  ejemplo) y ese desalojo se oye como un crujido suelto. Se pide de verdad
//  desde este hilo y se mira si el sistema lo concede.
// ---------------------------------------------------------------------------
static void hTestRtPriority() {
    HANDLE h = audioRtEnter();
    const bool ok = (h != nullptr);
    audioRtLeave(h);
    char d[200];
    sprintf_s(d, "MMCSS \"Pro Audio\": %s (prioridad de reserva: TIME_CRITICAL)",
              ok ? "concedido" : "NO concedido");
    hCheck(ok && g_rtBoostMmcss,
           "T15 el hilo de audio consigue prioridad de tiempo real (MMCSS)", d);
}

// ---------------------------------------------------------------------------
//  T16: el convolucionador de la cabina se reserva al PREPARAR la sesion, no en
//  la primera muestra que pasa por el. Sus buffers son de tamaÃ±o fijo (N y
//  BLOCK) pero se reservaban de forma perezosa dentro del callback: seis
//  reservas en el primer bloque, justo cuando el usuario empieza a tocar. Aqui
//  se comprueba el contrato de prepare(): despues de el, dar de alta un IR (y
//  cambiar de cabina) no reserva nada, y el convolucionador sigue sonando.
// ---------------------------------------------------------------------------
static void hTestCabConvPrep(int fs) {
#ifdef GA_HARNESS
    hPrepare(fs, 0);                      // las 54 IRs fisicas ya estan generadas
    CabConv cv;
    cv.prepare();                         // lo que hace el arranque de sesion
    g_allocCount.store(0);
    g_allocAuditOn.store(true);
    cv.ensureFor(&g_procIR2[0]);          // primer IR de la sesion
    cv.ensureFor(&g_procIR2[9]);          // ...y un cambio de cabina en marcha
    g_allocAuditOn.store(false);
    // Y ademas tiene que sonar: sin esto, un prepare() vacio pasaria la prueba.
    float pk = 0.0f;
    for (int i = 0; i < 300; ++i) {
        const float y = cv.process((i == 0) ? 1.0f : 0.0f);
        const float a = std::fabs(y);
        if (a > pk) pk = a;
    }
    char d[220];
    sprintf_s(d, "%lld reserva(s) al dar de alta el IR; pico de la respuesta %.5f",
              g_allocCount.load(), pk);
    hCheck(g_allocCount.load() == 0 && pk > 0.0f,
           "T16 la cabina se reserva al preparar, no en el primer bloque", d);
    hPrepare(fs, 0);
#endif
}

// W1..W6: las capturas .nam y el nivel de las doce voces.
//  Lo que se corrigio aqui:
//   - un A2 de TONE3000 es un SlimmableContainer (y el WaveNet de dentro trae
//     kernel y activacion POR CAPA): se rechazaba con "arquitectura no
//     soportada" y esas capturas no cargaban;
//   - la ruta se pasaba a fopen en UTF-8, que en Windows no la entiende: una
//     captura en una carpeta con acentos decia "no se puede abrir el fichero";
//   - "sample_rate" no se leia nunca (todo se trataba como 48 kHz);
//   - el modelo se ejecutaba a la tasa del motor aunque no fuera la suya;
//   - DUO RECTO y KRAFT V4 iban 10,5 dB por debajo del resto.
static void hNamFixes(int fs) {
    char d[320];

    // W1: contenedor A2 completo, ejecutando el submodelo entero
    {
        nam::Model m;
        const bool ok = m.load("_nmaref/m_A2.nam");
        float pk = 0.0f, nan = 0.0f;
        if (ok) {
            m.reset();
            for (int i = 0; i < m.prewarmSamples; ++i) m.tick(0.0f);
            for (int i = 0; i < 2048; ++i) {
                const float y = m.tick(0.2f*(float)std::sin(2.0*M_PI*220.0*(double)i/(double)fs));
                if (!std::isfinite(y)) nan += 1.0f;
                const float a = std::fabs(y);
                if (a > pk) pk = a;
            }
        }
        sprintf_s(d, "_nmaref/m_A2.nam -> %s | %.0f Hz, %d canal(es), pico %.3f, no finitos %d",
                  ok ? "carga" : m.err.c_str(), m.sampleRate, m.outChannels, pk, (int)nan);
        hCheck(ok && pk > 0.0f && nan == 0.0f,
               "W1 un A2 SlimmableContainer carga y ejecuta el submodelo entero", d);
    }

    // W2: captura en una ruta con acentos (loadW, la que usa la app)
    {
        const std::wstring dir = exeDirW() + L"\\_dsp_out\\captura con acentos \u00f1\u00e1\u00e9";
        CreateDirectoryW((exeDirW() + L"\\_dsp_out").c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring dst = dir + L"\\modelo.nam";
        const BOOL cp = CopyFileW((exeDirW() + L"\\_nmaref\\m_wavenet.nam").c_str(), dst.c_str(), FALSE);
        nam::Model m;
        const bool ok = cp && m.loadW(dst);
        sprintf_s(d, "copia %s | carga %s", cp ? "hecha" : "FALLO",
                  ok ? "ok" : m.err.c_str());
        hCheck(ok, "W2 una captura en una carpeta con acentos carga", d);
        DeleteFileW(dst.c_str());
    }

    // W3: sample_rate se lee del fichero (antes quedaba clavado en 48000)
    {
        std::string src, mod;
        const bool read = hNamRead("_nmaref/m_wavenet.nam", src);
        const bool sub  = read && hNamSubst(src, "\"sample_rate\": 48000", "\"sample_rate\": 44100", mod);
        nam::Model m;
        const bool ok = sub && m.loadText(mod);
        sprintf_s(d, "sample_rate del fichero -> %.0f Hz (esperado 44100)", m.sampleRate);
        hCheck(ok && std::fabs(m.sampleRate - 44100.0) < 1.0,
               "W3 la frecuencia del .nam se lee y no se queda en 48000", d);
    }

    // W4: la conversion de frecuencia no cambia el tono ni el nivel
    {
        NamRateConv rc;
        rc.configure(48000.0, 44100.0);
        auto pass = [](float s) { return s; };
        const int N = 44100;
        std::vector<float> out((size_t)N, 0.0f);
        for (int i = 0; i < N; ++i) {
            const float x = 0.5f*(float)std::sin(2.0*M_PI*1000.0*(double)i/44100.0);
            out[(size_t)i] = rc.tick(pass, x);
        }
        const size_t i0 = (size_t)N/2, n = i0 - 16;
        const double m1 = hDft(out, i0, n, 1000.0, 44100.0);
        double hmax = 0.0;
        for (int h = 2; h <= 4; ++h) {
            const double mm = hDft(out, i0, n, 1000.0*h, 44100.0);
            if (mm > hmax) hmax = mm;
        }
        sprintf_s(d, "48k->44,1k: 1 kHz sale con amplitud %.4f (esperada 0,5000), peor armonico %.5f",
                  m1, hmax);
        hCheck(std::fabs(m1 - 0.5) < 0.01 && hmax < 0.002,
               "W4 convertir 48k a 44,1k deja el tono y el nivel en su sitio", d);
    }

    // W5: la 2a etapa asimetrica no deja continua EN LA SALIDA con la MEZCLA al
    // 0 % (sin cabina de por medio, que es el unico caso en que la continua
    // llegaria al conector). Con la cabina puesta su propio paso alto ya la quita
    // y la medida no distingue nada, por eso el caso util es este.
    {
        double worst = 0.0; int worstM = -1;
        for (int m = 0; m < 12; ++m) {
            if (kVoices[m].stage2 <= 0.01f) continue;
            hPrepare(fs, m, kVoices[m].cab, kVoices[m].mic, kVoices[m].dist);
            g_params.mixCab.store(0.0f);      // DESPUES de hPrepare: initDspDefaults la pone a 100
            std::vector<float> in = hTone(220.0, -20.0, 1.0, fs);
            std::vector<float> out = hRender(in);
            double acc = 0.0; const size_t i0 = out.size()/2, n = out.size()/2 - 16;
            for (size_t i = 0; i < n; ++i) acc += out[i0 + i];
            const double dc = std::fabs(acc/(double)n);
            const double rms = hRms(out, i0, n);
            const double rel = (rms > 1e-9) ? dc/rms : dc;
            if (rel > worst) { worst = rel; worstM = m; }
        }
        g_params.mixCab.store(100.0f);
        sprintf_s(d, "peor desvio de continua %.4f%% del rms (ampli %d), sin cabina", 100.0*worst, worstM);
        hCheck(worstM >= 0 && worst < 0.005,
               "W5 el cabezal no saca continua (se bloquea a su salida)", d);
    }

    // W6: cambiar de ampli no puede dar un salto de nivel
    {
        std::vector<float> di = hMakeDI(fs, 8.0);
        double lo = 1e9, hi = 0.0; int loM = -1, hiM = -1;
        for (int m = 0; m < 12; ++m) {
            hPrepare(fs, m, kVoices[m].cab, kVoices[m].mic, kVoices[m].dist);
            std::vector<float> out = hRender(di);
            const double r = hRms(out, 0, out.size());
            if (r < lo) { lo = r; loM = m; }
            if (r > hi) { hi = r; hiM = m; }
        }
        const double spreadDb = 20.0*std::log10((hi + 1e-12)/(lo + 1e-12));
        sprintf_s(d, "rms %.4f (%s) .. %.4f (%s) = %.2f dB de diferencia",
                  lo, kVoices[loM].name, hi, kVoices[hiM].name, spreadDb);
        hCheck(spreadDb < 3.0, "W6 los doce amplis quedan nivelados entre si", d);
    }

    // W7: un hueco del anillo NO puede meter un escalon (era el chasquido).
    //  Antes el render rellenaba con 0: en medio de una onda eso es un salto de
    //  toda la amplitud, ida y vuelta, o sea un "crack". Ahora se mantiene la
    //  ultima muestra y se desvanece: el salto entre muestras tiene que quedar
    //  al nivel del de la propia senal, no muy por encima.
    {
        const double fsd = (double)fs;
        const float fadeK = std::exp(-1.0f/(0.005f*fs));
        float last = 0.0f, fade = 1.0f;
        const int N = 4096, gapAt = 2048, gapN = 3;
        double worstStep = 0.0, refStep = 0.0;
        float prev = 0.0f;
        for (int i = 0; i < N; ++i) {
            float s;
            if (i >= gapAt && i < gapAt + gapN) s = bridgeNextGapSample(last, fade, fadeK);
            else {
                s = (float)(0.5*std::sin(2.0*M_PI*1000.0*(double)i/fsd));
                last = s; fade = 1.0f;
            }
            const double dd = std::fabs((double)s - (double)prev);
            if (i > gapAt - 2 && i < gapAt + gapN + 2) { if (dd > worstStep) worstStep = dd; }
            else if (i > 2 && dd > refStep) refStep = dd;
            prev = s;
        }
        sprintf_s(d, "peor salto en el hueco %.5f vs %.5f de la senal (x%.2f)",
                  worstStep, refStep, worstStep/(refStep + 1e-12));
        hCheck(worstStep < refStep*1.6 + 1e-4,
               "W7 un hueco del anillo no mete escalon (se mantiene, no se pone a cero)", d);
    }

    hPrepare(fs, 0);
}

// W8/W9: el boton RECARGAR DISPOSITIVOS del panel de ajustes.
//  Windows solo lista los endpoints ACTIVOS al enumerar, asi que una interfaz
//  enchufada despues de arrancar no aparece hasta volver a enumerar. Lo que se
//  comprueba aqui es lo que puede romperse al recargar: que la seleccion siga
//  al MISMO dispositivo (los indices bailan) y que no se toque el que esta
//  sonando (el cambio lo hace APLICAR).
static void hDevRefresh() {
    char d[320];
    // W8: recolocacion por ID, con listas sinteticas (no depende del hardware)
    {
        std::vector<AudioDeviceInfo> l, l2, vacia;
        l.push_back({ L"id-A", L"A" });
        l.push_back({ L"id-B", L"B" });
        l.push_back({ L"id-C", L"C" });
        l2.push_back({ L"id-B", L"B" });     // se ha ido el primero: los indices bajan
        l2.push_back({ L"id-C", L"C" });
        const int a = remapDeviceIdx(l,     L"id-C");   // 2
        const int b = remapDeviceIdx(l2,    L"id-C");   // 1 (antes era 2)
        const int c = remapDeviceIdx(l2,    L"id-A");   // 0: ya no esta
        const int e = remapDeviceIdx(vacia, L"id-A");   // 0: lista vacia
        sprintf_s(d, "mismo orden=%d | tras quitar uno=%d | desaparecido=%d | lista vacia=%d",
                  a, b, c, e);
        hCheck(a == 2 && b == 1 && c == 0 && e == 0,
               "W8 recargar recoloca la seleccion por ID, no por posicion", d);
    }
    // W9: ida y vuelta con los dispositivos de esta maquina
    {
        const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        EnumerateDevices(true);
        const size_t nin = g_inputDevices.size(), nout = g_outputDevices.size();
        std::wstring curIn, curOut;
        {
            std::lock_guard<std::mutex> lk(g_deviceMutex);
            curIn = g_currentInputId; curOut = g_currentOutputId;
        }
        // Se elige el ULTIMO de cada lista: si la recarga se guiara por indice,
        // al desaparecer uno de delante la seleccion se moveria de dispositivo.
        g_selectedInputIdx  = (int)nin  > 0 ? (int)nin  - 1 : 0;
        g_selectedOutputIdx = (int)nout > 0 ? (int)nout - 1 : 0;
        const std::wstring wantIn  = nin  ? g_inputDevices[(size_t)g_selectedInputIdx].id   : L"";
        const std::wstring wantOut = nout ? g_outputDevices[(size_t)g_selectedOutputIdx].id : L"";
        RefreshDeviceLists();
        const bool inOk  = (nin  == 0) || (g_inputDevices[(size_t)g_selectedInputIdx].id   == wantIn);
        const bool outOk = (nout == 0) || (g_outputDevices[(size_t)g_selectedOutputIdx].id == wantOut);
        std::wstring curIn2, curOut2;
        {
            std::lock_guard<std::mutex> lk(g_deviceMutex);
            curIn2 = g_currentInputId; curOut2 = g_currentOutputId;
        }
        const bool keep = (curIn == curIn2) && (curOut == curOut2);
        sprintf_s(d, "%d entrada(s) / %d salida(s) | seleccion conservada %d/%d | motor intacto %d",
                  (int)nin, (int)nout, (int)inOk, (int)outOk, (int)keep);
        hCheck(inOk && outOk && keep,
               "W9 recargar no cambia el dispositivo con el que suena el motor", d);
        if (SUCCEEDED(hrCo)) CoUninitialize();
    }
}

// W10: los presets de TONE3000 dentro del catalogo de PRESETS.
//  Se prueba con una biblioteca SINTETICA (no toca disco ni red). Lo que puede
//  romperse es el espacio de indices compartido -que un preset de TONE3000 se
//  cuele donde se espera uno del banco- y los filtros del navegador.
static void hT3kPresets() {
    char d[400];
    std::vector<t3k::LocalPreset> bak;      // la real, si la hay: se devuelve
    bak.swap(g_t3kLocal);
    g_t3kLocal.clear();
    const int base = presetTotal();

    t3k::PresetBlock b1; b1.name = "A1"; b1.url = "https://x/a1.nam"; b1.nam = true;
    t3k::PresetBlock b2; b2.name = "A2"; b2.url = "https://x/a2.nam"; b2.nam = true;
    t3k::LocalPreset p1; p1.name = "RUIDO DE FONDO"; p1.ok = true;
    p1.left.push_back(b1); p1.left.push_back(b2);
    t3k::LocalPreset p2; p2.name = "CRUNCH SUCIO"; p2.ok = true;
    p2.stereo = true; p2.left.push_back(b1);
    g_t3kLocal.push_back(p1);
    g_t3kLocal.push_back(p2);

    const bool count  = (presetTotalAll() == base + 2);
    const bool bounds = isT3kPreset(base) && isT3kPreset(base + 1) &&
                        !isT3kPreset(base - 1) && !isT3kPreset(base + 2);
    WCHAR nm[128] = L""; presetNameW(base, nm, 128);
    const WCHAR* cm = presetComment(base);
    const bool meta = (wcscmp(nm, L"RUIDO DE FONDO") == 0) &&
                      (presetGroup(base) == 12) && (presetTags(base) == 0) &&
                      (wcscmp(presetAuthor(base), L"TONE3000") == 0) &&
                      (wcsstr(cm, L"2 capturas") != nullptr) &&
                      (presetFavColor(base) == 0);

    // El orden curado los deja PEGADOS al PERFIL (grupo 12) y sin duplicados.
    int ord[kMaxListAll];
    const int nord = presetOrder(ord);
    int pos1 = -1, pos2 = -1, dup = 0;
    bool seen[kMaxListAll] = {};
    for (int i = 0; i < nord; ++i) {
        if (ord[i] >= 0 && ord[i] < kMaxListAll) { if (seen[ord[i]]) ++dup; seen[ord[i]] = true; }
        if (ord[i] == base)     pos1 = i;
        if (ord[i] == base + 1) pos2 = i;
    }

    // Filtros de origen: TONE3000 solo ellos; FABRICA y MIOS ninguno.
    const int   bakO = g_brwOrigin, bakC = g_brwColor, bakS = g_brwSort;
    unsigned int bakCh = g_brwChar, bakAm = g_brwAmp;
    WCHAR bakT[64]; wcsncpy_s(bakT, g_brwText, _TRUNCATE);
    g_brwColor = -1; g_brwChar = 0; g_brwAmp = 0; g_brwText[0] = 0; g_brwSort = 0;
    g_brwOrigin = 2; int o2[kMaxListAll]; const int n2 = browseOrder(o2);
    g_brwOrigin = 0; int o0[kMaxListAll]; const int n0 = browseOrder(o0);
    g_brwOrigin = 1; int o1[kMaxListAll]; const int n1 = browseOrder(o1);
    bool t3kOnly = (n2 == 2);
    for (int i = 0; i < n2; ++i) if (!isT3kPreset(o2[i])) t3kOnly = false;
    bool fabOk = true, mioOk = true;
    for (int i = 0; i < n0; ++i) if (isT3kPreset(o0[i])) fabOk = false;
    for (int i = 0; i < n1; ++i) if (isT3kPreset(o1[i])) mioOk = false;
    g_brwOrigin = bakO; g_brwColor = bakC; g_brwSort = bakS;
    g_brwChar = bakCh; g_brwAmp = bakAm; wcsncpy_s(g_brwText, bakT, _TRUNCATE);

    sprintf_s(d, "%d +2=%d | nombre/meta %d | orden %d,%d dup %d | origen T3K: %d (solo T3K %d) | fabrica sin T3K %d | mios sin T3K %d",
              base, (int)count, (int)meta, pos1, pos2, dup, n2, (int)t3kOnly, (int)fabOk, (int)mioOk);
    hCheck(count && bounds && meta && dup == 0 && pos1 >= 0 && pos2 == pos1 + 1 &&
           t3kOnly && fabOk && mioOk,
           "W10 los presets de TONE3000 entran en el catalogo de PRESETS", d);

    // Cargar sin sesion y sin las capturas en disco: NO puede quedarse callado
    // (tiene que abrir la hoja de conexion) y NO puede dejar el cabezal en el
    // PERFIL sin capturas, que seria silencio sin explicacion.
    {
        const int ampBak = g_params.model.load();
        g_t3kConnOpen = false;
        g_params.model.store(0);
        applyT3kPreset(base);
        const bool aviso   = g_t3kConnOpen;
        const bool intacto = (g_params.model.load() == 0);
        g_t3kConnOpen = false;
        g_params.model.store(ampBak);
        rebuildKnobs();
        sprintf_s(d, "pide conexion=%d | cabezal sin tocar=%d", (int)aviso, (int)intacto);
        hCheck(aviso && intacto,
               "W11 un preset de TONE3000 sin sesion avisa y no deja el cabezal a oscuras", d);
    }

    g_t3kLocal.clear();
    bak.swap(g_t3kLocal);
}

static void hSelftest(int fs) {
    printf("\n== autotest de correcciones ==\n");
    hNamRejects();
    hNamFixes(fs);
    hDevRefresh();
    hT3kPresets();
    hPrepareRebuild(fs);
    hRingContract();
    hIniContract();
    hTestTuner(fs);
    hTestStaticIR(fs);
    hTestPaste();
    hTestFlat();
    hTestZones();
    hTestDesign();
    hTestPresets();
    hTestBrowse();
    hTestExtraLeak(fs);
    hTestRtPath(fs);
    hTestChainPub(fs);
    hTestRtPriority();
    hTestCabConvPrep(fs);
    // P2 va al final: inyecta NaN en el motor y aunque prepare(false) lo deja
    // recuperable, el historial de las lineas de retardo queda con residuos que
    // contaminarian a cualquier prueba posterior que mida el ampli.
    hPrepareRecovery(fs);
    {   // Las tres tablas de amplis deben ir sincronizadas: si kVoices[i] no es
        // el ampli que la lista llama kAmpNames[i], ese ampli suena con la voz
        // (y la cabina, y los defaults) de otro. Encontrado asi: estaba el swap.
        int bad = 0, first = -1;
        for (int m = 0; m < 13; ++m)
            if (wstr(std::string(kVoices[m].name)) != kAmpNames[m]) { if (first < 0) first = m; ++bad; }
        char det[120];
        sprintf_s(det, "%d amplis con la voz equivocada (primero el indice %d)", bad, first);
        hCheck(bad == 0, "C kVoices y kAmpNames van en el mismo orden", det);
    }
    {   // La lista de amplis tiene que ofrecer los TRECE, con el cabezal PERFIL:
        // el bucle pedia 12 y el 13 no aparecia nunca en la interfaz.
        wcscpy_s(g_ampFilter, L"");
        int idx[kMaxListEntries], cnt = 0;
        listFilterIdx(idx, &cnt);
        bool has12 = false;
        for (int i = 0; i < cnt; ++i) if (idx[i] == 12) has12 = true;
        char d[160];
        sprintf_s(d, "%d filas de ampli (esperadas 13), PERFIL presente: %d", cnt, has12 ? 1 : 0);
        hCheck(cnt == 13 && has12, "E la lista incluye el cabezal PERFIL", d);
    }
    {   // Los trece amplis tienen su arte: los dos fallos de este tipo de
        // off-by-one eran "el PERFIL no sale en la lista" y "sale sin miniatura"
        // (ampThumb cortaba en 12). Se comprueba el dato del que dependen los dos.
        int bad = 0;
        for (int i = 0; i < kAmpCount; ++i)
            if (!kAmpArt[i].fondo || !kAmpArt[i].fondo[0] || !kAmpArt[i].strip ||
                !kAmpArt[i].strip[0] || kAmpArt[i].nall < 1) ++bad;
        char d[220];
        sprintf_s(d, "%d amplis, arte incompleto en %d; ultimo = %ls",
                  kAmpCount, bad, kAmpArt[kAmpCount - 1].fondo);
        hCheck(kAmpCount == 13 && bad == 0, "F los trece amplis tienen su arte de lista", d);
    }
    {   // A2 contra el core OFICIAL: el oraculo se compila aparte (_nmaref/ref.exe),
        // asi que si no esta se avisa y se sigue (el resto del autotest no depende
        // de el). El chef de A2 es el modo nam2; aqui se engancha para que forme
        // parte de la bateria normal.
        FILE* rf = nullptr;
        if (fopen_s(&rf, "_nmaref/ref.exe", "rb") == 0 && rf) {
            std::fclose(rf);
            hNamVerify("perfiles/wavenet_condition_dsp.nam", fs);
        } else {
            printf("  (nota) sin _nmaref/ref.exe no se verifica A2 contra el core oficial\n");
        }
    }
    const double os6 = hOs4RejectDb(fs, 6000.0), os18 = hOs4RejectDb(fs, 18000.0);
    const double os30 = hOs4RejectDb(fs, 30000.0), os42 = hOs4RejectDb(fs, 42000.0);
    printf("  B2 rechazo del diezmado 4x (dB, mas negativo = mejor):\n");
    printf("     6 kHz %7.2f dB  |  18 kHz %7.2f dB  |  30 kHz %7.2f dB  |  42 kHz %7.2f dB\n",
           os6, os18, os30, os42);
    char d[220];
    sprintf_s(d, "6 kHz %+.2f dB (debe ser plano), 30 kHz %.2f dB", os6, os30);
    hCheck(os6 > -0.5 && os30 < -50.0, "B2 el 4x filtra de verdad: pasa 6 kHz, mata 30 kHz", d);

    // N antes de M: la tipografia solo mide texto, no toca la escena ni la cadena.
    hTestType();
    // M va la ULTIMA: carga el ini y deja otra cadena de pedales, asi que no puede
    // quedar nada despues que dependa del estado del motor.
    hTestScale();
}

// ---------------------------------------------------------------------------
//  render <png> [estado] [AnchoxAlto]: fotografia de la ventana entera, para poder
//  mirar el rediseno en una maquina sin sesion grafica utilizable.
//  El tamano es opcional (por defecto el de diseno, 1440x956) y existe porque la
//  ventana es REDIMENSIONABLE: a 1:1 la transformacion de GDI+ es la identidad y
//  un pintado que se salte la escala (un BitBlt de GDI) sale bien y el fallo no se
//  ve. Pidiendo por ejemplo 1800x1200 la foto sale a 1.25 y ese fallo aparece.
//  Es EXACTAMENTE el mismo paintWindow que usa la app, en una ventana fuera de
//  pantalla; el destino real del pintado es el buffer trasero, asi que la foto
//  sale entera aunque la ventana no se vea. Con esto cada panel se revisa con
//  pixeles reales en vez de a ciegas, y se puede ensenar antes y despues.
//  NO usa DestroyWindow: su WM_DESTROY llama a saveIni() y un render no debe
//  escribir los ajustes del usuario. Al terminar se sueltan a mano los Bitmaps
//  de GDI+ y NO se llama a GdiplusShutdown (con Bitmaps vivos mata el proceso:
//  es la trampa 0xC0000005 ya documentada en hTestFlat).
// ---------------------------------------------------------------------------
static bool hPngEncoder(CLSID* out) {
    UINT num = 0, size = 0;
    if (G::GetImageEncodersSize(&num, &size) != G::Ok || size == 0) return false;
    std::vector<BYTE> buf(size);
    G::ImageCodecInfo* p = (G::ImageCodecInfo*)buf.data();
    if (G::GetImageEncoders(num, size, p) != G::Ok) return false;
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(p[i].MimeType, L"image/png") == 0) { *out = p[i].Clsid; return true; }
    return false;
}

static const void* g_renderFaultAddr = nullptr;
static int hRenderFaultFilter(EXCEPTION_POINTERS* ep) {   // el unico sitio donde
    g_renderFaultAddr = ep->ExceptionRecord->ExceptionAddress;   // es valido pedirla
    return EXCEPTION_EXECUTE_HANDLER;
}

// Sonda del pintado, aislada y sin objetos C++ para poder usar SEH. Un fallo
// dentro del WndProc lo reporta Windows como 0xC000041D (excepcion en un
// callback) y se pierde el codigo real; llamando a WM_PAINT desde aqui el
// __except si ve la excepcion de verdad.
static void hRenderPaintProbe(HWND hwnd) {
    __try {
        SendMessageA(hwnd, WM_PAINT, 0, 0);
    } __except (hRenderFaultFilter(GetExceptionInformation())) {
        printf("PINTADO_PETA %08lx en +0x%llx (el PNG sale a medias)\n",
               (unsigned long)GetExceptionCode(),
               (unsigned long long)((const char*)g_renderFaultAddr -
                                    (const char*)GetModuleHandleA(nullptr)));
    }
}

static int hRender(const char* outPath, const char* state, int winW, int winH) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // si algo peta, el log tiene que llegar
    G::GdiplusStartupInput gsi;
    ULONG_PTR tok = 0;
    if (G::GdiplusStartup(&tok, &gsi, nullptr) != G::Ok) { printf("RENDER_SIN_GDI+\n"); return 1; }

    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "GuitarAmpRenderClass";
    RegisterClassExA(&wc);

    // Fuera de pantalla: da igual, el pintado va al buffer trasero
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "GUITAR ONE", WS_POPUP,
                                -4000, -4000, winW, winH,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { printf("RENDER_SIN_VENTANA\n"); return 1; }

    // Mismo orden que WinMain, sin hilos ni audio. loadIni() es de solo lectura:
    // asi la foto sale con los presets y ajustes reales del usuario.
    initDspDefaults();
    applyPreset(0);
    loadIni();
    // GA_LANG=en retrata la interfaz en ingles (solo el banco de medida). El
    // idioma real lo decide el .ini o el del sistema.
    { const char* lg = getenv("GA_LANG");
      if (lg && lg[0] == 'e' && lg[1] == 'n') g_lang = 1;
      else if (lg && lg[0] == 'e' && lg[1] == 's') g_lang = 0; }
    loadAmpArt();
    rebuildKnobs();

    // El motor tiene que estar PREPARADO antes de pintar: en la app lo hace el
    // hilo de audio. Sin esto el frame sale con el scope y los medidores a cero
    // (y, antes de arreglar pop(), tumbaba el proceso con una division por cero).
    g_amp.prepare(48000.0f);

    // Los dispositivos, igual que WinMain: sin esto el panel de ajustes sale con
    // "(sin dispositivos)" y la foto no ensena lo que ve el usuario.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnumerateDevices();

    if (state) {   // los modales tambien se retratan
        if      (strcmp(state, "add") == 0)      { g_addOpen = true; g_addRow = 0; }
        else if (strcmp(state, "t3k") == 0)      { g_t3kOpen = true; }   // explorador TONE3000
        else if (strcmp(state, "t3kconn") == 0)  { g_t3kOpen = true; g_t3kConnOpen = true; }
        else if (strcmp(state, "longname") == 0) {   // nombre de preset largo (linea de estado)
            UserPreset u = {};
            wcsncpy_s(u.name, L"CLEAN DIAMOND WITH A VERY LONG USER PRESET NAME (LIVE ROOM)", _TRUNCATE);
            u.model = 0;
            g_userPresets.push_back(u);
            g_presetIdx = kNumPresets + (int)g_userPresets.size() - 1;
        }
        else if (strcmp(state, "t3kkey") == 0) {   // campo con un token largo (recorte)
            g_t3kOpen = true; g_t3kConnOpen = true; g_t3kFocus = 2;
            for (int i = 0; i < 180; ++i) g_t3kKey[i] = (WCHAR)(L'a' + (i % 26));
            g_t3kKey[180] = 0;
        }
        else if (strcmp(state, "tuner") == 0)    g_tunerOpen.store(true);
        else if (strcmp(state, "settings") == 0) g_settingsOpen = true;
        else if (strcmp(state, "audioon") == 0) g_audioEnabled.store(true);   // boton AUDIO ON
        else if (strcmp(state, "perfil") == 0) {   // cabezal PERFIL con una captura cargada
            g_params.model.store(12);
            std::wstring msg;
            namChainAdd(L"perfiles/wavenet_a1_standard.nam", msg);
            rebuildKnobs();
        }
        else if (strcmp(state, "browser") == 0)  { brwOpen(); g_brwSel = 0; }   // navegador (paso 18)
        else if (strcmp(state, "browser_t3k") == 0) {   // catalogo filtrado por TONE3000
            brwOpen();
            g_brwOrigin = 2;
            if (t3kPresetCount() > 0) g_brwSel = presetTotal();
            g_brwScroll = 0;
        }
        else if (strcmp(state, "browser_mio") == 0) {   // con presets mios y metadatos
            brwOpen();
            UserPreset u = {};
            wcsncpy_s(u.name, L"MI CRUNCH 800", _TRUNCATE);
            u.model = 4; u.favColor = 2;
            wcsncpy_s(u.author, L"YO", _TRUNCATE);
            wcsncpy_s(u.comment, L"Cuerpo del 800 con brillo y el delay de la sala.", _TRUNCATE);
            u.tags = (1u << 1) | (1u << 3);
            u.created = 1700000000; u.used = 1700000123;
            g_userPresets.push_back(u);
            UserPreset v = {};
            wcsncpy_s(v.name, L"AMBIENTE AZUL", _TRUNCATE);
            v.model = 8; v.favColor = 3; v.tags = (1u << 3);
            g_userPresets.push_back(v);
            g_brwSel = kNumPresets;
        }
        else if (strcmp(state, "browser_edit") == 0) {   // el Info pane escribiendo
            brwOpen();
            UserPreset u = {};
            wcsncpy_s(u.name, L"MI CRUNCH 800", _TRUNCATE);
            u.model = 4; u.favColor = 2;
            wcsncpy_s(u.author, L"YO", _TRUNCATE);
            wcsncpy_s(u.comment, L"Cuerpo del 800 con brillo y el delay de la sala.", _TRUNCATE);
            u.tags = (1u << 1) | (1u << 3);
            u.created = 1700000000; u.used = 1700000123;
            g_userPresets.push_back(u);
            g_brwSel = kNumPresets;
            brwLayout();
            brwBeginEdit(3, presetComment(kNumPresets));
        }
        else if (strcmp(state, "dirty") == 0) {          // el punto de MODIFICADO
            // El punto vive pegado al nombre del preset en la CABECERA: la fila de
            // la lista que tambien lo llevaba se fue con la pestana PRESETS (paso
            // 19), asi que este estado vigila el unico sitio donde queda.
            const int mm = g_params.model.load();
            g_params.amp[mm][0].store(g_params.amp[mm][0].load() + 6.0f);
        }
        else if (strcmp(state, "topsearch") == 0) {      // buscador de la cabecera abierto
            g_topOpen = true; g_topQuery[0] = 0; topFilter(); g_topSel = g_topCount ? 0 : -1;
        }
        else if (strcmp(state, "topsearch_q") == 0) {    // con texto tecleado
            g_topOpen = true; wcscpy_s(g_topQuery, L"crunch"); topFilter(); g_topSel = 0;
        }
        else if (strcmp(state, "rename") == 0) {         // modal de nombre (GUARDAR COMO)
            openNameModal(-1, true, L"CLEAN DIAMOND WITH A VERY LONG USER PRESET NAME (LIVE ROOM)");
        }
        else if (strcmp(state, "emptylist") == 0) {      // lista vacia: el texto guia
            g_favMask = 0;
            g_favOnly = true;
        }
        else if (strcmp(state, "favonly") == 0) {        // la estrella: solo mis amplis
            g_favMask = (1u << 0) | (1u << 4) | (1u << 10);
            g_favOnly = true;
        }
        else if (strcmp(state, "drag") == 0) {
            // Arrastre de pedal a medias: la tarjeta que sigue al raton. Es la unica
            // forma de mirar el fantasma, que solo existe mientras dura el gesto. Se
            // pone sobre el frontal del cabezal para que se vea entero.
            if (!g_chainPre.empty()) {
                g_dragPedalRow = 0; g_dragPedalIdx = 0; g_dragPedalType = g_chainPre[0];
                g_mouseX = ART_X + 210; g_mouseY = ART_Y + 150;
            }
        }
    }

    ShowWindow(hwnd, SW_SHOWNA);
    InvalidateRect(hwnd, nullptr, FALSE);
    hRenderPaintProbe(hwnd);

    int rc = 1;
    if (g_bufBits && g_bufW > 0 && g_bufH > 0) {
        surfaceDibOpaque(g_bufBits, g_bufW, g_bufH);   // sin alfa el PNG sale transparente
        {
            G::Bitmap bmp(g_bufW, g_bufH, g_bufW*4, PixelFormat32bppPARGB, (BYTE*)g_bufBits);
            CLSID enc;
            if (hPngEncoder(&enc) && bmp.Save(u8w(outPath).c_str(), &enc, nullptr) == G::Ok) {
                printf("RENDER_OK %s %dx%d %s\n", outPath, g_bufW, g_bufH, state ? state : "main");
                rc = 0;
            } else {
                printf("RENDER_SIN_GUARDAR %s\n", outPath);
            }
        }
        freeStaticScene(); freeAmpArt(); freeSlotTemplates(); freeBackbuffer();
    } else {
        printf("RENDER_SIN_BUFFER\n");
    }
    return rc;
}

int main(int argc, char** argv) {
    const int fs = 48000;
    const char* mode = (argc > 1) ? argv[1] : "all";
    if (const char* ce = getenv("CABENGINE")) g_cabEngine.store(atoi(ce) == 0 ? 0 : 1);
    // Todo el banco trabaja con rutas relativas (_dsp_out/, _nmaref/, perfiles/).
    // Se anclan al directorio del EXE para que los comandos del README funcionen
    // igual lanzados desde la raiz del repo que desde native/: si no, la prueba
    // E0 fallaba por el sitio desde el que se ejecutaba, no por el codigo, y el
    // autotest salia con error teniendo todo en verde.
    const std::wstring exeDir = exeDirW();
    if (!exeDir.empty()) SetCurrentDirectoryW(exeDir.c_str());
    printf("[motor de gabinete: %s]\n", g_cabEngine.load() == 1 ? "IR fisica" : "biquads clasicos");
    system("if not exist _dsp_out mkdir _dsp_out");
    if (strcmp(mode, "nam2") == 0) {
        printf("\n== %s: nam.h contra el core oficial ==\n", "nam2");
        hNamVerify((argc > 2) ? argv[2] : "perfiles/wavenet_a1_standard.nam", fs);
        printf("\nNAM2_FAILS=%d\n", hFail);
        printf("\nHARNESS_DONE\n");
        return hFail ? 1 : 0;
    }
    if (strcmp(mode, "presets") == 0) {
        t3kLoadLocalPresets();
        int okn = 0, bad = 0;
        for (const t3k::LocalPreset& p : g_t3kLocal) {
            if (p.ok) {
                ++okn;
                printf("%s | %s | L%d R%d%s\n", p.file.c_str(), p.name.c_str(),
                       (int)p.left.size(), (int)p.right.size(), p.stereo ? " | stereo" : "");
            } else {
                ++bad;
                printf("%s | ILEGIBLE: %s\n", p.file.c_str(), p.err.c_str());
            }
        }
        printf("\nPRESETS_OK=%d PRESETS_ILEGIBLES=%d TOTAL=%d\n", okn, bad, (int)g_t3kLocal.size());
        printf("\nHARNESS_DONE\n");
        return 0;
    }
    if (strcmp(mode, "extra") == 0) {
        hExtraReport(fs);
        printf("\nHOYOS_SIN_ACCION=%d\n", hFail);
        printf("\nHARNESS_DONE\n");
        return hFail ? 1 : 0;
    }
    if (strcmp(mode, "render") == 0) {
        const char* outP = (argc > 2) ? argv[2] : "_dsp_out/render.png";
        const char* st   = (argc > 3) ? argv[3] : nullptr;
        int rw = WIN_W, rh = WIN_H;   // por defecto, tamano de diseno (escala 1)
        if (argc > 4) {
            int a = 0, b = 0;
            if (sscanf_s(argv[4], "%dx%d", &a, &b) == 2 && a >= 320 && b >= 240) {
                rw = a; rh = b;
            } else {
                printf("RENDER_TAMANO_ILEGIBLE %s (se espera AnchoxAlto)\n", argv[4]);
            }
        }
        const int r = hRender(outP, st, rw, rh);
        printf("\nRENDER_FAILS=%d\n", r);
        printf("\nHARNESS_DONE\n");
        return r ? 1 : 0;
    }
    if (strcmp(mode, "selftest") == 0) {
        hSelftest(fs);
        printf("\nSELFTEST_FAILS=%d\n", hFail);
        printf("\nHARNESS_DONE\n");
        return hFail ? 1 : 0;
    }
    const bool all = (strcmp(mode, "all") == 0);

    printf("== respuesta en frecuencia (dB, 19 puntos 40..12k) ==\n");
    printf("%-18s", "Hz");
    for (int i = 0; i < 19; ++i) printf(" %6.0f", kHRespF[i]);
    printf("\n");
    if (all || strcmp(mode, "resp") == 0) {
        for (int m = 0; m < 12; ++m) {
            hPrepare(fs, m, kVoices[m].cab, kVoices[m].mic, kVoices[m].dist);
            hRespLine(kVoices[m].name, fs);
        }
        for (int c = 0; c < 6; ++c) {
            char tag[64]; sprintf_s(tag, "cab%d", c);
            hPrepare(fs, 0, c, 1, 1);
            hRespLine(tag, fs);
        }
        for (int mi = 0; mi < 3; ++mi) {
            char tag[64]; sprintf_s(tag, "mic%d-ne", mi);
            hPrepare(fs, 0, 2, mi, 0);
            hRespLine(tag, fs);
        }
        for (int d = 0; d < 3; ++d) {
            char tag[64]; sprintf_s(tag, "cab2-mic1-d%d", d);
            hPrepare(fs, 0, 2, 1, d);
            hRespLine(tag, fs);
        }
    }

    if (all || strcmp(mode, "thd") == 0) {
        printf("\n== THD+N %% a 220 Hz (nivel de entrada -42..-6 dB) ==\n");
        for (int m = 0; m < 12; ++m) {
            hPrepare(fs, m, kVoices[m].cab, kVoices[m].mic, kVoices[m].dist);
            hThdLine(kVoices[m].name, fs);
        }
    }

    if (all || strcmp(mode, "alias") == 0) {
        printf("\n== energia no armonica %% (tono 3.3 kHz, gain 50/70/90) ==\n");
        for (int m = 0; m < 12; ++m) {
            hPrepare(fs, m, kVoices[m].cab, kVoices[m].mic, kVoices[m].dist);
            hAliasLine(kVoices[m].name, fs);
        }
    }

    if (all || strcmp(mode, "wav") == 0) {
        std::vector<float> di = hMakeDI(fs, 8.0);
        hWav16("_dsp_out/di_dry.wav", di, fs);
        for (int m = 0; m < 12; ++m) {
            hPrepare(fs, m, kVoices[m].cab, kVoices[m].mic, kVoices[m].dist);
            std::vector<float> out = hRender(di);
            char p[128]; sprintf_s(p, "_dsp_out/amp_%02d_%s.wav", m, kVoices[m].name);
            for (char* q = p; *q; ++q) if (*q == ' ') *q = '_';
            hWav16(p, out, fs);
            double pk = 0.0;
            for (float v : out) pk = std::max(pk, (double)std::fabs(v));
            printf("[wav] %s  peak=%.3f rms=%.4f\n", p, pk, hRms(out, 0, out.size()));
        }
    }

    if (all || strcmp(mode, "ir") == 0) {
        printf("\n== IR del gabinete (respuesta de impulso, dB rel. pico) ==\n");
        for (int c = 0; c < 6; ++c) {
            for (int mi = 0; mi < 3; ++mi) {
                for (int d = 0; d < 3; ++d) {
                    hPrepare(fs, 0, c, mi, d);
                    std::vector<float> imp(2048, 0.0f), out;
                    imp[0] = 1.0f;
                    out = hRender(imp);
                    double pk = 1e-9;
                    for (float v : out) pk = std::max(pk, (double)std::fabs(v));
                    char p[160]; sprintf_s(p, "_dsp_out/ir_cab%d_mic%d_d%d.txt", c, mi, d);
                    FILE* fo = nullptr;
                    if (fopen_s(&fo, p, "w") == 0 && fo) {
                        for (size_t i = 0; i < 512; ++i)
                            fprintf(fo, "%zu %.2f\n", i, 20.0*std::log10((std::fabs((double)out[i]) + 1e-9)/pk));
                        fclose(fo);
                    }
                }
            }
        }
        printf("  (512 muestras por combinacion cab/mic/dist -> _dsp_out/ir_*.txt)\n");
    }

    if (all || strcmp(mode, "fx") == 0) {
        static const char* kFxNames[18] = {
            "GATE","COMP","DRIVE","CHORUS","DELAY","REVERB","TREMOLO","PHASER","FLANGER",
            "EQ","OVERDRIVE","FUZZ","DIST","CHORUS BBD","TAPE ECHO","SPRING","BOOST","AUTO-WAH" };
        const std::vector<int> noSel;
        printf("\n== nivel por pedal (dB respecto al DI, con el pedal solo) ==\n");
        std::vector<float> di = hMakeDI(fs, 8.0);
        const double dryRms = hRms(di, 0, di.size());
        for (int t = 0; t < 18; ++t) {
            hPrepare(fs, 0, 0, 1, 1);
            for (int f = 0; f < 18; ++f) g_params.fxOn[f].store(f == t);
            std::vector<int> pre; pre.push_back(t);
            std::vector<float> out(di.size(), 0.0f);
            for (size_t i = 0; i < di.size(); ++i) out[i] = g_amp.process(di[i], pre, noSel);
            const double r = hRms(out, 0, out.size());
            printf("  fx%02d %-12s %+6.2f dB   rms=%.4f\n",
                   t, kFxNames[t], 20.0*std::log10((r + 1e-12)/(dryRms + 1e-12)), r);
        }
    }

    if (strcmp(mode, "perf") == 0 || all) {
        const size_t n = (size_t)fs*10;
        std::vector<float> in(n, 0.0f);
        for (size_t i = 0; i < n; ++i) in[i] = (float)(0.05*std::sin(2.0*3.14159265358979*220.0*(double)i/(double)fs));
        hPrepare(fs, 11, 2, 0, 1);                     // peor caso: high-gain + cab IR
        LARGE_INTEGER fq, t0, t1;
        QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
        const std::vector<int> noSel;
        volatile float sink = 0.0f;
        for (size_t i = 0; i < n; ++i) sink += g_amp.process(in[i], noSel, noSel);
        QueryPerformanceCounter(&t1);
        const double cpu = (double)(t1.QuadPart - t0.QuadPart)/(double)fq.QuadPart;
        const double audio = (double)n/(double)fs;
        printf("\n== coste DSP ==\n  %.3f s de CPU para %.1f s de audio -> %.2f%% de un nucleo (x%.1f tiempo real)\n",
               cpu, audio, 100.0*cpu/audio, audio/cpu);
        (void)sink;
    }

    printf("\nHARNESS_DONE\n");
    return 0;
}
#endif   // GA_HARNESS
