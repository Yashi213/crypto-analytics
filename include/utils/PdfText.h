#pragma once
#include <string>
#include <hpdf.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Cyrillic text support for libharu PDF output.
//
//  libharu's built-in Helvetica only covers Latin-1, so Russian text would not
//  render.  We load the system Arial TrueType faces and use the single-byte
//  CP1251 encoding.  CP1251 is one byte per glyph, so std::string::size() still
//  equals the visible glyph count — which keeps the chart label-centring math
//  (which multiplies size() by a per-char width) correct.
//
//  The implementation lives in PdfText.cpp so that <windows.h> (needed for the
//  UTF-8 -> CP1251 conversion) does not leak its ERROR / min / max macros into
//  every translation unit that only needs the helpers.
// ─────────────────────────────────────────────────────────────────────────────

#define PDF_FONT_REG  "ArialMT"
#define PDF_FONT_BOLD "Arial-BoldMT,Bold"
#define PDF_ENC       "CP1251"

// Convert a UTF-8 literal (source files are UTF-8) to CP1251 bytes for libharu.
// ASCII passes through unchanged, so it is safe to wrap every string.
std::string ru(const std::string& utf8);

// Load Arial (regular + bold) into the document. Call once right after HPDF_New,
// before any text/chart is drawn.
void pdfLoadFonts(HPDF_Doc pdf);
