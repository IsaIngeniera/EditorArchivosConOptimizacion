#ifndef GAP_BUFFER_H
#define GAP_BUFFER_H

#include <stddef.h>  /* size_t */

/*
 * ============================================================
 *  GAP BUFFER — Estructura de datos para edición eficiente
 * ============================================================
 *
 * ¿Qué es un Gap Buffer?
 * Es un array de caracteres con un "hueco" (gap) en la posición
 * del cursor. Insertar/borrar en el cursor es O(1) porque solo
 * movemos el gap, no todo el texto.
 *
 * Ejemplo visual:
 *   Texto: "Hola mundo"
 *   Cursor entre "Hola" y " mundo"
 *
 *   [ H | o | l | a | _ | _ | _ | _ |   | m | u | n | d | o ]
 *    ^texto_izq^         ^gap (vacío)^   ^texto_der^
 *
 * Si el usuario escribe 'X':
 *   [ H | o | l | a | X | _ | _ | _ |   | m | u | n | d | o ]
 *   Solo se llenó un slot del gap → O(1)
 *
 * Si el usuario mueve el cursor a la izquierda:
 *   [ H | o | l | _ | _ | _ | _ | a |   | m | u | n | d | o ]
 *   Se movió 'a' al lado derecho del gap → O(1)
 * ============================================================
 */

/* Tamaño inicial del buffer en bytes */
#define GAP_BUFFER_INITIAL_SIZE 256

/* Factor de crecimiento cuando el gap se llena */
#define GAP_BUFFER_GROW_FACTOR  2

/*
 * Estructura principal del Gap Buffer.
 * Usamos __attribute__((packed)) para evitar padding del compilador
 * y tener control exacto del layout en memoria (importante para P2).
 */
typedef struct {
    char  *buffer;      /* Array de caracteres en heap (malloc) */
    size_t buf_size;    /* Tamaño total del array (texto + gap) */
    size_t gap_start;   /* Índice donde empieza el gap (= posición del cursor) */
    size_t gap_end;     /* Índice donde termina el gap (exclusivo) */
} __attribute__((packed)) GapBuffer;

/* ── Ciclo de vida ─────────────────────────────────────── */

/*
 * gb_create: Reserva memoria e inicializa el gap buffer.
 * Retorna NULL si malloc falla.
 * El gap ocupa todo el buffer al inicio (no hay texto aún).
 */
GapBuffer *gb_create(void);

/*
 * gb_destroy: Libera toda la memoria del gap buffer.
 * Siempre llamar esto antes de perder la referencia (evita memory leak).
 */
void gb_destroy(GapBuffer *gb);

/* ── Operaciones de edición (todas O(1) amortizado) ───── */

/*
 * gb_insert: Inserta un carácter en la posición actual del cursor.
 * Si el gap está lleno, crece automáticamente (realloc).
 * Retorna 0 en éxito, -1 en error de memoria.
 */
int gb_insert(GapBuffer *gb, char c);

/*
 * gb_delete_before: Borra el carácter ANTES del cursor (como Backspace).
 * No hace nada si el cursor está al inicio.
 */
void gb_delete_before(GapBuffer *gb);

/*
 * gb_delete_after: Borra el carácter DESPUÉS del cursor (como Delete).
 * No hace nada si el cursor está al final del texto.
 */
void gb_delete_after(GapBuffer *gb);

/* ── Movimiento del cursor ─────────────────────────────── */

/*
 * gb_move_left: Mueve el cursor un carácter a la izquierda.
 * Internamente, mueve el carácter izquierdo al lado derecho del gap.
 */
void gb_move_left(GapBuffer *gb);

/*
 * gb_move_right: Mueve el cursor un carácter a la derecha.
 */
void gb_move_right(GapBuffer *gb);

/*
 * gb_move_to: Mueve el cursor a una posición absoluta en el texto.
 * Posición 0 = inicio del texto.
 */
void gb_move_to(GapBuffer *gb, size_t pos);

/* ── Consultas ─────────────────────────────────────────── */

/*
 * gb_length: Retorna la cantidad de caracteres de texto (sin contar el gap).
 */
size_t gb_length(const GapBuffer *gb);

/*
 * gb_cursor_pos: Retorna la posición actual del cursor (0-indexed).
 */
size_t gb_cursor_pos(const GapBuffer *gb);

/*
 * gb_get_char: Retorna el carácter en la posición lógica 'pos'.
 * Maneja internamente el salto por encima del gap.
 * Retorna '\0' si pos >= longitud.
 */
char gb_get_char(const GapBuffer *gb, size_t pos);

/* ── Exportación (interfaz con Persona 2) ──────────────── */

/*
 * gb_to_flat: Copia el texto completo (sin gap) a un buffer contiguo.
 *
 * Uso: Persona 2 llama esto para obtener los bytes listos para comprimir.
 *
 * Parámetros:
 *   gb       → el gap buffer del que extraer
 *   out_size → [SALIDA] se llena con la cantidad de bytes copiados
 *
 * Retorna: puntero a buffer recién allocado con el texto plano.
 *          El LLAMADOR es responsable de hacer free() sobre este puntero.
 *          Retorna NULL si la memoria falla.
 *
 * Ejemplo de uso (Persona 2):
 *   size_t size;
 *   char *raw = gb_to_flat(gb, &size);
 *   // ... comprimir raw[0..size-1] ...
 *   free(raw);
 */
char *gb_to_flat(const GapBuffer *gb, size_t *out_size);

/*
 * gb_from_flat: Carga un buffer de texto plano al gap buffer.
 * Usado al ABRIR un archivo: Persona 2 descomprime y llama esto.
 *
 * Parámetros:
 *   gb   → gap buffer (debe estar recién creado con gb_create)
 *   data → bytes del texto descomprimido
 *   len  → cantidad de bytes
 *
 * Retorna: 0 en éxito, -1 en error.
 */
int gb_from_flat(GapBuffer *gb, const char *data, size_t len);

#endif /* GAP_BUFFER_H */