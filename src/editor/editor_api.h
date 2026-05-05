#ifndef EDITOR_API_H
#define EDITOR_API_H

/*
 * ============================================================
 *  EDITOR API — Interfaz pública para Persona 2
 * ============================================================
 *
 * Este archivo es el CONTRATO entre Persona 1 (editor en RAM)
 * y Persona 2 (compresión + I/O al disco).
 *
 * Persona 2 solo necesita incluir este header.
 * No necesita conocer los detalles de GapBuffer ni LineList.
 *
 * Flujo de GUARDADO:
 *   1. Persona 2 llama editor_export_text() → obtiene bytes del texto
 *   2. Persona 2 llama editor_export_spans() → obtiene bytes del formato
 *   3. Persona 2 comprime ambos con Huffman
 *   4. Persona 2 escribe al disco
 *
 * Flujo de APERTURA:
 *   1. Persona 2 lee el disco y descomprime
 *   2. Persona 2 llama editor_import_text() → carga el texto al editor
 *   3. Persona 2 llama editor_import_spans() → carga el formato
 * ============================================================
 */

#include "line_list.h"
#include <stddef.h>

/* ── El estado completo del editor ─────────────────────── */

typedef struct {
    LineList *document;       /* El documento (líneas + texto) */
    char      filename[256];  /* Nombre del archivo actual     */
    int       modified;       /* 1 si hay cambios sin guardar  */
} EditorState;

/* ── Ciclo de vida ─────────────────────────────────────── */

EditorState *editor_create(void);
void         editor_destroy(EditorState *ed);

/* ── Interfaz de exportación (para Persona 2) ──────────── */

/*
 * editor_export_text:
 *   Retorna el texto completo del documento como buffer de bytes.
 *   Las líneas están separadas por '\n'.
 *   El LLAMADOR debe hacer free() sobre el retorno.
 *   out_size se llena con la cantidad de bytes.
 */
char *editor_export_text(const EditorState *ed, size_t *out_size);

/*
 * editor_export_spans:
 *   Retorna los metadatos de formato (spans) serializados.
 *   El LLAMADOR debe hacer free() sobre el retorno.
 *   out_size se llena con la cantidad de bytes.
 */
char *editor_export_spans(const EditorState *ed, size_t *out_size);

/* ── Interfaz de importación (para Persona 2) ──────────── */

/*
 * editor_import_text:
 *   Carga texto plano descomprimido al documento.
 *   Reemplaza el contenido actual.
 *   Retorna 0 en éxito, -1 en error.
 */
int editor_import_text(EditorState *ed, const char *data, size_t len);

/*
 * editor_import_spans:
 *   Carga los metadatos de formato descomprimidos.
 *   Debe llamarse DESPUÉS de editor_import_text.
 *   Retorna 0 en éxito, -1 en error.
 */
int editor_import_spans(EditorState *ed, const char *data, size_t len);

/* ── Operaciones de edición (para la UI) ───────────────── */

int  editor_insert_char(EditorState *ed, char c);
void editor_delete_before(EditorState *ed);
void editor_delete_after(EditorState *ed);
void editor_move_up(EditorState *ed);
void editor_move_down(EditorState *ed);
void editor_move_left(EditorState *ed);
void editor_move_right(EditorState *ed);
void editor_get_cursor(const EditorState *ed, size_t *line, size_t *col);
int  editor_apply_style(EditorState *ed,
                         size_t start_line, size_t start_col,
                         size_t end_line,   size_t end_col,
                         uint8_t style_mask, uint8_t fg, uint8_t bg);

#endif /* EDITOR_API_H */