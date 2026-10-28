/** @file ocr_tsv.h
 * @brief Strict parsing of the TSV Tesseract writes, independent of the process adapter.
 */
#pragma once

#include <QByteArray>
#include <QSize>

#include "ocr_engine.h"

namespace lens::capture {

/**
 * @brief Parse one Tesseract TSV answer into the unified result.
 *
 * The parser is the only place that trusts the engine's wire format, so every limit and every
 * coordinate rule is enforced here. A row that does not fit the published shape is a protocol
 * error, not a partially usable result: the caller must not translate a half-parsed page.
 *
 * @param tsv The process's stdout, with the header row still in it.
 * @param imageSize The original crop's size, in physical pixels.
 * @return `Ready` with text, lines and word boxes; `EmptyText` when no word survived; a
 *         `ProtocolError` or `ResultTooLarge` otherwise. No error result carries text.
 */
OcrResult parseTesseractTsv(QByteArray const& tsv, QSize imageSize);

}
