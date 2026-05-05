/*
 * ============================================================
 *  TESTS — Gap Buffer y Editor (para Persona 3 / valgrind)
 * ============================================================
 *
 * Estos tests verifican que:
 *   1. El GapBuffer inserta, borra y mueve el cursor correctamente
 *   2. La LineList maneja múltiples líneas sin memory leaks
 *   3. La exportación gb_to_flat / ll_to_flat_text es correcta
 *   4. No hay segmentation faults en casos borde
 *
 * Correr con:
 *   make test
 *
 * Correr bajo valgrind:
 *   valgrind --leak-check=full ./test_runner
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/editor/gap_buffer.h"
#include "../src/editor/rich_text.h"
#include "../src/editor/line_list.h"
#include "../src/editor/editor_api.h"

/* Contador de tests pasados/fallados */
static int tests_passed = 0;
static int tests_failed = 0;

/* Macro de aserción con reporte */
#define TEST(condition, name) do {                                  \
    if (condition) {                                                 \
        printf("  ✅ PASS: %s\n", name);                           \
        tests_passed++;                                              \
    } else {                                                         \
        printf("  ❌ FAIL: %s  (línea %d)\n", name, __LINE__);     \
        tests_failed++;                                              \
    }                                                                \
} while(0)

/* ─────────────────────────────────────────────────────────
 * TESTS DEL GAP BUFFER
 * ───────────────────────────────────────────────────────── */

void test_gap_buffer_create_destroy(void) {
    printf("\n[GapBuffer] Creación y destrucción\n");

    GapBuffer *gb = gb_create();
    TEST(gb != NULL, "gb_create retorna puntero válido");
    TEST(gb_length(gb) == 0, "longitud inicial es 0");
    TEST(gb_cursor_pos(gb) == 0, "cursor inicial en posición 0");

    gb_destroy(gb);
    TEST(1, "gb_destroy sin crash");
}

void test_gap_buffer_insert(void) {
    printf("\n[GapBuffer] Inserción de caracteres\n");

    GapBuffer *gb = gb_create();

    gb_insert(gb, 'H');
    gb_insert(gb, 'o');
    gb_insert(gb, 'l');
    gb_insert(gb, 'a');

    TEST(gb_length(gb) == 4, "longitud es 4 después de 4 inserciones");
    TEST(gb_cursor_pos(gb) == 4, "cursor al final después de insertar");
    TEST(gb_get_char(gb, 0) == 'H', "carácter 0 es 'H'");
    TEST(gb_get_char(gb, 3) == 'a', "carácter 3 es 'a'");

    gb_destroy(gb);
}

void test_gap_buffer_delete(void) {
    printf("\n[GapBuffer] Borrado de caracteres\n");

    GapBuffer *gb = gb_create();
    gb_insert(gb, 'H');
    gb_insert(gb, 'o');
    gb_insert(gb, 'X');  /* Este lo vamos a borrar */
    gb_insert(gb, 'l');
    gb_insert(gb, 'a');

    /* Nos movemos antes de 'l' para borrar 'X' con backspace */
    gb_move_left(gb);  /* cursor antes de 'a' */
    gb_move_left(gb);  /* cursor antes de 'l' */
    gb_delete_before(gb);  /* borra 'X' */

    TEST(gb_length(gb) == 4, "longitud es 4 después de borrar 1");
    TEST(gb_get_char(gb, 2) == 'l', "carácter 2 es 'l' (X fue borrado)");

    gb_destroy(gb);
}

void test_gap_buffer_cursor_movement(void) {
    printf("\n[GapBuffer] Movimiento del cursor\n");

    GapBuffer *gb = gb_create();
    gb_insert(gb, 'A');
    gb_insert(gb, 'B');
    gb_insert(gb, 'C');

    /* Cursor está al final (posición 3) */
    TEST(gb_cursor_pos(gb) == 3, "cursor al final es 3");

    gb_move_left(gb);
    TEST(gb_cursor_pos(gb) == 2, "cursor en 2 después de move_left");

    gb_move_to(gb, 0);
    TEST(gb_cursor_pos(gb) == 0, "cursor en 0 después de move_to(0)");

    /* Insertar en medio */
    gb_insert(gb, 'X');
    TEST(gb_get_char(gb, 0) == 'X', "X insertado al inicio");
    TEST(gb_get_char(gb, 1) == 'A', "A se desplazó a posición 1");
    TEST(gb_length(gb) == 4, "longitud es 4");

    gb_destroy(gb);
}

