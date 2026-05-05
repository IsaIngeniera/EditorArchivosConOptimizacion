#include "file_manager.h"
#include "compressor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define CHUNK_SIZE 4096

/* Función simple para calcular checksum */
static uint32_t calculate_checksum(const char *data, size_t len) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (uint8_t)data[i]; /* hash * 33 + c */
    }
    return hash;
}

/* Escribe datos a un file descriptor en bloques de 4KB para optimizar Syscalls */
static int write_in_chunks(int fd, const char *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        size_t to_write = size - written;
        if (to_write > CHUNK_SIZE) to_write = CHUNK_SIZE;
        ssize_t res = write(fd, data + written, to_write);
        if (res < 0) return -1;
        written += res;
    }
    return 0;
}

int file_save(EditorState *ed) {
    if (!ed || !ed->filename[0]) return -1;

    size_t text_raw_size = 0, spans_raw_size = 0;
    char *text_raw = editor_export_text(ed, &text_raw_size);
    char *spans_raw = editor_export_spans(ed, &spans_raw_size);

    size_t text_cmp_size = 0, spans_cmp_size = 0;
    char *text_cmp = compress_rle(text_raw, text_raw_size, &text_cmp_size);
    char *spans_cmp = compress_rle(spans_raw, spans_raw_size, &spans_cmp_size);

    FileHeader header;
    memcpy(header.magic, "EDITORF\0", 8);
    header.version = 1;
    header.text_compressed_size = text_cmp_size;
    header.spans_compressed_size = spans_cmp_size;
    header.text_original_size = text_raw_size;
    header.spans_original_size = spans_raw_size;
    header.checksum = calculate_checksum(text_cmp, text_cmp_size); 

    int fd = open(ed->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(text_raw); free(spans_raw); free(text_cmp); free(spans_cmp);
        return -1;
    }

    /* Escribimos header y luego payloads en chunks */
    write_in_chunks(fd, (const char *)&header, sizeof(FileHeader));
    if (text_cmp_size > 0) write_in_chunks(fd, text_cmp, text_cmp_size);
    if (spans_cmp_size > 0) write_in_chunks(fd, spans_cmp, spans_cmp_size);

    close(fd);

    free(text_raw); free(spans_raw);
    free(text_cmp); free(spans_cmp);
    
    ed->modified = 0;
    return 0;
}

int file_load(EditorState *ed, const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    struct stat sb;
    if (fstat(fd, &sb) < 0 || sb.st_size < (off_t)sizeof(FileHeader)) {
        close(fd);
        return -1;
    }

    /* Mapeamos archivo a memoria para lectura I/O óptima */
    char *mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); /* Podemos cerrar el fd después de mmap */
    if (mapped == MAP_FAILED) return -1;

    FileHeader *header = (FileHeader *)mapped;
    if (memcmp(header->magic, "EDITORF\0", 8) != 0) {
        munmap(mapped, sb.st_size);
        return -1; /* Archivo inválido o corrupto */
    }

    const char *text_cmp_ptr = mapped + sizeof(FileHeader);
    const char *spans_cmp_ptr = text_cmp_ptr + header->text_compressed_size;

    size_t text_out_size = 0, spans_out_size = 0;
    char *text_raw = decompress_rle(text_cmp_ptr, header->text_compressed_size, &text_out_size);
    char *spans_raw = decompress_rle(spans_cmp_ptr, header->spans_compressed_size, &spans_out_size);

    int ret = -1;
    if (text_raw) {
        ret = editor_import_text(ed, text_raw, text_out_size);
        if (spans_raw) {
            editor_import_spans(ed, spans_raw, spans_out_size);
        }
    }

    free(text_raw);
    free(spans_raw);
    munmap(mapped, sb.st_size);

    if (ret == 0) ed->modified = 0;
    return ret;
}