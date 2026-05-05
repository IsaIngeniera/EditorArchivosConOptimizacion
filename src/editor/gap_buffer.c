#include "gap_buffer.h"

#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* memmove, memcpy */
#include <stdio.h>    /* fprintf, stderr */

/* ─────────────────────────────────────────────────────────
 * FUNCIONES INTERNAS (no expuestas en el .h)
 * ───────────────────────────────────────────────────────── */

/*
 * gap_size: Cuántos slots vacíos hay en el gap actualmente.
 * Si es 0, necesitamos hacer crecer el buffer antes de insertar.
 */
static size_t gap_size(const GapBuffer *gb) {
    return gb->gap_end - gb->gap_start;
}

/*
 * gb_grow: Agranda el buffer cuando el gap se agota.
 *
 * Estrategia:
 *   1. Calculamos el nuevo tamaño (factor 2x).
 *   2. realloc: pide más memoria al SO manteniendo el contenido.
 *   3. Movemos la parte derecha del texto al final del nuevo buffer,
 *      así el gap queda en el medio con más espacio.
 *
 * Antes del grow (gap en medio, lleno):
 *   [ H | o | l | a |gap| m | u | n | d | o ]
 *                    ^^ gap_start == gap_end (tamaño 0)
 *
 * Después del grow (buffer 2x, gap ampliado):
 *   [ H | o | l | a | _ | _ | _ | _ | _ | m | u | n | d | o ]
 *                    ^^^^^^^^^gap grande^^^^^^^^^
 */
static int gb_grow(GapBuffer *gb) {
    size_t new_size    = gb->buf_size * GAP_BUFFER_GROW_FACTOR;
    size_t added_space = new_size - gb->buf_size;

    /* realloc: puede mover el bloque o extenderlo en su lugar */
    char *new_buf = (char *)realloc(gb->buffer, new_size);
    if (!new_buf) {
        fprintf(stderr, "[GapBuffer] Error: realloc falló al intentar %zu bytes\n", new_size);
        return -1;
    }

    /*
     * Movemos la parte DERECHA del texto hacia el final del nuevo buffer.
     * Esto agranda el gap en el medio.
     *
     * Usamos memmove (no memcpy) porque las regiones pueden solaparse.
     *
     * Origen:  new_buf + gap_end         (texto derecho actual)
     * Destino: new_buf + gap_end + added_space (nueva posición al final)
     * Bytes:   buf_size - gap_end
     */
    size_t right_len = gb->buf_size - gb->gap_end;
    memmove(
        new_buf + gb->gap_end + added_space,  /* destino */
        new_buf + gb->gap_end,                /* origen  */
        right_len                              /* bytes   */
    );

    gb->buffer   = new_buf;
    gb->buf_size = new_size;
    gb->gap_end  = gb->gap_start + added_space + 0;
    /* gap_end apunta justo antes del texto derecho reubicado */
    gb->gap_end  = gb->gap_end;

    /* Recalculamos gap_end correctamente */
    gb->gap_end = new_size - right_len;

    return 0;
}

/* ─────────────────────────────────────────────────────────
 * CICLO DE VIDA
 * ───────────────────────────────────────────────────────── */

GapBuffer *gb_create(void) {
    /* Allocamos la estructura en el heap */
    GapBuffer *gb = (GapBuffer *)malloc(sizeof(GapBuffer));
    if (!gb) {
        fprintf(stderr, "[GapBuffer] Error: malloc falló para la estructura\n");
        return NULL;
    }

    /* Allocamos el buffer inicial de caracteres */
    gb->buffer = (char *)malloc(GAP_BUFFER_INITIAL_SIZE);
    if (!gb->buffer) {
        fprintf(stderr, "[GapBuffer] Error: malloc falló para el buffer\n");
        free(gb);
        return NULL;
    }

    /*
     * Al inicio: el gap ocupa TODO el buffer.
     * No hay texto, el cursor está al inicio (posición 0).
     *
     * [ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ ]
     *  ^gap_start=0                      ^gap_end=256
     */
    gb->buf_size  = GAP_BUFFER_INITIAL_SIZE;
    gb->gap_start = 0;
    gb->gap_end   = GAP_BUFFER_INITIAL_SIZE;

    return gb;
}

