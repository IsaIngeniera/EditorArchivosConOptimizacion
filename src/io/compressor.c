#include "compressor.h"
#include <stdlib.h>
#include <string.h>

char *compress_rle(const char *input, size_t input_len, size_t *out_len) {
    if (!input || input_len == 0) {
        *out_len = 0;
        return NULL;
    }

    /* En el peor caso (sin caracteres repetidos), el tamaño se duplica */
    size_t max_out = input_len * 2;
    char *out_buf = (char *)malloc(max_out);
    if (!out_buf) return NULL;

    size_t out_idx = 0;
    size_t i = 0;

    while (i < input_len) {
        unsigned char count = 1;
        while (i + count < input_len && count < 255 && input[i] == input[i + count]) {
            count++;
        }
        out_buf[out_idx++] = (char)count;
        out_buf[out_idx++] = input[i];
        i += count;
    }

    *out_len = out_idx;
    /* Redimensionar para no desperdiciar memoria (opcional pero recomendado) */
    char *shrunk = (char *)realloc(out_buf, out_idx);
    return shrunk ? shrunk : out_buf;
}

char *decompress_rle(const char *input, size_t input_len, size_t *out_len) {
    if (!input || input_len == 0 || input_len % 2 != 0) {
        *out_len = 0;
        return NULL;
    }

    /* Calcular tamaño descomprimido primero */
    size_t decompressed_size = 0;
    for (size_t i = 0; i < input_len; i += 2) {
        decompressed_size += (unsigned char)input[i];
    }

    char *out_buf = (char *)malloc(decompressed_size + 1); // +1 para asegurar \0 final
    if (!out_buf) return NULL;

    size_t out_idx = 0;
    for (size_t i = 0; i < input_len; i += 2) {
        unsigned char count = (unsigned char)input[i];
        char c = input[i + 1];
        for (unsigned char j = 0; j < count; j++) {
            out_buf[out_idx++] = c;
        }
    }

    out_buf[out_idx] = '\0';
    *out_len = out_idx;
    return out_buf;
}
