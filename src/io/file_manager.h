#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

/*
 * ============================================================
 *  FILE MANAGER — Interfaz de I/O al disco (PERSONA 2)
 * ============================================================
 *
 * Este archivo fue creado por Persona 1 para que Persona 2
 * sepa exactamente qué funciones debe implementar.
 *
 * Persona 2 solo necesita completar file_manager.c con la
 * lógica de compresión (Huffman) y escritura al disco.
 *
 * El formato del archivo binario en disco:
 * ┌─────────────────────────────────────────┐
 * │ Magic Number: 8 bytes "EDITORF\0"       │
 * ├─────────────────────────────────────────┤
 * │ FileHeader: struct empaquetado (packed) │
 * │  - version: uint16_t                   │
 * │  - text_compressed_size: uint64_t      │
 * │  - spans_compressed_size: uint64_t     │
 * │  - text_original_size: uint64_t        │
 * │  - spans_original_size: uint64_t       │
 * │  - checksum: uint32_t (CRC32 simple)   │
 * ├─────────────────────────────────────────┤
 * │ Payload texto comprimido (Huffman)      │
 * ├─────────────────────────────────────────┤
 * │ Payload spans comprimido (Huffman)      │
 * └─────────────────────────────────────────┘
 * ============================================================
 *
 * IMPLEMENTAR EN: src/io/file_manager.c
 */

#include "../editor/editor_api.h"
#include <stdint.h>

typedef struct __attribute__((packed)) {
    char magic[8];
    uint16_t version;
    uint64_t text_compressed_size;
    uint64_t spans_compressed_size;
    uint64_t text_original_size;
    uint64_t spans_original_size;
    uint32_t checksum;
} FileHeader;

/*
 * file_save: Guarda el documento en disco.
 *
 * Flujo esperado de implementación (Persona 2):
 *   1. editor_export_text(ed, &size)   → obtener texto raw
 *   2. editor_export_spans(ed, &size)  → obtener spans raw
 *   3. Comprimir texto con Huffman     → texto_comprimido
 *   4. Comprimir spans con Huffman     → spans_comprimido
 *   5. open() / write() el header + payloads al disco
 *   6. free() de todos los buffers intermedios
 *
 * Retorna 0 en éxito, -1 en error.
 */
int file_save(EditorState *ed);

/*
 * file_load: Carga un documento desde disco.
 *
 * Flujo esperado de implementación (Persona 2):
 *   1. open() / read() el archivo binario
 *   2. Verificar Magic Number
 *   3. Leer el FileHeader
 *   4. Descomprimir el payload de texto   → texto_raw
 *   5. Descomprimir el payload de spans   → spans_raw
 *   6. editor_import_text(ed, texto_raw)
 *   7. editor_import_spans(ed, spans_raw)
 *   8. free() de todos los buffers
 *
 * Retorna 0 en éxito, -1 en error.
 */
int file_load(EditorState *ed, const char *filepath);

#endif /* FILE_MANAGER_H */