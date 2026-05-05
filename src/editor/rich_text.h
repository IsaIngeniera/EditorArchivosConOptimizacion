#ifndef RICH_TEXT_H
#define RICH_TEXT_H

#include <stdint.h>   /* uint8_t, uint16_t, uint32_t */
#include <stddef.h>   /* size_t */

/*
 * ============================================================
 *  RICH TEXT — Texto enriquecido con colores y estilos
 * ============================================================
 *
 * ¿Cómo representamos el formato?
 *
 * Usamos "spans": segmentos del texto que comparten el mismo estilo.
 * Cada span tiene una posición de inicio, un largo, y los atributos
 * de formato (negrita, color, etc).
 *
 * Ejemplo: "Hola mundo cruel"
 *           ^^^^            → span 1: negrita, color rojo
 *                ^^^^^      → span 2: normal
 *                      ^^^^→ span 3: cursiva, color azul
 *
 * Los spans NO se solapan y cubren todo el texto.
 *
 * ¿Por qué __attribute__((packed))?
 * Sin packed, el compilador agrega padding entre campos para alinear
 * a 4 u 8 bytes. Eso desperdicia memoria y hace que el tamaño del
 * struct sea impredecible al serializarlo al disco.
 * Con packed, el struct ocupa EXACTAMENTE lo que suman sus campos.
 * Esto es CRÍTICO para la cabecera binaria del archivo (Persona 2).
 * ============================================================
 */

/* ── Atributos de estilo (bitmask) ─────────────────────── */

/*
 * Usamos un campo de bits para los atributos de texto.
 * Cada bit representa un atributo booleano.
 * Se pueden combinar con OR: BOLD | ITALIC = negrita+cursiva
 */
#define STYLE_NORMAL     0x00  /* Sin formato */
#define STYLE_BOLD       0x01  /* Negrita      */
#define STYLE_ITALIC     0x02  /* Cursiva      */
#define STYLE_UNDERLINE  0x04  /* Subrayado    */
#define STYLE_STRIKETHROUGH 0x08 /* Tachado   */

/* ── Colores ncurses ───────────────────────────────────── */

/*
 * Paleta de colores básica.
 * Los valores coinciden con las constantes de ncurses (COLOR_*).
 * Esto simplifica el render en ncurses_ui.c
 */
typedef enum {
    COLOR_IDX_DEFAULT = 0,  /* Color por defecto del terminal */
    COLOR_IDX_RED     = 1,
    COLOR_IDX_GREEN   = 2,
    COLOR_IDX_YELLOW  = 3,
    COLOR_IDX_BLUE    = 4,
    COLOR_IDX_MAGENTA = 5,
    COLOR_IDX_CYAN    = 6,
    COLOR_IDX_WHITE   = 7,
} ColorIndex;

/* ── Span: segmento de texto con formato ──────────────── */

/*
 * TextSpan: describe el formato de un segmento del texto.
 *
 * IMPORTANTE: __attribute__((packed)) aquí es crítico.
 * Sin él, este struct podría tener 12 bytes por padding.
 * Con él, ocupa exactamente: 8+2+1+1 = 12... veamos:
 *   - start:      8 bytes (size_t en 64-bit)
 *   - length:     8 bytes (size_t en 64-bit)
 *   - style_mask: 1 byte
 *   - fg_color:   1 byte
 *   - bg_color:   1 byte
 *   Total packed: 19 bytes exactos (sin relleno)
 *
 * Esto le permite a Persona 2 serializar estos spans
 * directamente a bytes sin ambigüedad.
 */
typedef struct {
    size_t   start;       /* Posición de inicio en el texto (0-indexed) */
    size_t   length;      /* Cantidad de caracteres que cubre este span  */
    uint8_t  style_mask;  /* Combinación de flags STYLE_* (bold, italic) */
    uint8_t  fg_color;    /* Color del texto (ColorIndex)                */
    uint8_t  bg_color;    /* Color de fondo (ColorIndex)                 */
} __attribute__((packed)) TextSpan;

/* ── SpanList: colección dinámica de spans ────────────── */

/*
 * Lista dinámica de TextSpans para una línea o el documento completo.
 * Crece automáticamente con realloc cuando se añaden spans.
 */
typedef struct {
    TextSpan *spans;    /* Array de spans en heap */
    size_t    count;    /* Cuántos spans hay actualmente */
    size_t    capacity; /* Cuántos spans caben sin realloc */
} SpanList;

/* ── Ciclo de vida de SpanList ─────────────────────────── */

/*
 * sl_create: Crea una lista de spans vacía.
 * Retorna NULL si falla la memoria.
 */
SpanList *sl_create(void);

/*
 * sl_destroy: Libera toda la memoria de la lista.
 */
void sl_destroy(SpanList *sl);

/* ── Operaciones sobre spans ────────────────────────────── */

/*
 * sl_add_span: Agrega un nuevo span al final de la lista.
 * Retorna 0 en éxito, -1 en error de memoria.
 */
int sl_add_span(SpanList *sl, size_t start, size_t length,
                uint8_t style_mask, uint8_t fg, uint8_t bg);

/*
 * sl_find_span: Encuentra el span que cubre la posición 'pos'.
 * Retorna puntero al span encontrado, o NULL si no hay span en esa pos.
 */
const TextSpan *sl_find_span(const SpanList *sl, size_t pos);

/*
 * sl_clear: Elimina todos los spans (sin liberar la lista misma).
 * Útil para resetear el formato al cargar un archivo nuevo.
 */
void sl_clear(SpanList *sl);

/*
 * sl_apply_style: Aplica un estilo a un rango del texto.
 * Si el rango cubre spans existentes, los divide/modifica según sea necesario.
 *
 * Ejemplo: texto "Hola mundo", aplicar BOLD en posiciones 5-9 ("mundo"):
 *   Antes: [ span(0,10, NORMAL) ]
 *   Después: [ span(0,5, NORMAL), span(5,5, BOLD) ]
 *
 * Retorna 0 en éxito, -1 en error.
 */
int sl_apply_style(SpanList *sl, size_t start, size_t length,
                   uint8_t style_mask, uint8_t fg, uint8_t bg);

/* ── Exportación para Persona 2 ────────────────────────── */

/*
 * sl_serialize: Convierte la lista de spans a bytes para guardar al disco.
 *
 * Formato serializado:
 *   [4 bytes: cantidad de spans (uint32_t)]
 *   [N * sizeof(TextSpan) bytes: los spans en orden]
 *
 * Retorna puntero al buffer serializado (el llamador hace free).
 * out_size se llena con el total de bytes.
 * Retorna NULL si falla.
 */
char *sl_serialize(const SpanList *sl, size_t *out_size);

/*
 * sl_deserialize: Reconstruye una SpanList desde bytes del disco.
 * Usado al abrir un archivo.
 *
 * Retorna nueva SpanList, o NULL si los datos están corruptos.
 */
SpanList *sl_deserialize(const char *data, size_t data_len);

#endif /* RICH_TEXT_H */