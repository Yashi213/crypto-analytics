#include "utils/PdfText.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX            // keep std::min / std::max usable elsewhere
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

std::string ru(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                   static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return utf8;
    std::wstring w(static_cast<size_t>(wlen), 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), &w[0], wlen);
    int clen = WideCharToMultiByte(1251, 0, w.data(), wlen,
                                   nullptr, 0, nullptr, nullptr);
    if (clen <= 0) return utf8;
    std::string out(static_cast<size_t>(clen), 0);
    WideCharToMultiByte(1251, 0, w.data(), wlen, &out[0], clen, nullptr, nullptr);
    return out;
#else
    return utf8;
#endif
}

void pdfLoadFonts(HPDF_Doc pdf) {
    HPDF_LoadTTFontFromFile(pdf, "C:/Windows/Fonts/arial.ttf",   HPDF_TRUE);
    HPDF_LoadTTFontFromFile(pdf, "C:/Windows/Fonts/arialbd.ttf", HPDF_TRUE);
}
