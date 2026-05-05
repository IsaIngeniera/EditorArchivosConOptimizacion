#include "ncurses_ui.h"
#include "../editor/editor_api.h"
#include "../io/file_manager.h"  /* Persona 2 implementa esto */

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────
 * CICLO DE VIDA
 * ───────────────────────────────────────────────────────── */

UIState *ui_init(EditorState *editor) {
    UIState *ui = (UIState *)malloc(sizeof(UIState));
    if (!ui) return NULL;

    ui->editor      = editor;
    ui->scroll_y    = 0;
    ui->scroll_x    = 0;
    ui->msg[0]      = '\0';
    ui->msg_is_error = 0;
    ui->clipboard   = NULL;

    /*
     * Inicializamos ncurses:
     *   initscr()  → Activa el modo ncurses, captura la pantalla
     *   cbreak()   → Input carácter por carácter (sin esperar Enter)
     *   noecho()   → No mostrar lo que se escribe (nosotros controlamos el echo)
     *   keypad()   → Activar teclas especiales (flechas, F1, etc)
     *   curs_set() → Cursor visible
     */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    /* Verificamos soporte de colores */
    if (!has_colors()) {
        ui_destroy(ui);
        fprintf(stderr, "Error: el terminal no soporta colores\n");
        return NULL;
    }

    start_color();
    use_default_colors();  /* Usar colores del terminal por defecto */

    /*
     * Definimos los pares de colores.
     * init_pair(número, foreground, background)
     * -1 = color por defecto del terminal
     */
    init_pair(COLOR_PAIR_DEFAULT,   COLOR_WHITE,   -1);
    init_pair(COLOR_PAIR_STATUS,    COLOR_WHITE,   COLOR_BLUE);
    init_pair(COLOR_PAIR_BOLD_TEXT, COLOR_YELLOW,  -1);
    init_pair(COLOR_PAIR_ITALIC,    COLOR_GREEN,   -1);
    init_pair(COLOR_PAIR_SELECTED,  COLOR_BLACK,   COLOR_WHITE);
    init_pair(COLOR_PAIR_ERROR,     COLOR_WHITE,   COLOR_RED);
    init_pair(10, COLOR_GREEN, -1); /* Bordes y título */

    /* Obtenemos las dimensiones del terminal */
    getmaxyx(stdscr, ui->rows, ui->cols);

    return ui;
}

void ui_destroy(UIState *ui) {
    if (!ui) return;

    /* CRÍTICO: endwin() restaura el terminal a su estado normal */
    endwin();

    if (ui->clipboard) {
        free(ui->clipboard);
        ui->clipboard = NULL;
    }

    free(ui);
}

/* ─────────────────────────────────────────────────────────
 * LOOP PRINCIPAL
 * ───────────────────────────────────────────────────────── */

