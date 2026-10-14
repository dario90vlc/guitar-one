#pragma once
// ===========================================================================
//  t3k.h - cliente minimo de la API de TONE3000
// ---------------------------------------------------------------------------
//  Solo lo que hace falta para el explorador de perfiles:
//    - HTTPS con WinHTTP (sin dependencias externas)
//    - un lector JSON pequeno (arboles de objetos/arrays)
//    - OAuth 2.0 flujo de DISPOSITIVO (RFC 8628): la app muestra un codigo,
//      el usuario lo confirma en el navegador y la app recibe el token.
//      Es el flujo que la propia documentacion da para hosts sin redireccion.
//    - busqueda, listado de modelos y descarga del .nam
//
//  La busqueda (/tones/search) EXIGE token: sin el devuelve 401.
// ===========================================================================

#include <string>
#include <vector>
#include <utility>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>

namespace t3k {

// ---------------------------------------------------------------------------
//  JSON minimo
// ---------------------------------------------------------------------------
struct Jv
{
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool        b = false;
    double      n = 0.0;
    std::string s;
    std::vector<Jv> a;
    std::vector<std::pair<std::string, Jv>> o;

    const Jv* get(const char* k) const
    {
        for (const auto& kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    const Jv* at(size_t i) const { return (i < a.size()) ? &a[i] : nullptr; }
    size_t size() const { return (t == ARR) ? a.size() : (t == OBJ ? o.size() : 0); }
    std::string str(const char* k = nullptr, const char* def = "") const
    {
        if (k) { const Jv* v = get(k); return (v && v->t == STR) ? v->s : def; }
        return (t == STR) ? s : def;
    }
    double num(const char* k = nullptr, double def = 0.0) const
    {
        if (k) { const Jv* v = get(k); return (v && v->t == NUM) ? v->n : def; }
        return (t == NUM) ? n : def;
    }
    long long i64(const char* k = nullptr, long long def = 0) const
    {
        const double d = num(k, (double)def);
        return (long long)d;
    }
};

inline bool jParse(const std::string& s, Jv& out);

namespace jdetail {
inline size_t ws(const std::string& s, size_t p)
{
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
    return p;
}
inline bool str(const std::string& s, size_t& p, std::string& out)
{
    if (p >= s.size() || s[p] != '"') return false;
    p++;
    out.clear();
    while (p < s.size())
    {
        const char c = s[p];
        if (c == '\\' && p + 1 < s.size())
        {
            const char e = s[p + 1];
            switch (e)
            {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                {
                    if (p + 5 < s.size())
                    {
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++)
                        {
                            const char h = s[p + 2 + i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        }
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        p += 6;
                        continue;
                    }
                    break;
                }
                default: out += e; break;
            }
            p += 2;
            continue;
        }
        if (c == '"') { p++; return true; }
        out += c;
        p++;
    }
    return false;
}
inline bool value(const std::string& s, size_t& p, Jv& out, int depth)
{
    if (depth > 48) return false;
    p = ws(s, p);
    if (p >= s.size()) return false;
    const char c = s[p];
    if (c == '{')
    {
        out.t = Jv::OBJ;
        p++;
        p = ws(s, p);
        if (p < s.size() && s[p] == '}') { p++; return true; }
        while (p < s.size())
        {
            std::string key;
            if (!str(s, p, key)) return false;
            p = ws(s, p);
            if (p >= s.size() || s[p] != ':') return false;
            p++;
            Jv v;
            if (!value(s, p, v, depth + 1)) return false;
            out.o.emplace_back(key, std::move(v));
            p = ws(s, p);
            if (p < s.size() && s[p] == ',') { p++; p = ws(s, p); continue; }
            if (p < s.size() && s[p] == '}') { p++; return true; }
            return false;
        }
        return false;
    }
    if (c == '[')
    {
        out.t = Jv::ARR;
        p++;
        p = ws(s, p);
        if (p < s.size() && s[p] == ']') { p++; return true; }
        while (p < s.size())
        {
            Jv v;
            if (!value(s, p, v, depth + 1)) return false;
            out.a.push_back(std::move(v));
            p = ws(s, p);
            if (p < s.size() && s[p] == ',') { p++; p = ws(s, p); continue; }
            if (p < s.size() && s[p] == ']') { p++; return true; }
            return false;
        }
        return false;
    }
    if (c == '"') { out.t = Jv::STR; return str(s, p, out.s); }
    if (s.compare(p, 4, "true") == 0)  { out.t = Jv::BOOL; out.b = true;  p += 4; return true; }
    if (s.compare(p, 5, "false") == 0) { out.t = Jv::BOOL; out.b = false; p += 5; return true; }
    if (s.compare(p, 4, "null") == 0)  { out.t = Jv::NUL; p += 4; return true; }
    {
        char* endp = nullptr;
        const double d = std::strtod(s.c_str() + p, &endp);
        if (endp == s.c_str() + p) return false;
        out.t = Jv::NUM; out.n = d;
        p = (size_t)(endp - s.c_str());
        return true;
    }
}
} // namespace jdetail

inline bool jParse(const std::string& s, Jv& out)
{
    size_t p = 0;
    if (!jdetail::value(s, p, out, 0)) return false;
    return true;
}

// ---------------------------------------------------------------------------
//  HTTP (WinHTTP)
// ---------------------------------------------------------------------------
struct Resp
{
    int         status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

// method: "GET" | "POST"
inline bool httpRequest(const wchar_t* method, const std::string& url, const std::string& body,
                        const std::string& token, Resp& out, std::string* err = nullptr)
{
    out.status = 0; out.body.clear();
    auto fail = [&](const char* m) { if (err) *err = m; return false; };

    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = L"", path[2048] = L"";
    uc.lpszHostName = host;  uc.dwHostNameLength = 255;
    uc.lpszUrlPath  = path;  uc.dwUrlPathLength  = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return fail("URL invalida");

    HINTERNET ses = WinHttpOpen(L"GuitarAmp/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return fail("WinHttpOpen");
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) { WinHttpCloseHandle(ses); return fail("sin conexion"); }
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, method, path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return fail("sin peticion"); }

    std::wstring hdr;
    if (!token.empty())
    {
        std::wstring wt(token.begin(), token.end());
        hdr += L"Authorization: Bearer " + wt + L"\r\n";
    }
    if (!body.empty()) hdr += L"Content-Type: application/x-www-form-urlencoded\r\n";
    hdr += L"Accept: application/json\r\n";

    BOOL ok = WinHttpSendRequest(req, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(),
                                 hdr.empty() ? 0 : (DWORD)-1L,
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                 (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok)
    {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return fail("fallo de red");
    }
    DWORD code = 0, len = sizeof(code);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)code;

    DWORD avail = 0;
    bool truncated = false;
    for (;;)
    {
        if (!WinHttpQueryDataAvailable(req, &avail)) { truncated = true; break; }
        if (!avail) break;
        std::string chunk; chunk.resize(avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, &chunk[0], avail, &got)) { truncated = true; break; }
        chunk.resize(got);
        out.body += chunk;
        if (out.body.size() > (16u << 20)) { truncated = true; break; }   // tope de 16 MB
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    // Un cuerpo cortado NO es exito: antes se devolvia true y el .nam incompleto
    // acababa en disco y en la cadena, con el cargador diciendo que no cuadran
    // los pesos en vez de que la descarga fallo.
    if (truncated)
    {
        if (err) *err = "descarga incompleta (cortada o mayor de 16 MB)";
        return false;
    }
    return true;
}

// Descarga a fichero (los .nam pesan poco, pero no se cargan en RAM dos veces)
inline bool httpDownload(const std::string& url, const std::string& token, const std::wstring& path,
                         std::string* err = nullptr)
{
    Resp r;
    if (!httpRequest(L"GET", url, "", token, r, err)) return false;
    if (!r.ok()) { if (err) *err = "el servidor devolvio " + std::to_string(r.status); return false; }
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) { if (err) *err = "no se puede escribir el fichero"; return false; }
    const size_t n = std::fwrite(r.body.data(), 1, r.body.size(), f);
    std::fclose(f);
    if (n != r.body.size()) { if (err) *err = "escritura incompleta"; return false; }
    return true;
}

inline std::string urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

inline std::string b64url(const unsigned char* d, size_t n)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string o;
    for (size_t i = 0; i < n; i += 3)
    {
        const unsigned v = (unsigned)d[i] << 16
                         | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0)
                         | (i + 2 < n ? (unsigned)d[i + 2] : 0);
        o += t[(v >> 18) & 63];
        o += t[(v >> 12) & 63];
        if (i + 1 < n) o += t[(v >> 6) & 63];
        if (i + 2 < n) o += t[v & 63];
    }
    return o;
}