void gb_destroy(GapBuffer *gb) {
    if (!gb) return;

    /* Primero liberamos el buffer interno */
    if (gb->buffer) {
        free(gb->buffer);
        gb->buffer = NULL;  /* Buena práctica: evitar dangling pointer */
    }

    /* Luego liberamos la estructura */
    free(gb);
}

/* ─────────────────────────────────────────────────────────
 * OPERACIONES DE EDICIÓN
 * ───────────────────────────────────────────────────────── */

int gb_insert(GapBuffer *gb, char c) {
    /* Si el gap está lleno, necesitamos más espacio */
    if (gap_size(gb) == 0) {
        if (gb_grow(gb) != 0) {
            return -1;  /* Error de memoria, no podemos insertar */
        }
    }

    /*
     * Insertar es simplemente poner el carácter en gap_start
     * y avanzar gap_start una posición.
     *
     * Antes: [ H | o | _ | _ | _ | l | a ]
     *                  ^gap_start
     * Insertar 'X':
     * Después: [ H | o | X | _ | _ | l | a ]
     *                      ^gap_start avanzó
     */
    gb->buffer[gb->gap_start] = c;
    gb->gap_start++;

    return 0;
}

void gb_delete_before(GapBuffer *gb) {
    /* No hacer nada si el cursor está al inicio (no hay texto a la izquierda) */
    if (gb->gap_start == 0) return;

    /*
     * Borrar antes = retroceder gap_start (el carácter queda "en el gap",
     * efectivamente eliminado sin mover memoria).
     *
     * Antes: [ H | o | X | _ | _ | l | a ]
     *                  ^gap_start (cursor después de X)
     * Backspace:
     * Después: [ H | o | _ | _ | _ | l | a ]
     *              ^gap_start retrocedió, X ya no es texto
     */
    gb->gap_start--;
}

void gb_delete_after(GapBuffer *gb) {
    /* No hacer nada si el cursor está al final del texto */
    if (gb->gap_end >= gb->buf_size) return;

    /*
     * Borrar después = avanzar gap_end (el carácter a la derecha
     * queda "absorbido" por el gap).
     *
     * Antes: [ H | o | _ | _ | l | a ]
     *                  ^gap_start  ^gap_end apunta a 'l'
     * Delete:
     * Después: [ H | o | _ | _ | _ | a ]
     *                  ^gap_start   ^gap_end avanzó, 'l' desapareció
     */
    gb->gap_end++;
}

/* ─────────────────────────────────────────────────────────
 * MOVIMIENTO DEL CURSOR
 * ───────────────────────────────────────────────────────── */

void gb_move_left(GapBuffer *gb) {
    if (gb->gap_start == 0) return;  /* Ya en el inicio */

    /*
     * Mover cursor izquierda = mover el carácter a la izquierda del gap
     * hacia el lado derecho del gap.
     *
     * Antes: [ H | o | l | _ | _ | a ]
     *                     ^gap_start  ^gap_end
     * Mover left (mover 'l' al lado derecho):
     * Después: [ H | o | _ | _ | l | a ]
     *              ^gap_start  ^gap_end
     */
    gb->gap_start--;
    gb->gap_end--;
    gb->buffer[gb->gap_end] = gb->buffer[gb->gap_start];
}

void gb_move_right(GapBuffer *gb) {
    if (gb->gap_end >= gb->buf_size) return;  /* Ya al final */

    /*
     * Mover cursor derecha = mover el carácter a la derecha del gap
     * hacia el lado izquierdo del gap.
     *
     * Antes: [ H | o | _ | _ | l | a ]
     *                  ^gap_start  ^gap_end apunta a 'l'
     * Mover right (mover 'l' al lado izquierdo):
     * Después: [ H | o | l | _ | _ | a ]
     *                      ^gap_start  ^gap_end
     */
    gb->buffer[gb->gap_start] = gb->buffer[gb->gap_end];
    gb->gap_start++;
    gb->gap_end++;
}

void gb_move_to(GapBuffer *gb, size_t pos) {
    size_t current = gb_cursor_pos(gb);

    /*
     * Movemos el cursor a la posición absoluta 'pos'.
     * Simplemente llamamos move_left o move_right repetidamente.
     * Para un editor real con archivos grandes, esto se optimizaría
     * con memmove directo, pero para este proyecto es suficiente.
     */
    while (current > pos) {
        gb_move_left(gb);
        current--;
    }
    while (current < pos) {
        gb_move_right(gb);
        current++;
    }
}

