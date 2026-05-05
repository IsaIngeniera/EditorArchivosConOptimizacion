#ifndef NCURSES_UI_H
#define NCURSES_UI_H

/*
 * ============================================================
 *  NCURSES UI — Interfaz visual del editor
 * ============================================================
 *
 * ncurses es una librería de C para construir interfaces de
 * texto en terminal. Permite posicionar el cursor, usar colores,
 * capturar teclas especiales (flechas, F1, Ctrl+X, etc).
 *
 * Estructura visual del editor:
 *
 * ┌──────────────────────────────────────────┐
 * │ [ Área de edición - texto del documento ] │ ← la mayor parte
 * │                                           │
 * │                                           │
 * │                                           │
 * ├──────────────────────────────────────────┤
 * │ archivo.txt  |  Línea: 5  Col: 12        │ ← status bar
 * ├──────────────────────────────────────────┤
 * │ ^S Guardar  ^O Abrir  ^Q Salir  ^B Bold  │ ← barra de comandos
 * └──────────────────────────────────────────┘
 * ============================================================
 */

#include "../editor/editor_api.h"
#include <ncurses.h>

/* ── Pares de color predefinidos para ncurses ──────────── */

/*
 * ncurses maneja colores en "pares" (foreground + background).
 * Cada par tiene un número. Los definimos como constantes.
 * Se inicializan en ui_init() con init_pair().
 */
#define COLOR_PAIR_DEFAULT   1  /* Texto normal: blanco sobre negro      */
#define COLOR_PAIR_STATUS    2  /* Status bar: negro sobre cian           */
#define COLOR_PAIR_BOLD_TEXT 3  /* Texto en negrita: amarillo sobre negro */
#define COLOR_PAIR_ITALIC    4  /* Cursiva: verde sobre negro             */
#define COLOR_PAIR_SELECTED  5  /* Texto seleccionado: negro sobre blanco */
#define COLOR_PAIR_ERROR     6  /* Mensajes de error: blanco sobre rojo   */

/* ── Teclas especiales que manejamos ──────────────────── */

#define KEY_CTRL_S    19   /* Ctrl+S → Guardar */
#define KEY_CTRL_O    15   /* Ctrl+O → Abrir   */
#define KEY_CTRL_Q     17  /* Ctrl+Q → Salir   */
#define KEY_CTRL_B     2   /* Ctrl+B → Negrita  */
#define KEY_CTRL_I     9   /* Ctrl+I → Cursiva  */
#define KEY_CTRL_U    21   /* Ctrl+U → Subrayado */
#define KEY_CTRL_Z    26   /* Ctrl+Z → Deshacer (futuro) */

/* ── Estado de la UI ───────────────────────────────────── */

typedef struct {
    EditorState *editor;   /* El editor que estamos mostrando    */
    int          rows;     /* Filas totales del terminal          */
    int          cols;     /* Columnas totales del terminal       */
    int          scroll_y; /* Primera línea visible (scroll)      */
    int          scroll_x; /* Primera columna visible (scroll H)  */
    char         msg[256]; /* Mensaje temporal en el status bar   */
    int          msg_is_error; /* Si es error, se muestra en rojo */
    char        *clipboard; /* Buffer de copiar/pegar (futuro)    */
} UIState;

/* ── Ciclo de vida de la UI ────────────────────────────── */

/*
 * ui_init: Inicializa ncurses, los colores y la estructura UIState.
 * DEBE llamarse antes de cualquier otra función ui_*.
 * Retorna NULL si falla la inicialización.
 */
UIState *ui_init(EditorState *editor);

/*
 * ui_destroy: Apaga ncurses y libera la UIState.
 * SIEMPRE llamar esto al salir (aunque sea por error),
 * o el terminal queda en modo raw y no responde bien.
 */
void ui_destroy(UIState *ui);

/* ── Loop principal ────────────────────────────────────── */

/*
 * ui_run: Inicia el loop de eventos del editor.
 * Bloquea hasta que el usuario presiona Ctrl+Q (salir).
 * Dentro de este loop: dibuja la pantalla, espera input, procesa.
 *
 * Retorna 0 si el usuario salió normalmente.
 * Retorna 1 si el usuario eligió NO guardar cambios pendientes.
 */
int ui_run(UIState *ui);

/* ── Funciones de dibujo ───────────────────────────────── */

/*
 * ui_render: Redibuja toda la pantalla.
 * Se llama después de cada evento de teclado.
 */
void ui_render(UIState *ui);

/*
 * ui_render_text_area: Dibuja el área de edición con el texto
 * y los colores/estilos de cada span.
 */
void ui_render_text_area(UIState *ui);

/*
 * ui_render_status_bar: Dibuja la barra de estado inferior
 * con el nombre del archivo, línea/columna y mensaje.
 */
void ui_render_status_bar(UIState *ui);

/*
 * ui_render_command_bar: Dibuja la barra de comandos (atajos de teclado).
 */
void ui_render_command_bar(UIState *ui);

/*
 * ui_set_message: Muestra un mensaje temporal en el status bar.
 * is_error: 1 para mostrar en rojo, 0 para mensaje normal.
 */
void ui_set_message(UIState *ui, const char *msg, int is_error);

/* ── Manejo de input ───────────────────────────────────── */

/*
 * ui_handle_key: Procesa una tecla capturada por getch().
 * Retorna 1 si el editor debe cerrarse, 0 si continúa.
 */
int ui_handle_key(UIState *ui, int key);

/*
 * ui_prompt: Muestra un prompt en el status bar y lee input del usuario.
 * Ejemplo: ui_prompt(ui, "Nombre del archivo: ", buf, sizeof(buf))
 */
void ui_prompt(UIState *ui, const char *prompt, char *out_buf, size_t buf_size);

/* ── Diálogos ──────────────────────────────────────────── */

/*
 * ui_confirm: Muestra una pregunta Sí/No en el status bar.
 * Retorna 1 si el usuario presiona 's'/'S'/'y'/'Y', 0 en otro caso.
 */
int ui_confirm(UIState *ui, const char *question);

#endif /* NCURSES_UI_H */