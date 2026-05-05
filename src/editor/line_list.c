#include "line_list.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────
 * FUNCIONES INTERNAS
 * ───────────────────────────────────────────────────────── */

/*
 * line_create: Crea un nodo Line vacío con su GapBuffer y SpanList.
 * Retorna NULL si falla algún malloc.
 */
static Line *line_create(void) {
    Line *line = (Line *)malloc(sizeof(Line));
    if (!line) return NULL;

    line->text = gb_create();
    if (!line->text) {
        free(line);
        return NULL;
    }

    line->spans = sl_create();
    if (!line->spans) {
        gb_destroy(line->text);
        free(line);
        return NULL;
    }

    line->prev        = NULL;
    line->next        = NULL;
    line->line_number = 1;

    return line;
}

/*
 * line_destroy: Libera un nodo Line y todo su contenido.
 * NO modifica los punteros prev/next de los vecinos (eso lo hace ll).
 */
static void line_destroy(Line *line) {
    if (!line) return;
    gb_destroy(line->text);
    sl_destroy(line->spans);
    free(line);
}

/*
 * ll_renumber: Recorre la lista y actualiza los line_number.
 * Se llama después de insertar o borrar líneas.
 */
static void ll_renumber(LineList *ll) {
    size_t n = 1;
    for (Line *cur = ll->head; cur != NULL; cur = cur->next) {
        cur->line_number = n++;
    }
    ll->line_count = n - 1;
}

/* ─────────────────────────────────────────────────────────
 * CICLO DE VIDA
 * ───────────────────────────────────────────────────────── */

LineList *ll_create(void) {
    LineList *ll = (LineList *)malloc(sizeof(LineList));
    if (!ll) return NULL;

    /* Todo documento empieza con una línea en blanco */
    Line *first = line_create();
    if (!first) {
        free(ll);
        return NULL;
    }

    ll->head        = first;
    ll->tail        = first;
    ll->cursor_line = first;
    ll->line_count  = 1;

    return ll;
}

void ll_destroy(LineList *ll) {
    if (!ll) return;

    /* Recorremos la lista y liberamos cada línea */
    Line *cur = ll->head;
    while (cur) {
        Line *next = cur->next;
        line_destroy(cur);
        cur = next;
    }

    free(ll);
}

/* ─────────────────────────────────────────────────────────
 * NAVEGACIÓN
 * ───────────────────────────────────────────────────────── */

void ll_move_up(LineList *ll) {
    if (!ll->cursor_line->prev) return;  /* Ya en la primera línea */

    size_t col = gb_cursor_pos(ll->cursor_line->text);
    ll->cursor_line = ll->cursor_line->prev;

    /* Intentamos mantener la columna (si la línea es más corta, vamos al final) */
    size_t new_len = gb_length(ll->cursor_line->text);
    gb_move_to(ll->cursor_line->text, col < new_len ? col : new_len);
}

void ll_move_down(LineList *ll) {
    if (!ll->cursor_line->next) return;  /* Ya en la última línea */

    size_t col = gb_cursor_pos(ll->cursor_line->text);
    ll->cursor_line = ll->cursor_line->next;

    size_t new_len = gb_length(ll->cursor_line->text);
    gb_move_to(ll->cursor_line->text, col < new_len ? col : new_len);
}

Line *ll_get_line(const LineList *ll, size_t line_number) {
    if (line_number < 1 || line_number > ll->line_count) return NULL;

    /* Búsqueda lineal desde el head */
    Line *cur = ll->head;
    for (size_t i = 1; i < line_number && cur; i++) {
        cur = cur->next;
    }
    return cur;
}

/* ─────────────────────────────────────────────────────────
 * OPERACIONES DE EDICIÓN
 * ───────────────────────────────────────────────────────── */

