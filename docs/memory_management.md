# Gestión de Memoria: Gap Buffers y Control de Heap

> **Nivel:** Arquitecto de Sistemas Operativos  
> **Herramientas:** `valgrind --leak-check=full`, `gcc -fsanitize=address`, `gdb`  
> **Invariante central:** Toda memoria asignada con `malloc`/`realloc` tiene exactamente un `free()` correspondiente.

## Tabla de Contenido

## Tabla de Contenido

- [1. ¿Por Qué No un Array Simple?](#1-por-qué-no-un-array-simple)
- [2. Gap Buffer: La Solución O(1) Amortizado](#2-gap-buffer-la-solución-o1-amortizado)
  - [2.1 Concepto Fundamental](#21-concepto-fundamental)
  - [2.2 Estructura con `__attribute__((packed))`](#22-estructura-con-__attributepacked)
- [3. Operaciones del Gap Buffer en Detalle](#3-operaciones-del-gap-buffer-en-detalle)
  - [3.1 Inserción: `gb_insert(gb, c)`](#31-inserción-gb_insertgb-c--o1-amortizado)
  - [3.2 Crecimiento: `gb_grow()`](#32-crecimiento-gb_grow--estrategia-realloc--memmove)
  - [3.3 Movimiento del Cursor: `gb_move_left()`](#33-movimiento-del-cursor-gb_move_left--o1)
  - [3.4 Exportación Plana: `gb_to_flat()`](#34-exportación-plana-gb_to_flat--interfaz-con-el-compresor)
- [4. Flujo Completo de `malloc` / `free`](#4-flujo-completo-de-malloc--free)
  - [4.1 Ciclo de Vida de un GapBuffer](#41-ciclo-de-vida-de-un-gapbuffer)
  - [4.2 Ownership explícito en `compress_rle` / `decompress_rle`](#42-ownership-explícito-en-compress_rle--decompress_rle)
  - [4.3 `mmap` / `munmap` en `file_load()`](#43-mmap--munmap-en-file_load)
- [5. Evitar Padding en Estructuras: Guía de Buenas Prácticas](#5-evitar-padding-en-estructuras-guía-de-buenas-prácticas)
  - [5.1 Regla del Compilador (Alignment)](#51-regla-del-compilador-alignment)
  - [5.2 Trade-off: `packed` vs Acceso Desalineado](#52-trade-off-packed-vs-acceso-desalineado)
- [6. Validación con Valgrind](#6-validación-con-valgrind)
  - [6.1 Comandos de Análisis](#61-comandos-de-análisis)
  - [6.2 Salida Esperada (Sin Leaks)](#62-salida-esperada-sin-leaks)
  - [6.3 Causas Comunes de Leaks](#63-causas-comunes-de-leaks-en-este-tipo-de-editor)
  - [6.4 Test de Regresión con Valgrind](#64-test-de-regresión-con-valgrind)
- [7. Análisis de Complejidad Espacial](#7-análisis-de-complejidad-espacial)
- [8. Instrumentación en Tiempo de Ejecución](#8-instrumentación-en-tiempo-de-ejecución)
---

## 1. ¿Por Qué No un Array Simple?

El enfoque naïve para un editor de texto es un array de `char` en heap. El problema aparece en la operación más frecuente del usuario: **insertar un carácter en el medio del texto**.

```
Texto: "Hola mundo" (10 chars en array)
Cursor está entre "Hola" y " mundo"
Usuario escribe 'X':

ANTES:  [ H | o | l | a |   | m | u | n | d | o ]
                         ^cursor (posición 4)

Para insertar 'X' en posición 4:
  1. memmove(buf+5, buf+4, 6 bytes)  ← mover " mundo" un slot a la derecha
  2. buf[4] = 'X'

DESPUÉS: [ H | o | l | a | X |   | m | u | n | d | o ]

Complejidad: O(n) por cada inserción — inaceptable para documentos grandes.
```

Con un documento de 100.000 caracteres, escribir en el inicio requiere mover 100.000 bytes en cada pulsación de tecla.

---

## 2. Gap Buffer: La Solución O(1) Amortizado

### 2.1 Concepto Fundamental

Un Gap Buffer mantiene el texto en un array con un **hueco (gap) posicionado en el cursor**. Las inserciones y borrados en el cursor son O(1) porque solo manipulan los límites del gap.

```
Definición de campos:
  buffer:    puntero al array en heap (malloc)
  buf_size:  tamaño total del array (texto_izq + gap + texto_der)
  gap_start: índice donde empieza el gap (= posición lógica del cursor)
  gap_end:   índice donde termina el gap (exclusivo)
```

**Visualización con texto "Hola mundo", cursor entre "Hola" y " mundo":**

```
Índices físicos:
  0   1   2   3   4   5   6   7   8   9   10  11  12  13
[ H | o | l | a | _ | _ | _ | _ |   | m | u  | n | d  | o ]
 ^────────────────^   ^──────────^   ^───────────────────────^
  texto izquierdo     gap (vacío)        texto derecho

gap_start = 4   (el gap empieza aquí)
gap_end   = 8   (el texto derecho empieza aquí)
buf_size  = 14
Longitud lógica = gap_start + (buf_size - gap_end) = 4 + 6 = 10 chars
```

### 2.2 Estructura con `__attribute__((packed))`

```c
typedef struct {
    char  *buffer;      /* puntero al heap (8 bytes en x86_64) */
    size_t buf_size;    /* 8 bytes                              */
    size_t gap_start;   /* 8 bytes                              */
    size_t gap_end;     /* 8 bytes                              */
} __attribute__((packed)) GapBuffer;
```

**Impacto del `packed` en el layout de memoria:**

```
SIN packed (gcc añade padding para alinear):
  Offset 0:  buffer    (8 bytes)
  Offset 8:  buf_size  (8 bytes)
  Offset 16: gap_start (8 bytes)
  Offset 24: gap_end   (8 bytes)
  sizeof = 32 bytes  ← sin padding en este caso particular (size_t ya está alineado)

CON packed: sizeof = 32 bytes  ← igual aquí porque size_t es 8 bytes en x86_64

CASO CRÍTICO (si tuviéramos char + uint32_t sin packed):
  struct { char c; uint32_t x; }          → sizeof = 8 (4 bytes de padding)
  struct __attribute__((packed)) { ... }  → sizeof = 5 (0 bytes de padding)
```

El uso de `__attribute__((packed))` en `GapBuffer` no tiene impacto significativo en esta estructura específica, ya que todos sus campos están naturalmente alineados en arquitecturas x86_64. Su uso es más relevante en estructuras serializadas como `FileHeader`.
---

## 3. Operaciones del Gap Buffer en Detalle

### 3.1 Inserción: `gb_insert(gb, c)` — O(1) amortizado

```
Estado inicial: cursor entre "Hola" y " mundo", gap tiene 4 slots
[ H | o | l | a | _ | _ | _ | _ |   | m | u | n | d | o ]
                 ^gap_start=4         ^gap_end=8

Usuario escribe 'X':
  out_buf[gap_start] = 'X';   // llenar el primer slot del gap
  gap_start++;                 // contraer el gap por la izquierda

Estado resultante:
[ H | o | l | a | X | _ | _ | _ |   | m | u | n | d | o ]
                     ^gap_start=5     ^gap_end=8
```

**Caso especial — gap lleno (gap_start == gap_end):** Se invoca `gb_grow()`.

### 3.2 Crecimiento: `gb_grow()` — Estrategia `realloc` + `memmove`

```c
static int gb_grow(GapBuffer *gb) {
    size_t new_size    = gb->buf_size * GAP_BUFFER_GROW_FACTOR;  /* 2x */
    size_t added_space = new_size - gb->buf_size;

    char *new_buf = (char *)realloc(gb->buffer, new_size);
    if (!new_buf) return -1;

    /* memmove: mover texto derecho al final del buffer ampliado */
    size_t right_len = gb->buf_size - gb->gap_end;
    memmove(
        new_buf + gb->gap_end + added_space,  /* destino: nuevo final */
        new_buf + gb->gap_end,                /* origen: posición actual */
        right_len
    );

    gb->buffer   = new_buf;
    gb->buf_size = new_size;
    gb->gap_end  = new_size - right_len;      /* nuevo gap_end */
    return 0;
}
```

**Visualización del grow (buffer de 8 → 16, gap lleno en posición 4):**

```
ANTES (8 bytes, gap agotado):
[ H | o | l | a | X | X | m | u ]
                 ^ gap_start==gap_end==4 (gap vacío)
  texto_izq=4    texto_der=4 (inicio en offset 4)

realloc → buffer de 16 bytes (contenido del derecho aún en posición 4):
[ H | o | l | a | X | X | m | u | ? | ? | ? | ? | ? | ? | ? | ? ]

memmove(new_buf+4+8, new_buf+4, 4):  ← mueve "XmXu" 8 posiciones a la derecha
[ H | o | l | a | _ | _ | _ | _ | _ | _ | _ | _ | X | X | m | u ]
                 ^──────────── gap=8 slots ────────^  texto_der=4

DESPUÉS:
  gap_start = 4, gap_end = 12, buf_size = 16
```

**Análisis amortizado:** El factor 2x garantiza que tras k operaciones de grow, el número total de bytes movidos por `memmove` es O(n) acumulado, haciendo que el costo amortizado por inserción sea O(1).

### 3.3 Movimiento del Cursor: `gb_move_left()` — O(1)

```c
void gb_move_left(GapBuffer *gb) {
    if (gb->gap_start == 0) return;  /* ya en el inicio */
    gb->gap_start--;
    gb->gap_end--;
    gb->buffer[gb->gap_end] = gb->buffer[gb->gap_start];
    /* el char que estaba justo antes del gap pasa al lado derecho */
}
```

```
ANTES:  [ H | o | l | a | _ | _ | m | u ]
                         ^gap_start=4    ^gap_end=6

gb_move_left():
  buffer[gap_end-1=5] = buffer[gap_start-1=3] = 'a'
  gap_start=3, gap_end=5

DESPUÉS: [ H | o | l | _ | _ | a | m | u ]
                      ^gap_start=3  ^gap_end=5
Cursor ahora entre "Hol" y "amu"
```

### 3.4 Exportación Plana: `gb_to_flat()` — Interfaz con el Compresor

```c
char *gb_to_flat(const GapBuffer *gb, size_t *out_size) {
    size_t len  = gb_length(gb);   /* gap_start + (buf_size - gap_end) */
    char  *flat = (char *)malloc(len + 1);
    if (!flat) return NULL;

    /* Copiar texto izquierdo (antes del gap) */
    memcpy(flat, gb->buffer, gb->gap_start);

    /* Copiar texto derecho (después del gap) — saltando el hueco */
    memcpy(flat + gb->gap_start,
           gb->buffer + gb->gap_end,
           gb->buf_size - gb->gap_end);

    flat[len] = '\0';
    *out_size = len;
    return flat;  /* El LLAMADOR hace free() */
}
```

Esta función es el **puente** entre la Capa 1 (edición) y la Capa 2 (compresión). Produce un buffer contiguo sin el gap, listo para pasar a `compress_rle()`.

---

## 4. Flujo Completo de `malloc` / `free`

### 4.1 Ciclo de Vida de un GapBuffer

```
gb_create()
  └── malloc(GAP_BUFFER_INITIAL_SIZE=256)   → buffer inicial
        │
        ▼ [edición en curso]
        │
  gb_grow() (si necesario)
  └── realloc(buffer, new_size)             → puede moverse en heap
        │
        ▼ [al guardar el archivo]
        │
  gb_to_flat()
  └── malloc(len + 1)                       → buffer temporal para el compresor
  │                                           El llamador hace free()
  │
  compress_rle(flat_buf, ...)
  └── malloc(input_len * 2)                 → buffer de compresión
      realloc(out_buf, out_idx)             → reducir al tamaño real
      El llamador (file_save) hace free()
        │
        ▼
  write_in_chunks(fd, compressed, ...)      → syscall, sin allocations adicionales
        │
        ▼ [al destruir el editor]
        │
gb_destroy()
  └── free(gb->buffer)                      → liberar el array principal
      free(gb)                              → liberar la struct
```

### 4.2 Ownership explícito en `compress_rle` / `decompress_rle`

La convención de ownership está documentada en `compressor.h`:

```c
/*
 * Comprime los datos de entrada usando Run-Length Encoding (RLE).
 * Asigna memoria dinámicamente para el resultado.
 * El llamador es responsable de hacer free() del buffer devuelto.
 */
char *compress_rle(const char *input, size_t input_len, size_t *out_len);
```

En `file_save()`, el patrón de limpieza es exhaustivo:

```c
char *text_raw  = editor_export_text(ed, &text_raw_size);
char *spans_raw = editor_export_spans(ed, &spans_raw_size);
char *text_cmp  = compress_rle(text_raw, text_raw_size, &text_cmp_size);
char *spans_cmp = compress_rle(spans_raw, spans_raw_size, &spans_cmp_size);

/* ... operaciones de escritura ... */

free(text_raw);   /* ← free del export */
free(spans_raw);  /* ← free del export */
free(text_cmp);   /* ← free del compresor */
free(spans_cmp);  /* ← free del compresor */
```

**Patrón de error temprano:** Si `open()` falla, todos los buffers se liberan antes del `return -1`, sin ningún path de código que pueda escapar con memoria viva.

### 4.3 `mmap` / `munmap` en `file_load()`

El flujo de carga utiliza `mmap` en lugar de `read()`, lo que elimina una copia de datos intermedia. El ciclo de vida es:

```c
char *mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
close(fd);   /* ← El fd puede cerrarse inmediatamente después de mmap */

/* ... parsing y descompresión usando punteros dentro de 'mapped' ... */

free(text_raw);    /* ← malloc de decompress_rle */
free(spans_raw);   /* ← malloc de decompress_rle */
munmap(mapped, sb.st_size);  /* ← devolver el mapeo al kernel */
```

**Nota crítica:** `close(fd)` después de `mmap()` es correcto y deliberado. El mapeo mantiene una referencia interna al inode; el descriptor de archivo ya no es necesario y cerrarlo antes evita un potencial file descriptor leak si el código de error sale en cualquier punto posterior.

---

## 5. Evitar Padding en Estructuras: Guía de Buenas Prácticas

### 5.1 Regla del Compilador (Alignment)

Por defecto, `gcc` alinea cada campo al mínimo entre su tamaño y el `max_align_t` de la plataforma:

```c
struct Sin_Packed {
    char     a;       /* offset 0, 1 byte  */
    /* 3 bytes padding → */
    uint32_t b;       /* offset 4, 4 bytes */
    char     c;       /* offset 8, 1 byte  */
    /* 3 bytes padding → */
    uint32_t d;       /* offset 12, 4 bytes */
};                    /* sizeof = 16 (desperdicia 6 bytes) */

struct __attribute__((packed)) Con_Packed {
    char     a;       /* offset 0, 1 byte  */
    uint32_t b;       /* offset 1, 4 bytes */
    char     c;       /* offset 5, 1 byte  */
    uint32_t d;       /* offset 6, 4 bytes */
};                    /* sizeof = 10 (cero desperdicio) */
```

### 5.2 Trade-off: `packed` vs Acceso Desalineado

En arquitecturas como ARM, un acceso desalineado a `uint32_t` en una struct `packed` puede generar un **bus error** (`SIGBUS`) si el campo no coincide con un múltiplo de 4 bytes. Las estrategias para evitarlo:

1. **`memcpy` para leer campos críticos:**
   ```c
   uint64_t size;
   memcpy(&size, &header->text_compressed_size, sizeof(uint64_t));
   ```

2. **Orden de campos en la struct:** Colocar los campos más grandes primero naturalmente reduce el padding necesario incluso sin `packed`.

3. **Uso de `packed` solo en estructuras serializadas:** En este proyecto, `__attribute__((packed))` se aplica únicamente a `FileHeader` (en disco) y `GapBuffer` (donde los campos `size_t` ya tienen alineación natural de 8 bytes en x86_64, por lo que `packed` no cambia el layout).

---

## 6. Validación con Valgrind

### 6.1 Comandos de Análisis

```bash
# Análisis completo de leaks
valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --verbose \
  ./editor test_document.txt

# Análisis con AddressSanitizer (más rápido, compile-time)
gcc -fsanitize=address -fsanitize=undefined -g -O0 \
    src/**/*.c -o editor_asan -lncurses
./editor_asan test_document.txt
```

### 6.2 Salida Esperada (Sin Leaks)

```
==12345== Memcheck, a memory error detector
==12345== HEAP SUMMARY:
==12345==   in use at exit: 0 bytes in 0 blocks
==12345==   total heap usage: 847 allocs, 847 frees, 1,234,567 bytes allocated
==12345==
==12345== All heap blocks were freed -- no leaks are possible
==12345==
==12345== ERROR SUMMARY: 0 errors from 0 contexts
```

El número de `allocs` y `frees` debe ser idéntico. Cada `malloc`/`realloc` registrado debe tener su `free` correspondiente.

### 6.3 Causas Comunes de Leaks en Este Tipo de Editor

| Escenario de Error | Síntoma en Valgrind | Código Correcto |
|---|---|---|
| `gb_to_flat()` sin free | `definitely lost: N bytes in 1 blocks` | `free(flat)` después de `compress_rle()` |
| `compress_rle()` sin free | `definitely lost: N bytes in 1 blocks` | `free(text_cmp)` en `file_save()` |
| `decompress_rle()` con early return | `possibly lost: N bytes` | Goto-cleanup o reestructurar el error path |
| `gb_grow()` con `realloc` error | Buffer original perdido si se asignó a nueva variable | Guardar retorno antes de reasignar `gb->buffer` |
| `mmap` sin `munmap` | Memory mapping leak (no aparece como heap) | `munmap(mapped, sb.st_size)` en todos los paths |

### 6.4 Test de Regresión con Valgrind

El Makefile incluye un target específico para ejecutar los tests bajo Valgrind:

```bash
# Ejecutar suite de tests con detección de leaks
make test
# Equivale a:
# valgrind --error-exitcode=1 --leak-check=full ./test_gap_buffer
# valgrind --error-exitcode=1 --leak-check=full ./test_editor
```

Si Valgrind retorna código de salida distinto de 0, el CI falla y el commit se rechaza. Esto garantiza un entorno libre de leaks en todo momento del desarrollo.

---

## 7. Análisis de Complejidad Espacial

| Estructura | Espacio en RAM | Overhead vs Contenido |
|---|---|---|
| `GapBuffer` (struct) | 32 bytes | Fijo, independiente del texto |
| `GapBuffer.buffer` | `buf_size` bytes | Gap ocupa en promedio ≤ 50% en steady state |
| `EditorState` | 32 + 256 + 4 bytes ≈ 292 bytes | Fijo |
| `LineList` (una línea) | `sizeof(GapBuffer) + 1 puntero` | O(L) para L líneas |
| Buffer de compresión | `2 × text_size` (peor caso) | Transitorio; liberado post-write |
| `mmap` región | `file_size` bytes | Transitorio; `munmap` post-load |

**Propiedad de localidad de caché:** El Gap Buffer concentra las escrituras recientes en la región `gap_start` del array. El hardware prefetcher de la CPU detecta el patrón de acceso secuencial y carga las líneas de caché anticipadamente, lo que resulta en una tasa de cache-miss extremadamente baja durante la edición normal (inserción continua en el cursor).

---

## 8. Instrumentación en Tiempo de Ejecución

Para observar el comportamiento de memoria en producción sin overhead de Valgrind:

```c
/* En gap_buffer.c, activar con -DDEBUG_MEMORY */
#ifdef DEBUG_MEMORY
  #define gb_malloc(sz)    ({ void *p = malloc(sz);  \
      fprintf(stderr, "[GBalloc] %p %zu\n", p, sz); p; })
  #define gb_free(p)       ({ fprintf(stderr, "[GBfree]  %p\n", p); free(p); })
  #define gb_realloc(p,sz) ({ void *q = realloc(p,sz); \
      fprintf(stderr, "[GBrealloc] %p→%p %zu\n", p, q, sz); q; })
#else
  #define gb_malloc(sz)    malloc(sz)
  #define gb_free(p)       free(p)
  #define gb_realloc(p,sz) realloc(p,sz)
#endif
```

```bash
gcc -DDEBUG_MEMORY -g src/**/*.c -o editor_debug -lncurses 2>memory_trace.log
./editor_debug test.txt
# Analizar memory_trace.log para verificar balance de alloc/free
awk '/GBalloc/{allocs++} /GBfree/{frees++} END{print "Allocs:", allocs, "Frees:", frees}' memory_trace.log
```