void test_gap_buffer_export(void) {
    printf("\n[GapBuffer] Exportación a buffer plano\n");

    GapBuffer *gb = gb_create();
    const char *expected = "Hola mundo";
    for (size_t i = 0; expected[i]; i++) {
        gb_insert(gb, expected[i]);
    }

    size_t size;
    char *flat = gb_to_flat(gb, &size);

    TEST(flat != NULL, "gb_to_flat retorna buffer válido");
    TEST(size == 10, "tamaño exportado es 10");
    TEST(strcmp(flat, expected) == 0, "texto exportado coincide con el original");
    TEST(flat[size] == '\0', "terminador nulo presente");

    free(flat);
    gb_destroy(gb);
}

void test_gap_buffer_grow(void) {
    printf("\n[GapBuffer] Crecimiento automático del buffer\n");

    GapBuffer *gb = gb_create();

    /* Insertamos más de GAP_BUFFER_INITIAL_SIZE caracteres para forzar realloc */
    for (int i = 0; i < 512; i++) {
        int result = gb_insert(gb, 'A' + (i % 26));
        TEST(result == 0 || i == 0, "inserción exitosa durante crecimiento");
        (void)result;
    }

    TEST(gb_length(gb) == 512, "512 caracteres insertados correctamente");

    gb_destroy(gb);
}

/* ─────────────────────────────────────────────────────────
 * TESTS DE RICH TEXT
 * ───────────────────────────────────────────────────────── */

void test_rich_text_spans(void) {
    printf("\n[RichText] Spans de formato\n");

    SpanList *sl = sl_create();
    TEST(sl != NULL, "sl_create retorna puntero válido");
    TEST(sl->count == 0, "lista inicialmente vacía");

    sl_add_span(sl, 0, 5, STYLE_BOLD, COLOR_IDX_RED, COLOR_IDX_DEFAULT);
    TEST(sl->count == 1, "un span después de add");

    const TextSpan *found = sl_find_span(sl, 2);
    TEST(found != NULL, "span encontrado en posición 2");
    TEST(found->style_mask == STYLE_BOLD, "estilo es BOLD");
    TEST(found->fg_color == COLOR_IDX_RED, "color es rojo");

    const TextSpan *not_found = sl_find_span(sl, 10);
    TEST(not_found == NULL, "no hay span en posición 10");

    sl_destroy(sl);
}

void test_rich_text_serialization(void) {
    printf("\n[RichText] Serialización y deserialización\n");

    SpanList *original = sl_create();
    sl_add_span(original, 0, 4, STYLE_BOLD, COLOR_IDX_BLUE, COLOR_IDX_DEFAULT);
    sl_add_span(original, 5, 3, STYLE_ITALIC, COLOR_IDX_GREEN, COLOR_IDX_DEFAULT);

    size_t size;
    char *data = sl_serialize(original, &size);

    TEST(data != NULL, "serialización exitosa");
    TEST(size > 0, "tamaño serializado > 0");

    SpanList *restored = sl_deserialize(data, size);
    TEST(restored != NULL, "deserialización exitosa");
    TEST(restored->count == 2, "mismo número de spans después de deserializar");

    if (restored->count == 2) {
        TEST(restored->spans[0].style_mask == STYLE_BOLD, "span 0 sigue siendo BOLD");
        TEST(restored->spans[1].style_mask == STYLE_ITALIC, "span 1 sigue siendo ITALIC");
    }

    free(data);
    sl_destroy(original);
    sl_destroy(restored);
}

/* ─────────────────────────────────────────────────────────
 * TESTS DE LINE LIST
 * ───────────────────────────────────────────────────────── */