int ll_insert_char(LineList *ll, char c) {
    if (c == '\n') {
        /* Enter → dividir la línea en dos */
        return ll_split_line(ll);
    }

    /* Inserción normal en el gap buffer de la línea actual */
    return gb_insert(ll->cursor_line->text, c);
}

void ll_delete_before(LineList *ll) {
    GapBuffer *gb = ll->cursor_line->text;

    if (gb_cursor_pos(gb) > 0) {
        /* Hay texto antes del cursor en esta línea → borrado normal */
        gb_delete_before(gb);
    } else if (ll->cursor_line->prev) {
        /*
         * El cursor está al inicio de la línea y hay una línea anterior.
         * Unimos esta línea con la anterior (la línea anterior "absorbe" el texto).
         */
        Line *prev = ll->cursor_line->prev;

        /* Movemos el cursor al final de la línea anterior */
        gb_move_to(prev->text, gb_length(prev->text));

        /* Ahora hacemos merge */
        ll->cursor_line = prev;
        ll_merge_with_next(ll);
    }
    /* Si cursor en inicio de la primera línea: no hacer nada */
}

void ll_delete_after(LineList *ll) {
    GapBuffer *gb  = ll->cursor_line->text;
    size_t     pos = gb_cursor_pos(gb);
    size_t     len = gb_length(gb);

    if (pos < len) {
        /* Hay texto después del cursor → borrado normal */
        gb_delete_after(gb);
    } else if (ll->cursor_line->next) {
        /* Cursor al final de la línea → unir con la siguiente */
        ll_merge_with_next(ll);
    }
}

int ll_split_line(LineList *ll) {
    Line      *cur    = ll->cursor_line;
    GapBuffer *gb     = cur->text;
    size_t     cursor = gb_cursor_pos(gb);
    size_t     len    = gb_length(gb);

    /* Tomamos el texto que queda a la DERECHA del cursor */
    size_t right_len = len - cursor;
    char  *right_text = NULL;

    if (right_len > 0) {
        right_text = (char *)malloc(right_len);
        if (!right_text) return -1;

        /* Copiamos los caracteres a la derecha del cursor */
        for (size_t i = 0; i < right_len; i++) {
            right_text[i] = gb_get_char(gb, cursor + i);
        }

        /* Borramos ese texto de la línea actual (delete_after tantas veces) */
        for (size_t i = 0; i < right_len; i++) {
            gb_delete_after(gb);
        }
    }

    /* Creamos la nueva línea */
    Line *new_line = line_create();
    if (!new_line) {
        free(right_text);
        return -1;
    }

    /* Cargamos el texto derecho en la nueva línea */
    if (right_text && right_len > 0) {
        gb_from_flat(new_line->text, right_text, right_len);
        gb_move_to(new_line->text, 0);  /* cursor al inicio de la nueva línea */
        free(right_text);
    }

    /*
     * Insertamos la nueva línea DESPUÉS de la línea actual.
     *
     * Antes: ... [cur] ↔ [cur->next] ...
     * Después: ... [cur] ↔ [new_line] ↔ [cur->next] ...
     */
    new_line->prev = cur;
    new_line->next = cur->next;

    if (cur->next) {
        cur->next->prev = new_line;
    } else {
        ll->tail = new_line;  /* new_line es la nueva cola */
    }
    cur->next = new_line;

    /* Movemos el cursor del documento a la nueva línea */
    ll->cursor_line = new_line;

    ll_renumber(ll);
    return 0;
}

