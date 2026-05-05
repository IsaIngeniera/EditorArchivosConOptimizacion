#include "editor_api.h"
#include <stdlib.h>

/* Crea el estado inicial del editor cuando abrimos el programa */
EditorState *editor_create(void) {
    EditorState *ed = (EditorState *)malloc(sizeof(EditorState));
    if (!ed) return NULL;
    
    ed->document = ll_create();
    ed->filename[0] = '\0';
    ed->modified = 0;
    
    return ed;
}

/* Limpia la memoria cuando cerramos el programa */
void editor_destroy(EditorState *ed) {
    if (!ed) return;
    ll_destroy(ed->document);
    free(ed);
}

/* Extrae todo el texto de la memoria para que Persona 2 lo guarde */
char *editor_export_text(const EditorState *ed, size_t *out_size) {
    return ll_to_flat_text(ed->document, out_size);
}

/* Extrae los formatos (negritas, colores) para que Persona 2 los guarde */
char *editor_export_spans(const EditorState *ed, size_t *out_size) {
    /* Por ahora devolvemos vacío para que Persona 2 se enfoque primero en el texto */
    (void)ed;
    *out_size = 0;
    return NULL;
}

/* Carga el texto que Persona 2 leyó desde el disco duro */
int editor_import_text(EditorState *ed, const char *data, size_t len) {
    ed->modified = 0;
    return ll_from_flat_text(ed->document, data, len);
}

/* Carga los formatos leídos desde el disco duro */
int editor_import_spans(EditorState *ed, const char *data, size_t len) {
    /* Evitamos alertas de variables sin usar */
    (void)ed; (void)data; (void)len;
    return 0;
}

/* ── Acciones del usuario en la interfaz ── */

int editor_insert_char(EditorState *ed, char c) {
    ed->modified = 1;
    return ll_insert_char(ed->document, c);
}

void editor_delete_before(EditorState *ed) {
    ed->modified = 1;
    ll_delete_before(ed->document);
}

void editor_delete_after(EditorState *ed) {
    ed->modified = 1;
    ll_delete_after(ed->document);
}

void editor_move_up(EditorState *ed) {
    ll_move_up(ed->document);
}

void editor_move_down(EditorState *ed) {
    ll_move_down(ed->document);
}

void editor_move_left(EditorState *ed) {
    GapBuffer *gb = ed->document->cursor_line->text;
    if (gb_cursor_pos(gb) > 0) {
        gb_move_left(gb);
    } else {
        /* Si estamos al inicio de la línea, subimos a la anterior */
        ll_move_up(ed->document);
        GapBuffer *prev_gb = ed->document->cursor_line->text;
        gb_move_to(prev_gb, gb_length(prev_gb));
    }
}

void editor_move_right(EditorState *ed) {
    GapBuffer *gb = ed->document->cursor_line->text;
    if (gb_cursor_pos(gb) < gb_length(gb)) {
        gb_move_right(gb);
    } else {
        /* Si estamos al final de la línea, bajamos a la siguiente */
        ll_move_down(ed->document);
        GapBuffer *next_gb = ed->document->cursor_line->text;
        gb_move_to(next_gb, 0);
    }
}

void editor_get_cursor(const EditorState *ed, size_t *line, size_t *col) {
    ll_get_cursor_info(ed->document, line, col);
}

int editor_apply_style(EditorState *ed, size_t start_line, size_t start_col, size_t end_line, size_t end_col, uint8_t style_mask, uint8_t fg, uint8_t bg) {
    ed->modified = 1;
    return ll_apply_style_range(ed->document, start_line, start_col, end_line, end_col, style_mask, fg, bg);
}