int ui_run(UIState *ui) {
    /*
     * Loop principal del editor:
     * 1. Renderizar la pantalla
     * 2. Esperar una tecla
     * 3. Procesar la tecla
     * 4. Repetir hasta que el usuario salga
     */
    while (1) {
        /* Actualizamos las dimensiones (el usuario puede redimensionar) */
        getmaxyx(stdscr, ui->rows, ui->cols);

        /* Dibujamos todo */
        ui_render(ui);

        /* Esperamos input del usuario */
        int key = getch();

        /* Procesamos la tecla */
        if (ui_handle_key(ui, key)) {
            break;  /* El usuario quiere salir */
        }
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────
 * FUNCIONES DE DIBUJO
 * ───────────────────────────────────────────────────────── */

void ui_render_borders(UIState *ui) {
    attron(COLOR_PAIR(10) | A_BOLD);
    /* Top title bar */
    move(0, 0);
    hline(ACS_HLINE, ui->cols);
    mvprintw(0, (ui->cols - 34) / 2, " Editor de Texto Pro - Proyecto 3 ");
    
    /* Bottom border above status bar */
    move(ui->rows - 3, 0);
    hline(ACS_HLINE, ui->cols);

    /* Left and right borders */
    for (int i = 1; i < ui->rows - 3; i++) {
        mvaddch(i, 0, ACS_VLINE);
        mvaddch(i, ui->cols - 1, ACS_VLINE);
    }

    /* Corners */
    mvaddch(0, 0, ACS_ULCORNER);
    mvaddch(0, ui->cols - 1, ACS_URCORNER);
    mvaddch(ui->rows - 3, 0, ACS_LLCORNER);
    mvaddch(ui->rows - 3, ui->cols - 1, ACS_LRCORNER);

    attroff(COLOR_PAIR(10) | A_BOLD);
}

void ui_render(UIState *ui) {
    clear();  /* Limpiamos la pantalla */

    ui_render_borders(ui);
    ui_render_text_area(ui);
    ui_render_status_bar(ui);
    ui_render_command_bar(ui);

    /*
     * Posicionamos el cursor físico del terminal en la posición
     * correcta del editor (para que ncurses lo muestre ahí).
     */
    size_t cur_line, cur_col;
    editor_get_cursor(ui->editor, &cur_line, &cur_col);

    int screen_row = (int)(cur_line - 1) - ui->scroll_y + 1;
    int screen_col = (int)(cur_col  - 1) - ui->scroll_x + 1;

    /* Mantenemos el cursor dentro del área visible */
    if (screen_row > 0 && screen_row < ui->rows - 3 &&
        screen_col > 0 && screen_col < ui->cols - 1) {
        move(screen_row, screen_col);
    }

    refresh();  /* Volcamos el buffer de ncurses a la pantalla real */
}

void ui_render_text_area(UIState *ui) {
    /*
     * El área de texto ocupa desde la fila 1 hasta rows-4.
     */
    int text_rows = ui->rows - 4;
    int text_cols = ui->cols - 2;
    LineList *doc = ui->editor->document;

    /* Empezamos desde la línea de scroll */
    Line *cur_line = ll_get_line(doc, (size_t)(ui->scroll_y + 1));

    /* Mensaje de bienvenida si el documento está vacío */
    if (doc->line_count == 1 && gb_length(cur_line->text) == 0) {
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT) | A_DIM);
        mvprintw(ui->rows / 2, (ui->cols - 27) / 2, "Comienza a escribir aquí...");
        attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT) | A_DIM);
        return;
    }

    for (int row = 0; row < text_rows && cur_line != NULL; row++) {
        move(row + 1, 1);

        /* Obtenemos el texto de esta línea */
        size_t  len;
        char   *text = gb_to_flat(cur_line->text, &len);
        if (!text) { cur_line = cur_line->next; continue; }

        int chars_printed = 0;
        for (size_t col = ui->scroll_x;
             col < len && (int)(col - ui->scroll_x) < text_cols;
             col++) {

            const TextSpan *span = sl_find_span(cur_line->spans, col);

            /* Determinamos los atributos ncurses para este carácter */
            int attrs = 0;
            int color_pair = COLOR_PAIR_DEFAULT;

            if (span) {
                if (span->style_mask & STYLE_BOLD)       attrs |= A_BOLD;
                if (span->style_mask & STYLE_ITALIC)     attrs |= A_ITALIC;
                if (span->style_mask & STYLE_UNDERLINE)  attrs |= A_UNDERLINE;
                if (span->style_mask & STYLE_STRIKETHROUGH) attrs |= A_ALTCHARSET;

                if (span->style_mask & STYLE_BOLD)   color_pair = COLOR_PAIR_BOLD_TEXT;
                if (span->style_mask & STYLE_ITALIC)  color_pair = COLOR_PAIR_ITALIC;
            }

            /* Activamos los atributos y dibujamos el carácter */
            attron(COLOR_PAIR(color_pair) | attrs);
            addch((unsigned char)text[col]);
            attroff(COLOR_PAIR(color_pair) | attrs);
            chars_printed++;
        }

        /* Rellenamos con espacios hasta el borde derecho */
        while (chars_printed < text_cols) {
            addch(' ');
            chars_printed++;
        }

        free(text);
        cur_line = cur_line->next;
    }
}

void ui_render_status_bar(UIState *ui) {
    /*
     * El status bar está en la penúltima fila.
     * Mostramos: nombre del archivo | línea:col | mensaje
     */
    int status_row = ui->rows - 2;
    size_t cur_line, cur_col;
    editor_get_cursor(ui->editor, &cur_line, &cur_col);

    /* Construcción del string del status bar */
    char status[512];
    const char *filename = ui->editor->filename[0]
                           ? ui->editor->filename
                           : "[Sin nombre]";
    const char *modified = ui->editor->modified ? " [modificado]" : "";

    snprintf(status, sizeof(status),
             " %s%s  |  Ln: %zu  Col: %zu  |  Líneas: %zu ",
             filename, modified,
             cur_line, cur_col,
             ui->editor->document->line_count);

    /* Si hay un mensaje, lo agregamos */
    if (ui->msg[0]) {
        size_t used = strlen(status);
        snprintf(status + used, sizeof(status) - used, " | %s", ui->msg);
    }

    /* Dibujamos con color de status bar */
    move(status_row, 0);
    attron(COLOR_PAIR(COLOR_PAIR_STATUS) | A_BOLD);
    printw("%-*s", ui->cols, status);  /* Rellenamos hasta el ancho del terminal */
    attroff(COLOR_PAIR(COLOR_PAIR_STATUS) | A_BOLD);
}