int ll_merge_with_next(LineList *ll) {
    Line *cur  = ll->cursor_line;
    Line *next = cur->next;

    if (!next) return 0;  /* No hay línea siguiente, nada que hacer */

    /*
     * Unimos: tomamos todo el texto de 'next' y lo insertamos
     * al final de 'cur'.
     *
     * Antes: [cur: "Hola"] ↔ [next: " mundo"]
     * Después: [cur: "Hola mundo"] (next se elimina)
     */
    size_t  next_len;
    char   *next_text = gb_to_flat(next->text, &next_len);
    if (!next_text) return -1;

    /* Movemos el cursor de cur al final */
    gb_move_to(cur->text, gb_length(cur->text));

    /* Insertamos el texto de next en cur, carácter por carácter */
    for (size_t i = 0; i < next_len; i++) {
        if (gb_insert(cur->text, next_text[i]) != 0) {
            free(next_text);
            return -1;
        }
    }
    free(next_text);

    /* Desconectamos 'next' de la lista */
    cur->next = next->next;
    if (next->next) {
        next->next->prev = cur;
    } else {
        ll->tail = cur;
    }

    line_destroy(next);
    ll_renumber(ll);
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * FORMATO
 * ───────────────────────────────────────────────────────── */

int ll_apply_style_range(LineList *ll,
                          size_t start_line, size_t start_col,
                          size_t end_line,   size_t end_col,
                          uint8_t style_mask, uint8_t fg, uint8_t bg) {

    for (size_t ln = start_line; ln <= end_line; ln++) {
        Line *line = ll_get_line(ll, ln);
        if (!line) continue;

        size_t len  = gb_length(line->text);
        size_t col_start = (ln == start_line) ? start_col : 0;
        size_t col_end   = (ln == end_line)   ? end_col   : len;

        if (col_start >= len) continue;
        if (col_end   >  len) col_end = len;

        sl_apply_style(line->spans,
                       col_start,
                       col_end - col_start,
                       style_mask, fg, bg);
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────
 * EXPORTACIÓN
 * ───────────────────────────────────────────────────────── */

char *ll_to_flat_text(const LineList *ll, size_t *out_size) {
    /*
     * Calculamos el tamaño total primero (longitud de cada línea + '\n')
     */
    size_t total = 0;
    for (Line *cur = ll->head; cur != NULL; cur = cur->next) {
        total += gb_length(cur->text);
        if (cur->next) total += 1;  /* '\n' entre líneas */
    }

    char *flat = (char *)malloc(total + 1);  /* +1 para '\0' */
    if (!flat) {
        *out_size = 0;
        return NULL;
    }

    char *ptr = flat;
    for (Line *cur = ll->head; cur != NULL; cur = cur->next) {
        size_t  line_len;
        char   *line_text = gb_to_flat(cur->text, &line_len);
        if (!line_text) {
            free(flat);
            *out_size = 0;
            return NULL;
        }

        memcpy(ptr, line_text, line_len);
        ptr += line_len;
        free(line_text);

        if (cur->next) {
            *ptr = '\n';
            ptr++;
        }
    }

    *ptr      = '\0';
    *out_size = total;
    return flat;
}

int ll_from_flat_text(LineList *ll, const char *data, size_t len) {
    /* Limpiamos el documento actual */
    Line *cur = ll->head;
    while (cur) {
        Line *next = cur->next;
        line_destroy(cur);
        cur = next;
    }

    /* Empezamos con una línea vacía */
    Line *first = line_create();
    if (!first) return -1;

    ll->head        = first;
    ll->tail        = first;
    ll->cursor_line = first;
    ll->line_count  = 1;

    /*
     * Recorremos el texto e insertamos carácter por carácter.
     * ll_insert_char maneja '\n' dividiéndolo en líneas.
     */
    for (size_t i = 0; i < len; i++) {
        if (ll_insert_char(ll, data[i]) != 0) {
            return -1;
        }
    }

    /* Dejamos el cursor al inicio del documento */
    ll->cursor_line = ll->head;
    gb_move_to(ll->cursor_line->text, 0);

    return 0;
}

void ll_get_cursor_info(const LineList *ll,
                         size_t *out_line, size_t *out_col) {
    *out_line = ll->cursor_line ? ll->cursor_line->line_number : 1;
    *out_col  = ll->cursor_line ? gb_cursor_pos(ll->cursor_line->text) + 1 : 1;
}