void test_line_list_basic(void) {
    printf("\n[LineList] Operaciones básicas\n");

    LineList *ll = ll_create();
    TEST(ll != NULL, "ll_create retorna puntero válido");
    TEST(ll->line_count == 1, "una línea al inicio");

    ll_insert_char(ll, 'H');
    ll_insert_char(ll, 'o');
    ll_insert_char(ll, 'l');
    ll_insert_char(ll, 'a');

    TEST(gb_length(ll->cursor_line->text) == 4, "4 caracteres en la línea actual");

    ll_destroy(ll);
    TEST(1, "ll_destroy sin crash");
}

void test_line_list_newline(void) {
    printf("\n[LineList] Manejo de Enter (split de líneas)\n");

    LineList *ll = ll_create();

    /* Escribimos "Hola\nmundo" */
    const char *text = "Hola\nmundo";
    for (size_t i = 0; text[i]; i++) {
        ll_insert_char(ll, text[i]);
    }

    TEST(ll->line_count == 2, "2 líneas después de un Enter");

    Line *linea1 = ll_get_line(ll, 1);
    Line *linea2 = ll_get_line(ll, 2);

    TEST(linea1 != NULL, "línea 1 existe");
    TEST(linea2 != NULL, "línea 2 existe");

    if (linea1 && linea2) {
        TEST(gb_length(linea1->text) == 4, "línea 1 tiene 4 caracteres (Hola)");
        TEST(gb_length(linea2->text) == 5, "línea 2 tiene 5 caracteres (mundo)");
    }

    ll_destroy(ll);
}

void test_line_list_export(void) {
    printf("\n[LineList] Exportación a texto plano\n");

    LineList *ll = ll_create();

    const char *text = "Linea uno\nLinea dos\nLinea tres";
    for (size_t i = 0; text[i]; i++) {
        ll_insert_char(ll, text[i]);
    }

    size_t size;
    char *flat = ll_to_flat_text(ll, &size);

    TEST(flat != NULL, "ll_to_flat_text retorna buffer válido");
    TEST(strcmp(flat, text) == 0, "texto exportado coincide con el original");

    free(flat);
    ll_destroy(ll);
}

/* ─────────────────────────────────────────────────────────
 * TESTS DE EDGE CASES (casos límite importantes para valgrind)
 * ───────────────────────────────────────────────────────── */

void test_edge_cases(void) {
    printf("\n[EdgeCases] Casos límite\n");

    /* Borrar en buffer vacío no debe crashear */
    GapBuffer *gb = gb_create();
    gb_delete_before(gb);
    gb_delete_after(gb);
    TEST(gb_length(gb) == 0, "borrar en buffer vacío no crashea");
    gb_destroy(gb);

    /* Mover más allá de los límites */
    GapBuffer *gb2 = gb_create();
    gb_insert(gb2, 'A');
    gb_move_right(gb2);  /* Ya al final, no debe crashear */
    gb_move_left(gb2);
    gb_move_left(gb2);   /* Ya al inicio, no debe crashear */
    TEST(gb_cursor_pos(gb2) == 0, "cursor en inicio después de moves extremos");
    gb_destroy(gb2);

    /* gb_to_flat de buffer vacío */
    GapBuffer *gb3 = gb_create();
    size_t size;
    char *flat = gb_to_flat(gb3, &size);
    TEST(flat != NULL, "gb_to_flat de buffer vacío no retorna NULL");
    TEST(size == 0, "tamaño es 0 para buffer vacío");
    free(flat);
    gb_destroy(gb3);
}

/* ─────────────────────────────────────────────────────────
 * MAIN DE TESTS
 * ───────────────────────────────────────────────────────── */

int main(void) {
    printf("================================================\n");
    printf("  Tests Unitarios — Editor de Texto\n");
    printf("  Proyecto SO \n");
    printf("================================================\n");

    test_gap_buffer_create_destroy();
    test_gap_buffer_insert();
    test_gap_buffer_delete();
    test_gap_buffer_cursor_movement();
    test_gap_buffer_export();
    test_gap_buffer_grow();

    test_rich_text_spans();
    test_rich_text_serialization();

    test_line_list_basic();
    test_line_list_newline();
    test_line_list_export();

    test_edge_cases();

    printf("\n================================================\n");
    printf("  Resultados: %d pasados, %d fallados\n",
           tests_passed, tests_failed);
    printf("================================================\n");

    return tests_failed == 0 ? 0 : 1;
}