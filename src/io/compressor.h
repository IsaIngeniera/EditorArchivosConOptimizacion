#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <stddef.h>

/* 
 * Comprime los datos de entrada usando Run-Length Encoding (RLE).
 * Asigna memoria dinámicamente para el resultado.
 * El llamador es responsable de hacer free() del buffer devuelto.
 */
char *compress_rle(const char *input, size_t input_len, size_t *out_len);

/* 
 * Descomprime los datos RLE.
 * Asigna memoria dinámicamente para el resultado.
 * El llamador es responsable de hacer free() del buffer devuelto.
 */
char *decompress_rle(const char *input, size_t input_len, size_t *out_len);

#endif