// ---------------------------------------------------------------------------
//  OAuth: flujo de dispositivo (RFC 8628)
// ---------------------------------------------------------------------------
struct Device
{
    std::string deviceCode, userCode, verifyUri, verifyUriComplete;
    int interval = 5, expiresIn = 600;
};

inline bool deviceStart(const std::string& clientId, Device& out, std::string& err)
{
    const std::string body = "client_id=" + urlEncode(clientId);
    Resp r;
    if (!httpRequest(L"POST", "https://www.tone3000.com/api/v1/oauth/device_authorization", body, "", r, &err))
        return false;
    Jv j;
    if (!jParse(r.body, j)) { err = "respuesta ilegible (" + std::to_string(r.status) + ")"; return false; }
    if (!r.ok())
    {
        err = j.str("error", "error desconocido");
        const std::string d = j.str("error_description");
        if (!d.empty()) err += ": " + d;
        return false;
    }
    out.deviceCode        = j.str("device_code");
    out.userCode          = j.str("user_code");
    out.verifyUri         = j.str("verification_uri", "https://www.tone3000.com/activate");
    out.verifyUriComplete = j.str("verification_uri_complete", out.verifyUri.c_str());
    out.interval          = (int)j.num("interval", 5);
    out.expiresIn         = (int)j.num("expires_in", 600);
    if (out.deviceCode.empty()) { err = "sin device_code"; return false; }
    return true;
}

