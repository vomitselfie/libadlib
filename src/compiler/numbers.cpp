// MSVC 4.x C runtime behaviour that ParsePlanNumber (0x489780) depends on: _mbsspnp, sscanf
// "%x", atof, atol, and _stricmp. Written out here so the result does not depend on the host
// libc (glibc's strtod would read "0x.8" as a hex float, for example).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "machine.h"

namespace adlib::cc {

namespace {
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isXDigit(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int hexVal(char c) { return isDigit(c) ? c - '0' : (c | 0x20) - 'a' + 10; }
bool isSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

// sscanf(s, "%x", &v): returns the number of fields assigned, 0 on a matching failure, or -1 (EOF)
// when the input ends right after the "0x" prefix.
int scanHex(const char *s, uint32_t &v) {
    const char *p = s;
    while (isSpace(*p)) ++p;
    if (!*p) return -1;
    bool neg = false;
    if (*p == '-') { neg = true; ++p; } else if (*p == '+') ++p;
    bool started = false;
    if (*p == '0') {
        if (p[1] == 'x' || p[1] == 'X') p += 2;
        // else: the '0' is a digit, read by the loop below
    }
    uint32_t n = 0;
    while (isXDigit(*p)) { n = (n << 4) + uint32_t(hexVal(*p)); started = true; ++p; }
    if (!started) return *p ? 0 : -1;
    v = neg ? uint32_t(0) - n : n;
    return 1;
}

// atof: [ws][sign]digits[.digits][(e|E|d|D)[sign]digits], at most 21 significant digits
// (__strgtold12 drops the rest), then rounded to float by the caller's (float) cast.
double msvcAtof(const char *s) {
    const char *p = s;
    while (isSpace(*p)) ++p;
    std::string norm;
    if (*p == '+' || *p == '-') norm.push_back(*p++);
    std::string mant;
    int dotPos = -1, sig = 0;
    bool any = false;
    for (;; ++p) {
        if (isDigit(*p)) {
            any = true;
            if (sig == 0 && *p == '0') { mant.push_back('0'); continue; }
            if (sig < 21) { mant.push_back(*p); ++sig; } else mant.push_back('0');
        } else if (*p == '.' && dotPos < 0) {
            dotPos = int(mant.size());
            mant.push_back('.');
        } else {
            break;
        }
    }
    if (!any) return 0.0;
    norm += mant;
    if (*p == 'e' || *p == 'E' || *p == 'd' || *p == 'D') {
        const char *q = p + 1;
        std::string ex = "e";
        if (*q == '+' || *q == '-') ex.push_back(*q++);
        if (isDigit(*q)) {
            while (isDigit(*q)) ex.push_back(*q++);
            norm += ex;
        }
    }
    return std::strtod(norm.c_str(), nullptr);
}
} // namespace

int32_t msvcAtol(const char *s) {
    const char *p = s;
    while (isSpace(*p)) ++p;
    bool neg = false;
    if (*p == '-') { neg = true; ++p; } else if (*p == '+') ++p;
    uint32_t n = 0;
    while (isDigit(*p)) n = n * 10u + uint32_t(*p++ - '0');
    return int32_t(neg ? uint32_t(0) - n : n);
}

int msvcStricmp(const char *a, const char *b) {
    for (;;) {
        unsigned char x = uint8_t(*a++), y = uint8_t(*b++);
        if (x >= 'A' && x <= 'Z') x = uint8_t(x + 32);
        if (y >= 'A' && y <= 'Z') y = uint8_t(y + 32);
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

// ParsePlanNumber 0x489780. Returns false when a character is outside the number set; sets
// `undefined` when the original's result is uninitialised stack memory ("0x" alone).
bool msvcParsePlanNumber(const std::string &tok, uint32_t &out, bool *undefined) {
    static const char kSet[] = "xXABCDEFabcdef0123456789.+-_";
    for (char c : tok)
        if (!c || !std::strchr(kSet, c)) return false;
    const size_t len = tok.size();
    if (len >= 1 && tok[0] == '_' && tok[len - 1] == '_') {
        // StripQuotes on the sscanf copy: drop first and last character
        std::string inner = len >= 2 ? tok.substr(1, len - 2) : std::string();
        out = uint32_t(msvcAtol(inner.c_str()));
        return true;
    }
    if (len >= 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        uint32_t v = 0;
        int r = scanHex(tok.c_str(), v);
        if (r == -1) { // "0x" alone: sscanf returns EOF and leaves local_4 uninitialised (the oracle
                       // shows stale stack bytes that differ from the game's)
            if (undefined) *undefined = true;
            out = 0;
            return true;
        }
        if (r != 0) { out = v; return true; }
    }
    if (tok.find('.') != std::string::npos) {
        float f = float(msvcAtof(tok.c_str()));
        std::memcpy(&out, &f, 4);
        return true;
    }
    out = uint32_t(msvcAtol(tok.c_str()));
    return true;
}

} // namespace adlib::cc