/* ─────────────────────────────────────────────────────────
 * CONSULTAS
 * ───────────────────────────────────────────────────────── */

size_t gb_length(const GapBuffer *gb) {
    /*
     * Longitud del texto = tamaño total del buffer MENOS el tamaño del gap.
     * El gap no contiene texto real.
     */
    return gb->buf_size - gap_size(gb);
}

size_t gb_cursor_pos(const GapBuffer *gb) {
    /*
     * El cursor está justo donde empieza el gap.
     * Todo lo que está a la IZQUIERDA del gap es texto antes del cursor.
     */
    return gb->gap_start;
}

char gb_get_char(const GapBuffer *gb, size_t pos) {
    if (pos >= gb_length(gb)) return '\0';

    /*
     * El buffer tiene el gap en el medio, así que la posición lógica
     * no coincide 1:1 con el índice físico del array.
     *
     * Si pos < gap_start → el carácter está en la parte izquierda (antes del gap)
     * Si pos >= gap_start → el carácter está en la parte derecha (saltar el gap)
     *
     * Ejemplo:
     *   buffer físico: [ H | o | _ | _ | l | a ]
     *                   0    1   2   3   4   5    ← índices físicos
     *   gap_start=2, gap_end=4
     *
     *   pos lógica 0 → índice físico 0 → 'H'
     *   pos lógica 1 → índice físico 1 → 'o'
     *   pos lógica 2 → índice físico 4 → 'l'  (saltamos el gap)
     *   pos lógica 3 → índice físico 5 → 'a'
     */
    if (pos < gb->gap_start) {
        return gb->buffer[pos];
    } else {
        return gb->buffer[gb->gap_end + (pos - gb->gap_start)];
    }
}

/* ─────────────────────────────────────────────────────────
 * EXPORTACIÓN — INTERFAZ CON PERSONA 2
 * ───────────────────────────────────────────────────────── */

char *gb_to_flat(const GapBuffer *gb, size_t *out_size) {
    size_t len = gb_length(gb);

    /* Allocamos buffer contiguo para el texto completo */
    char *flat = (char *)malloc(len + 1);  /* +1 para el '\0' terminador */
    if (!flat) {
        fprintf(stderr, "[GapBuffer] Error: malloc falló en gb_to_flat\n");
        *out_size = 0;
        return NULL;
    }

    /*
     * Copiamos en dos partes para "cerrar" el gap:
     *   Parte izquierda: buffer[0..gap_start-1]
     *   Parte derecha:   buffer[gap_end..buf_size-1]
     *
     * Resultado: texto continuo sin el gap.
     *
     * buffer físico: [ H | o | _ | _ | l | a ]
     *                                            → flat: [ H | o | l | a | \0 ]
     */
    size_t left_len  = gb->gap_start;
    size_t right_len = gb->buf_size - gb->gap_end;

    memcpy(flat,             gb->buffer,              left_len);
    memcpy(flat + left_len,  gb->buffer + gb->gap_end, right_len);

    flat[len] = '\0';  /* Terminador nulo — CRÍTICO para evitar garbage */

    *out_size = len;
    return flat;
    /* ⚠ El LLAMADOR debe hacer free(flat) cuando termine */
}

int gb_from_flat(GapBuffer *gb, const char *data, size_t len) {
    /*
     * Cargamos texto plano (ya descomprimido por Persona 2) al gap buffer.
     * Nos aseguramos de tener suficiente espacio y copiamos el texto.
     */

    /* Aseguramos que el buffer sea suficientemente grande */
    size_t needed = len + GAP_BUFFER_INITIAL_SIZE;  /* texto + gap inicial */
    if (needed > gb->buf_size) {
        char *new_buf = (char *)realloc(gb->buffer, needed);
        if (!new_buf) {
            fprintf(stderr, "[GapBuffer] Error: realloc falló en gb_from_flat\n");
            return -1;
        }
        gb->buffer   = new_buf;
        gb->buf_size = needed;
    }

    /* Copiamos el texto al inicio del buffer */
    memcpy(gb->buffer, data, len);

    /*
     * El gap queda al FINAL (después del texto cargado).
     * El cursor empieza al final del texto cargado.
     *
     * [ H | o | l | a | _ | _ | _ | _ ]
     *                   ^gap_start      ^gap_end=buf_size
     */
    gb->gap_start = len;
    gb->gap_end   = gb->buf_size;

    return 0;
}