// Devuelve: 0 = sigue pendiente, 1 = token concedido (r trae el JSON), -1 = error
inline int devicePoll(const std::string& clientId, const std::string& deviceCode, Resp& r, std::string& err)
{
    const std::string body = "grant_type=" + urlEncode("urn:ietf:params:oauth:grant-type:device_code")
                           + "&device_code=" + urlEncode(deviceCode)
                           + "&client_id=" + urlEncode(clientId);
    if (!httpRequest(L"POST", "https://www.tone3000.com/api/v1/oauth/token", body, "", r, &err)) return -1;
    if (r.ok()) return 1;
    Jv j;
    if (!jParse(r.body, j)) { err = "respuesta ilegible"; return -1; }
    const std::string e = j.str("error");
    // 0 = sigue pendiente. Comprobado contra su servidor con la sonda _nmaref/
    // (_t3kprobe.exe): mientras el usuario no ha entrado con el codigo, el token
    // endpoint responde `authorization_pending` (HTTP 400) tal y como documenta,
    // y `slow_down` pide esperar mas. Cualquier otro error es fatal (access_denied,
    // expired_token...) y se propaga: NO se traga como "pendiente", o un codigo
    // invalido tendria al usuario diez minutos esperando un token que no llega.
    if (e == "authorization_pending" || e == "slow_down") return 0;
    err = e.empty() ? ("HTTP " + std::to_string(r.status)) : e;
    return -1;
}

inline bool refreshSession(const std::string& clientId, const std::string& refresh, Resp& r, std::string& err)
{
    const std::string body = "grant_type=refresh_token&refresh_token=" + urlEncode(refresh)
                           + "&client_id=" + urlEncode(clientId);
    if (!httpRequest(L"POST", "https://www.tone3000.com/api/v1/oauth/token", body, "", r, &err)) return false;
    return r.ok();
}

