#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/editor/editor_api.h"

/* Macro de aserción modificada para usar los punteros passed/failed */
#define TEST_ED(condition, name) do {                                  \
    if (condition) {                                                 \
        printf("  ✅ PASS: %s\n", name);                           \
        (*passed)++;                                                 \
    } else {                                                         \
        printf("  ❌ FAIL: %s  (línea %d)\n", name, __LINE__);     \
        (*failed)++;                                                 \
    }                                                                \
} while(0)

void test_editor_api_basic(int *passed, int *failed) {
    printf("\n[EditorAPI] Creación y operaciones básicas\n");

    EditorState *ed = editor_create();
    TEST_ED(ed != NULL, "editor_create retorna puntero válido");
    TEST_ED(ed->document != NULL, "documento interno no es nulo");
    TEST_ED(ed->modified == 0, "editor inicia sin modificaciones");

    editor_insert_char(ed, 'H');
    editor_insert_char(ed, 'o');
    editor_insert_char(ed, 'l');
    editor_insert_char(ed, 'a');

    TEST_ED(ed->modified == 1, "editor se marca como modificado tras inserción");
    
    size_t line, col;
    editor_get_cursor(ed, &line, &col);
    TEST_ED(line == 1, "cursor en línea 1");
    TEST_ED(col == 5, "cursor en columna 5");

    editor_destroy(ed);
    TEST_ED(1, "editor_destroy sin crash");
}

void test_editor_api_export_import(int *passed, int *failed) {
    printf("\n[EditorAPI] Exportación e Importación\n");

    EditorState *ed = editor_create();
    editor_insert_char(ed, 'A');
    editor_insert_char(ed, 'B');
    editor_insert_char(ed, '\n');
    editor_insert_char(ed, 'C');

    size_t text_size;
    char *text = editor_export_text(ed, &text_size);
    TEST_ED(text != NULL, "editor_export_text retorna buffer válido");
    TEST_ED(strcmp(text, "AB\nC") == 0, "texto exportado es correcto");

    editor_destroy(ed);

    /* Test de importación */
    EditorState *ed2 = editor_create();
    int res = editor_import_text(ed2, text, text_size);
    TEST_ED(res == 0, "editor_import_text retorna éxito");

    size_t new_size;
    char *new_text = editor_export_text(ed2, &new_size);
    TEST_ED(strcmp(new_text, "AB\nC") == 0, "texto importado coincide");

    free(text);
    free(new_text);
    editor_destroy(ed2);
}

void test_editor_api_movement(int *passed, int *failed) {
    printf("\n[EditorAPI] Movimientos de cursor\n");

    EditorState *ed = editor_create();
    editor_insert_char(ed, '1');
    editor_insert_char(ed, '2');
    editor_insert_char(ed, '\n');
    editor_insert_char(ed, '3');

    editor_move_up(ed);
    size_t line, col;
    editor_get_cursor(ed, &line, &col);
    TEST_ED(line == 1, "move_up mueve a la línea 1");

    editor_move_right(ed);
    editor_get_cursor(ed, &line, &col);
    TEST_ED(col == 3, "move_right mueve a la columna 3");

    editor_move_left(ed);
    editor_get_cursor(ed, &line, &col);
    TEST_ED(col == 2, "move_left mueve a la columna 2");

    editor_move_down(ed);
    editor_get_cursor(ed, &line, &col);
    TEST_ED(line == 2, "move_down mueve a la línea 2");

    editor_destroy(ed);
}

void test_editor_api_delete(int *passed, int *failed) {
    printf("\n[EditorAPI] Borrado\n");

    EditorState *ed = editor_create();
    editor_insert_char(ed, 'A');
    editor_insert_char(ed, 'B');
    editor_insert_char(ed, 'C');
    
    editor_delete_before(ed); // Borra 'C'
    
    size_t sz;
    char *text = editor_export_text(ed, &sz);
    TEST_ED(strcmp(text, "AB") == 0, "delete_before borra el carácter anterior");
    free(text);

    editor_move_left(ed); // Col 1
    editor_delete_after(ed); // Borra 'B'
    
    char *text2 = editor_export_text(ed, &sz);
    TEST_ED(strcmp(text2, "A") == 0, "delete_after borra el carácter siguiente");
    free(text2);

    editor_destroy(ed);
}

void run_editor_tests(int *passed, int *failed) {
    test_editor_api_basic(passed, failed);
    test_editor_api_export_import(passed, failed);
    test_editor_api_movement(passed, failed);
    test_editor_api_delete(passed, failed);
}