void ui_render_command_bar(UIState *ui) {
    /*
     * La última fila muestra los atajos de teclado disponibles.
     */
    int cmd_row = ui->rows - 1;
    move(cmd_row, 0);

    /* Array de comandos a mostrar */
    const char *cmds[] = {
        "^W Guardar",
        "^O Abrir",
        "^X Salir",
        "^B Negrita",
        "^I Cursiva",
        "^U Subray.",
        NULL
    };

    attron(A_REVERSE);  /* Atributo "invertido" para el fondo de los comandos */
    for (int i = 0; cmds[i] != NULL; i++) {
        if (i > 0) printw("  ");  /* Separador */
        printw("%s", cmds[i]);
    }
    attroff(A_REVERSE);

    clrtoeol();  /* Limpiamos el resto de la fila */
}

void ui_set_message(UIState *ui, const char *msg, int is_error) {
    strncpy(ui->msg, msg, sizeof(ui->msg) - 1);
    ui->msg[sizeof(ui->msg) - 1] = '\0';
    ui->msg_is_error = is_error;
}

/* ─────────────────────────────────────────────────────────
 * MANEJO DE INPUT
 * ───────────────────────────────────────────────────────── */

int ui_handle_key(UIState *ui, int key) {
    /* Limpiamos el mensaje anterior al procesar una nueva tecla */
    ui->msg[0] = '\0';

    switch (key) {

        /* ── Movimiento del cursor ── */
        case KEY_UP:    editor_move_up(ui->editor);    break;
        case KEY_DOWN:  editor_move_down(ui->editor);  break;
        case KEY_LEFT:  editor_move_left(ui->editor);  break;
        case KEY_RIGHT: editor_move_right(ui->editor); break;

        case KEY_HOME:
            gb_move_to(ui->editor->document->cursor_line->text, 0);
            break;

        case KEY_END: {
            GapBuffer *gb = ui->editor->document->cursor_line->text;
            gb_move_to(gb, gb_length(gb));
            break;
        }

        /* ── Edición básica ── */
        case KEY_BACKSPACE:
        case 127:           /* DEL en algunos terminales */
            editor_delete_before(ui->editor);
            break;

        case KEY_DC:        /* Tecla Delete */
            editor_delete_after(ui->editor);
            break;

        /* ── Guardar: Ctrl+W o Ctrl+S ── */
        case KEY_CTRL_S:
        case 23: /* KEY_CTRL_W (23) */ {
            /*
             * Si no hay nombre de archivo, pedimos uno.
             * Luego llamamos a file_manager (Persona 2) para guardar.
             */
            if (ui->editor->filename[0] == '\0') {
                ui_prompt(ui, "Guardar como: ",
                          ui->editor->filename,
                          sizeof(ui->editor->filename));
            }

            if (ui->editor->filename[0] != '\0') {
                /*
                 * file_save es la función de Persona 2.
                 * Nosotros le pasamos el EditorState y ella se encarga
                 * de exportar, comprimir y escribir al disco.
                 */
                if (file_save(ui->editor) == 0) {
                    ui_set_message(ui, "Archivo guardado correctamente", 0);
                } else {
                    ui_set_message(ui, "ERROR: no se pudo guardar el archivo", 1);
                }
            }
            break;
        }

        /* ── Abrir: Ctrl+O ── */
        case KEY_CTRL_O: {
            if (ui->editor->modified) {
                if (!ui_confirm(ui, "Hay cambios sin guardar. ¿Descartar? (s/n): ")) {
                    break;
                }
            }

            char filepath[256] = {0};
            ui_prompt(ui, "Abrir archivo: ", filepath, sizeof(filepath));

            if (filepath[0] != '\0') {
                /*
                 * file_load es la función de Persona 2.
                 * Lee el disco, descomprime, y llama editor_import_text.
                 */
                if (file_load(ui->editor, filepath) == 0) {
                    snprintf(ui->editor->filename, sizeof(ui->editor->filename), "%s", filepath);
                    ui_set_message(ui, "Archivo cargado correctamente", 0);
                } else {
                    ui_set_message(ui, "ERROR: no se pudo abrir el archivo", 1);
                }
            }
            break;
        }

        /* ── Formato: Ctrl+B (Negrita) ── */
        case KEY_CTRL_B: {
            size_t line, col;
            editor_get_cursor(ui->editor, &line, &col);
            /* Aplicamos negrita al carácter anterior (selección simple) */
            size_t start_col = (col > 0) ? col - 1 : 0;
            editor_apply_style(ui->editor,
                               line, start_col,
                               line, col,
                               STYLE_BOLD,
                               COLOR_IDX_DEFAULT, COLOR_IDX_DEFAULT);
            ui_set_message(ui, "Negrita aplicada", 0);
            break;
        }

        /* ── Formato: Ctrl+I (Cursiva) ── */
        case KEY_CTRL_I: {
            size_t line, col;
            editor_get_cursor(ui->editor, &line, &col);
            size_t start_col = (col > 0) ? col - 1 : 0;
            editor_apply_style(ui->editor,
                               line, start_col,
                               line, col,
                               STYLE_ITALIC,
                               COLOR_IDX_DEFAULT, COLOR_IDX_DEFAULT);
            ui_set_message(ui, "Cursiva aplicada", 0);
            break;
        }

        /* ── Formato: Ctrl+U (Subrayado) ── */
        case 21: /* KEY_CTRL_U (21) */ {
            size_t line, col;
            editor_get_cursor(ui->editor, &line, &col);
            size_t start_col = (col > 0) ? col - 1 : 0;
            editor_apply_style(ui->editor,
                               line, start_col,
                               line, col,
                               STYLE_UNDERLINE,
                               COLOR_IDX_DEFAULT, COLOR_IDX_DEFAULT);
            ui_set_message(ui, "Subrayado aplicado", 0);
            break;
        }

        /* ── Salir: Ctrl+X o Ctrl+Q ── */
        case KEY_CTRL_Q:
        case 24: /* KEY_CTRL_X (24) */ {
            if (ui->editor->modified) {
                if (!ui_confirm(ui, "Hay cambios sin guardar. ¿Salir de todos modos? (s/n): ")) {
                    break;
                }
            }
            return 1;  /* Señal para terminar el loop */
        }

        /* ── Caracteres normales ── */
        default:
            /*
             * Si la tecla es un carácter imprimible, lo insertamos.
             * key >= 32 filtra los caracteres de control no manejados.
             */
            if (key >= 32 && key < 256) {
                editor_insert_char(ui->editor, (char)key);
            } else if (key == '\n' || key == KEY_ENTER) {
                editor_insert_char(ui->editor, '\n');
            }
            break;
    }

    /* Ajuste de scroll: mantenemos el cursor visible */
    size_t cur_line, cur_col;
    editor_get_cursor(ui->editor, &cur_line, &cur_col);

    int text_rows = ui->rows - 4;
    int text_cols = ui->cols - 2;

    /* Scroll vertical */
    if ((int)(cur_line - 1) < ui->scroll_y) {
        ui->scroll_y = (int)(cur_line - 1);
    }
    if ((int)(cur_line - 1) >= ui->scroll_y + text_rows) {
        ui->scroll_y = (int)(cur_line - 1) - text_rows + 1;
    }

    /* Scroll horizontal */
    if ((int)(cur_col - 1) < ui->scroll_x) {
        ui->scroll_x = (int)(cur_col - 1);
    }
    if ((int)(cur_col - 1) >= ui->scroll_x + text_cols) {
        ui->scroll_x = (int)(cur_col - 1) - text_cols + 1;
    }

    return 0;
}

