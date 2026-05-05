#ifndef LINE_LIST_H
#define LINE_LIST_H

#include "gap_buffer.h"
#include "rich_text.h"
#include <stddef.h>

/*
 * ============================================================
 *  LINE LIST — Lista doblemente enlazada de líneas
 * ============================================================
 *
 * ¿Por qué una lista de líneas y no un gap buffer gigante?
 *
 * En un editor con soporte de múltiples líneas y texto enriquecido,
 * cada línea puede tener su propio formato. Con una lista enlazada:
 *
 *  - Insertar/borrar una línea completa es O(1) (solo redirigir punteros)
 *  - Cada línea tiene su propio GapBuffer → O(1) para editar en esa línea
 *  - Cada línea tiene su propio SpanList → el formato está localizado
 *
 * Estructura visual:
 *
 *   NULL ← [Línea 1] ↔ [Línea 2] ↔ [Línea 3] → NULL
 *             ↓             ↓           ↓
 *          GapBuf        GapBuf      GapBuf
 *          SpanList      SpanList    SpanList
 *
 * ============================================================
 */

/* ── Nodo de la lista (una línea del documento) ────────── */

typedef struct Line {
    GapBuffer  *text;    /* El texto de esta línea */
    SpanList   *spans;   /* El formato de esta línea */

    struct Line *prev;   /* Puntero a la línea anterior */
    struct Line *next;   /* Puntero a la línea siguiente */

    size_t line_number;  /* Número de línea (1-indexed, se actualiza dinámicamente) */
} Line;

/* ── El documento completo ─────────────────────────────── */

typedef struct {
    Line   *head;         /* Primera línea del documento */
    Line   *tail;         /* Última línea del documento  */
    Line   *cursor_line;  /* Línea donde está el cursor ahora */
    size_t  line_count;   /* Total de líneas en el documento */
} LineList;

/* ── Ciclo de vida ─────────────────────────────────────── */

/*
 * ll_create: Crea un documento vacío con una línea en blanco.
 * (Todo documento tiene al menos una línea)
 */
LineList *ll_create(void);

/*
 * ll_destroy: Libera TODA la memoria del documento.
 * Libera cada Line, su GapBuffer, su SpanList, y la estructura LineList.
 */
void ll_destroy(LineList *ll);

/* ── Navegación ────────────────────────────────────────── */

/*
 * ll_move_up: Mueve el cursor a la línea anterior.
 * Intenta mantener la columna del cursor.
 */
void ll_move_up(LineList *ll);

/*
 * ll_move_down: Mueve el cursor a la línea siguiente.
 */
void ll_move_down(LineList *ll);

/*
 * ll_get_line: Retorna la línea en el número dado (1-indexed).
 * Retorna NULL si el número está fuera de rango.
 */
Line *ll_get_line(const LineList *ll, size_t line_number);

/* ── Operaciones de edición ────────────────────────────── */

/*
 * ll_insert_char: Inserta un carácter en la posición del cursor.
 * Si el carácter es '\n', divide la línea actual en dos.
 */
int ll_insert_char(LineList *ll, char c);

/*
 * ll_delete_before: Borra el carácter antes del cursor.
 * Si el cursor está al inicio de la línea, une esta línea con la anterior.
 */
void ll_delete_before(LineList *ll);

/*
 * ll_delete_after: Borra el carácter después del cursor.
 * Si el cursor está al final, une con la siguiente línea.
 */
void ll_delete_after(LineList *ll);

/*
 * ll_split_line: Divide la línea actual en el cursor.
 * El texto a la izquierda queda en la línea actual,
 * el texto a la derecha va a una nueva línea insertada después.
 */
int ll_split_line(LineList *ll);

/*
 * ll_merge_with_next: Une la línea actual con la siguiente.
 * Usado cuando se borra al final de una línea.
 */
int ll_merge_with_next(LineList *ll);

/* ── Formato de texto ──────────────────────────────────── */

/*
 * ll_apply_style_range: Aplica un estilo a un rango del documento.
 * start_line y end_line son 1-indexed.
 * start_col y end_col son posiciones en el texto de esa línea.
 */
int ll_apply_style_range(LineList *ll,
                          size_t start_line, size_t start_col,
                          size_t end_line,   size_t end_col,
                          uint8_t style_mask, uint8_t fg, uint8_t bg);

/* ── Exportación ───────────────────────────────────────── */

/*
 * ll_to_flat_text: Exporta el texto completo del documento como un
 * buffer contiguo. Las líneas se separan con '\n'.
 *
 * Persona 2 usa esto para comprimir el texto.
 * Retorna buffer allocado (caller hace free), size en out_size.
 */
char *ll_to_flat_text(const LineList *ll, size_t *out_size);

/*
 * ll_from_flat_text: Carga texto plano al documento.
 * Divide por '\n' para reconstruir la lista de líneas.
 * Persona 2 llama esto después de descomprimir.
 *
 * Retorna 0 en éxito, -1 en error.
 */
int ll_from_flat_text(LineList *ll, const char *data, size_t len);

/*
 * ll_get_cursor_info: Retorna info del cursor actual.
 * Útil para la UI (mostrar "Línea X, Col Y" en el statusbar).
 */
void ll_get_cursor_info(const LineList *ll,
                         size_t *out_line, size_t *out_col);

#endif /* LINE_LIST_H */