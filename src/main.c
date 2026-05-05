/*
 * ============================================================
 *  MAIN — Punto de entrada del editor
 * ============================================================
 *
 * Inicializa el editor y lanza la UI.
 * Si se pasa un archivo como argumento, lo abre directamente.
 *
 * Uso:
 *   ./editor               → editor vacío
 *   ./editor archivo.txt   → abre el archivo
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>

#include "editor/editor_api.h"
#include "ui/ncurses_ui.h"
#include "io/file_manager.h"

int main(int argc, char *argv[]) {

    /* 1. Creamos el estado del editor en heap */
    EditorState *editor = editor_create();
    if (!editor) {
        fprintf(stderr, "Error fatal: no se pudo inicializar el editor\n");
        return EXIT_FAILURE;
    }

    /* 2. Si se pasó un archivo como argumento, lo cargamos */
    if (argc >= 2) {
        const char *filepath = argv[1];
        if (file_load(editor, filepath) == 0) {
            /* Guardamos el nombre del archivo en el estado del editor */
            snprintf(editor->filename, sizeof(editor->filename),
                     "%s", filepath);
        } else {
            /*
             * Si el archivo no se pudo cargar, puede ser que no exista.
             * Lo tratamos como un archivo nuevo con ese nombre.
             * (El usuario puede guardarlo con Ctrl+S)
             */
            snprintf(editor->filename, sizeof(editor->filename),
                     "%s", filepath);
            /* No es un error fatal, continuamos con documento vacío */
        }
    }

    /* 3. Inicializamos la UI de ncurses */
    UIState *ui = ui_init(editor);
    if (!ui) {
        editor_destroy(editor);
        fprintf(stderr, "Error fatal: no se pudo inicializar ncurses\n");
        return EXIT_FAILURE;
    }

    /* 4. Loop principal — bloquea hasta que el usuario sale con Ctrl+Q */
    int result = ui_run(ui);

    /* 5. Limpieza ordenada de toda la memoria */
    /* CRÍTICO: ui_destroy primero (apaga ncurses), luego editor_destroy */
    ui_destroy(ui);
    editor_destroy(editor);

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}