void ui_prompt(UIState *ui, const char *prompt, char *out_buf, size_t buf_size) {
    /*
     * Mostramos el prompt en el status bar y leemos input del usuario.
     * Activamos echo temporalmente para que el usuario vea lo que escribe.
     */
    int status_row = ui->rows - 2;
    move(status_row, 0);
    clrtoeol();

    attron(COLOR_PAIR(COLOR_PAIR_STATUS) | A_BOLD);
    printw("%s", prompt);
    attroff(COLOR_PAIR(COLOR_PAIR_STATUS) | A_BOLD);

    refresh();

    echo();     /* Mostrar caracteres mientras se escribe */
    curs_set(1);

    /* Leemos hasta 'buf_size - 1' caracteres */
    getnstr(out_buf, (int)(buf_size - 1));

    noecho();   /* Volvemos a modo sin echo */
}

int ui_confirm(UIState *ui, const char *question) {
    int status_row = ui->rows - 2;
    move(status_row, 0);
    clrtoeol();

    attron(COLOR_PAIR(COLOR_PAIR_ERROR) | A_BOLD);
    printw("%s", question);
    attroff(COLOR_PAIR(COLOR_PAIR_ERROR) | A_BOLD);

    refresh();

    int key = getch();
    return (key == 's' || key == 'S' || key == 'y' || key == 'Y');
}