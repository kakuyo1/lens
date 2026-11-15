/**
 * @file ocr_stub.cpp
 * @brief The tesseract stand-in the engine tests drive: drains the image from stdin and answers
 *        with a fixed TSV, so TesseractOcr's success path is testable without the real binary,
 *        which no runner has a reason to carry.
 */
#include <cstdio>

int main()
{
    char buffer[4096];
    while (std::fread(buffer, 1, sizeof buffer, stdin) > 0) {
    }
    // The header the parser requires, then one word row with a confident reading. The box is a
    // single pixel so it fits whatever image the engine sends.
    std::fputs("level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
               "5\t1\t1\t1\t1\t1\t0\t0\t1\t1\t96\tstub\n",
               stdout);
    return 0;
}