inline bool openBrowser(const std::string& url)
{
    const std::wstring w(url.begin(), url.end());
    // Tres intentos, del mas limpio al mas terco: ShellExecute puede devolver
    // fallo sin decir nada (el boton "no abria nada"), asi que se prueba tambien
    // el manejador de protocolo de Windows y, en ultimo lugar, el shell.
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32)
        return true;
    {
        const std::wstring cmd = L"url.dll,FileProtocolHandler " + w;
        if ((INT_PTR)ShellExecuteW(nullptr, L"open", L"rundll32.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL) > 32)
            return true;
    }
    {
        const std::wstring cmd = L"/c start \"\" \"" + w + L"\"";
        if ((INT_PTR)ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE) > 32)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  API
// ---------------------------------------------------------------------------
inline std::string searchUrl(const std::string& query, const std::string& gears, const std::string& extra,
                             int page, int pageSize)
{
    std::string u = "https://www.tone3000.com/api/v1/tones/search?page=" + std::to_string(page)
                  + "&page_size=" + std::to_string(pageSize);
    if (!query.empty()) u += "&query=" + urlEncode(query);
    if (!gears.empty()) u += "&gears=" + urlEncode(gears);
    if (!extra.empty()) u += "&" + extra;
    return u;
}

inline bool searchTones(const std::string& token, const std::string& query, const std::string& gears,
                        const std::string& extra, int page, Resp& r, std::string& err)
{
    return httpRequest(L"GET", searchUrl(query, gears, extra, page, 24), "", token, r, &err);
}

inline bool listModels(const std::string& token, long long toneId, const std::string& extra, Resp& r, std::string& err)
{
    std::string u = "https://www.tone3000.com/api/v1/models?tone_id=" + std::to_string(toneId)
                  + "&page_size=100";
    if (!extra.empty()) u += "&" + extra;
    return httpRequest(L"GET", u, "", token, r, &err);
}

// ---------------------------------------------------------------------------
//  Presets LOCALES (.t3kpreset) — la biblioteca que el usuario ya tiene
// ---------------------------------------------------------------------------
//  Formato: magic "T3KB" + un arbol de JUCE (`ValueTree::writeToStream`). Este
//  lector es un PORTADO del de la propia herramienta de TONE3000 (que ya maneja
//  esos ficheros), respetando su semantica, comprobada en ficheros reales:
//    nodo = tipo (UTF-8 terminado en NUL) + nProps + props + nHijos + hijos
//    cint = byte de tamano (bit 7 = signo) + n bytes, LITTLE-endian
//    var  = cint(tamano incluyendo la marca) + marca + carga LITTLE-endian
//           (1 int32, 2 true, 3 false, 4 double, 5 cadena, 6 int64, 8 binario)
//  Cada bloque trae su `toneJson` (titulo + lista de modelos) y su
//  `activeModelId`: de ahi salen el nombre y la URL de descarga. Los blobs
//  embebidos (ModelCache, ~400 KB) se SALTAN sin copiarlos.
struct PresetBlock {
    std::string name;      // titulo del tono
    std::string model;     // nombre del modelo activo
    std::string url;       // model_url de descarga
    bool nam = true;       // false = IR (.wav)
};
struct LocalPreset {
    std::string file;      // nombre del fichero en disco
    std::string name;      // nombre del preset
    bool stereo = false;
    std::vector<PresetBlock> left, right;
    bool ok = false;
    std::string err;
};

class PresetTree {                     // nodo ligero: solo lo que se pide
public:
    std::string type;
    std::map<std::string, std::string> props;
    std::vector<PresetTree> kids;
    const PresetTree* child(const char* t) const {
        for (const PresetTree& k : kids) if (k.type == t) return &k;
        return nullptr;
    }
    std::string prop(const char* k) const {
        auto it = props.find(k);
        return it == props.end() ? std::string() : it->second;
    }
};

class PresetReader {
public:
    // Los bytes se guardan POR VALOR: antes era una referencia y el
    // `bytes.substr(4)` del llamante moria al acabar la construccion, asi que el
    // lector recorria basura y se salia del fichero ("fin de fichero" en los 290).
    explicit PresetReader(std::string b) : _b(std::move(b)) {}
    const std::string& error() const { return _err; }

    bool readTree(PresetTree& out, const std::vector<std::string>& want) {
        out.type = jstr();
        const int np = cint();
        for (int k = 0; k < np && _err.empty(); ++k) {
            const std::string key = jstr();
            if (std::find(want.begin(), want.end(), key) != want.end()) out.props[key] = varStr();
            else skipVar();
        }
        const int nc = cint();
        for (int k = 0; k < nc && _err.empty(); ++k) {
            PresetTree kid;
            readTree(kid, want);
            out.kids.push_back(kid);
        }
        return _err.empty();
    }
    long long asInt(const std::string& p) const {   // 4 u 8 bytes LITTLE-endian
        long long v = 0;
        for (size_t k = p.size(); k > 0; --k) v = (v << 8) | (unsigned char)p[k-1];
        return v;
    }

private:
    int byte() { if (_i >= _b.size()) { _err = "fin de fichero"; return 0; } return (unsigned char)_b[_i++]; }
    int cint() {
        const int sb = byte();
        if (sb == 0) return 0;
        const int n = sb & 0x7F;
        if (n > 4 || _i + (size_t)n > _b.size()) { _err = "entero comprimido invalido"; return 0; }
        unsigned v = 0;
        for (int k = 0; k < n; ++k) v |= (unsigned)(unsigned char)_b[_i + (size_t)k] << (8*k);
        _i += (size_t)n;
        return (sb & 0x80) ? -(int)v : (int)v;
    }
    std::string jstr() {
        const size_t z = _b.find('\0', _i);
        if (z == std::string::npos) { _err = "cadena sin terminar"; return std::string(); }
        const std::string s = _b.substr(_i, z - _i);
        _i = z + 1;
        return s;
    }
    size_t varStart() {                 // devuelve la posicion de la carga y la marca
        const int n = cint();
        if (n <= 0) return 0;
        const int m = byte();
        (void)m;
        return (_i + (size_t)(n - 1) <= _b.size()) ? (size_t)(n - 1) : 0;
    }
    void skipVar() { const size_t len = varStart(); _i += len; }
    std::string varStr() {              // solo cadenas; el resto se devuelve crudo
        const size_t len = varStart();
        if (!len) return std::string();
        std::string p = _b.substr(_i, len);
        _i += len;
        size_t e = p.size();
        while (e > 0 && p[e-1] == '\0') --e;
        return p.substr(0, e);
    }

    std::string _b;                     // en propiedad (ver constructor)
    size_t _i = 0;
    std::string _err;
};

} // namespace t3k
