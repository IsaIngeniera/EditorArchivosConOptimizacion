# Arquitectura de I/O del Editor de Texto Optimizado

> **Nivel:** Wendy Vanesa Atehortua, Isabella Cadavid, Isabella Ocampo y Juan Manuel Hernández
> **Plataforma:** Linux (Kernel 5.x+), C99 nativo  
> **Componentes de análisis:** `strace`, `valgrind`, `perf`, `gcc -O2`


## Tabla de Contenido

- [1. Visión General del Sistema](#1-visión-general-del-sistema)
- [2. Contrato entre Capas: `editor_api.h`](#2-contrato-entre-capas-editor_apih)
  - [2.1 Estructura del Estado del Editor](#21-estructura-del-estado-del-editor)
  - [2.2 Pipeline de Guardado (Save Path)](#22-pipeline-de-guardado-save-path)
  - [2.3 Pipeline de Apertura (Load Path)](#23-pipeline-de-apertura-load-path)
- [3. Formato Binario del Archivo en Disco](#3-formato-binario-del-archivo-en-disco)
  - [3.1 Magic Number](#31-magic-number)
  - [3.2 Estructura `FileHeader` con `__attribute__((packed))`](#32-estructura-fileheader-con-attribute-packed)
  - [3.3 Layout Completo del Archivo](#33-layout-completo-del-archivo)
- [4. Escritura Optimizada con Bloques de 4KB](#4-escritura-optimizada-con-bloques-de-4kb)
  - [4.1 El Problema: Context Switches por Escritura Naïve](#41-el-problema-context-switches-por-escritura-naïve)
  - [4.2 La Solución: `write_in_chunks()`](#42-la-solución-write_in_chunks)
  - [4.3 Lectura Optimizada con `mmap()`](#43-lectura-optimizada-con-mmap)
- [5. Algoritmo de Compresión: Run-Length Encoding (RLE)](#5-algoritmo-de-compresión-run-length-encoding-rle)
  - [5.1 Fundamento](#51-fundamento)
  - [5.2 Implementación](#52-implementación)
  - [5.3 Checksum djb2](#53-checksum-djb2)
- [6. Estructura del Proyecto](#6-estructura-del-proyecto)
- [7. Decisiones de Diseño y Trade-offs](#7-decisiones-de-diseño-y-trade-offs)
- [8. Ejemplo de Sesión con `strace`](#8-ejemplo-de-sesión-con-strace)
- [9. Interfaz gráfica](#9-interfaz-gráfica)
  - [9.1 Patrón de Arquitectura: MVC (Modelo-Vista-Controlador)](#91-patrón-de-arquitectura-mvc-modelo-vista-controlador)
  - [9.2 Renderizado Dinámico y Scroll](#92-renderizado-dinámico-y-scroll)
  - [9.3 Decodificación de Atributos (Spans)](#93-decodificación-de-atributos-spans)

---

## 1. Visión General del Sistema

Este editor implementa una arquitectura de I/O en **dos capas desacopladas** con una interfaz contractual explícita (`editor_api.h`). El principio guía es que **ningún byte de texto viaja al disco en claro**: toda escritura pasa obligatoriamente por un pipeline de compresión en User Space antes de invocar cualquier syscall del kernel.


<img width="1003" height="815" alt="Diagrama sin título" src="https://github.com/user-attachments/assets/4b1037c2-3e71-4329-8872-815b3367da8b" />


---

## 2. Contrato entre Capas: `editor_api.h`

La separación entre la capa de edición (Persona 1) y la capa de I/O (Persona 2) está formalizada en `src/editor/editor_api.h`. Esta interfaz es el único punto de acoplamiento entre ambos módulos.

### 2.1 Estructura del Estado del Editor

```c
typedef struct {
    LineList *document;       /* Documento: lista de líneas con Gap Buffers */
    char      filename[256];  /* Nombre del archivo activo                  */
    int       modified;       /* Flag de cambios sin persistir               */
} EditorState;
```

### 2.2 Pipeline de Guardado (Save Path)

```
editor_export_text(ed, &size)   → buffer raw del texto (bytes planos)
         │
         ▼
editor_export_spans(ed, &size)  → buffer raw de spans de formato
         │
         ▼
compress_rle(text_raw, ...)     → text_cmp  (payload comprimido)
compress_rle(spans_raw, ...)    → spans_cmp (payload comprimido)
         │
         ▼
FileHeader (packed) + payloads  → write_in_chunks(fd, ...)  [bloques 4KB]
         │
         ▼
                    DISCO (formato binario)
```

### 2.3 Pipeline de Apertura (Load Path)

```
open() + fstat()                → verificación de tamaño mínimo
         │
         ▼
mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0)
         │                      ← lectura sin copia extra al kernel buffer
         ▼
memcmp(header->magic, "EDITORF\0", 8)  → validación de integridad
         │
         ▼
decompress_rle(text_cmp_ptr, ...)  → text_raw
decompress_rle(spans_cmp_ptr, ...) → spans_raw
         │
         ▼
editor_import_text(ed, text_raw, len)
editor_import_spans(ed, spans_raw, len)
         │
         ▼
munmap() + free()               → sin memory leaks
```

---

## 3. Formato Binario del Archivo en Disco

### 3.1 Magic Number

El archivo comienza con 8 bytes fijos que identifican inequívocamente el formato y permiten detectar corrupción o archivos inválidos antes de parsear cualquier campo de longitud variable:

```
Offset 0x00:  0x45 0x44 0x49 0x54 0x4F 0x52 0x46 0x00
              E    D    I    T    O    R    F    \0
```

La presencia del byte nulo `\0` en la posición 7 es intencional: previene que `strcmp()` se use accidentalmente en lugar de `memcmp()`, forzando al programador a manejar el magic como datos binarios puros.

**Verificación en código:**
```c
if (memcmp(header->magic, "EDITORF\0", 8) != 0) {
    munmap(mapped, sb.st_size);
    return -1; /* Archivo inválido o corrupto */
}
```

### 3.2 Estructura `FileHeader` con `__attribute__((packed))`

```c
typedef struct __attribute__((packed)) {
    char     magic[8];                /* "EDITORF\0" — 8 bytes fijos      */
    uint16_t version;                 /* Versión del formato (actualmente 1) */
    uint64_t text_compressed_size;    /* Bytes del payload de texto comprimido */
    uint64_t spans_compressed_size;   /* Bytes del payload de spans comprimido */
    uint64_t text_original_size;      /* Bytes del texto antes de comprimir */
    uint64_t spans_original_size;     /* Bytes de spans antes de comprimir */
    uint32_t checksum;                /* Hash djb2 del texto comprimido */
} FileHeader;
```

**¿Por qué `__attribute__((packed))`?**

Sin este atributo, `gcc` insertaría bytes de padding para alinear cada campo al tamaño de su tipo natural:

```
SIN packed:                         CON packed:
magic[8]  → offset 0  (8 bytes)    magic[8]  → offset 0  (8 bytes)
version   → offset 10 (+2 padding) version   → offset 8  (2 bytes)
          → ...malgasta espacio      text_cmp  → offset 10 (8 bytes)
                                     spans_cmp → offset 18 (8 bytes)
                                     text_orig → offset 26 (8 bytes)
                                     spans_orig→ offset 34 (8 bytes)
                                     checksum  → offset 42 (4 bytes)
                                     TOTAL: 46 bytes exactos
```

El `packed` garantiza un layout binario determinístico: el mismo binario generado en `x86_64` puede ser leído en `ARM64` siempre que se maneje correctamente el endianness.

### 3.3 Layout Completo del Archivo

```
┌────────────────────────────────────────────────────┐
│ Offset 0x00  │ magic[8]         │ "EDITORF\0"      │
│ Offset 0x08  │ version          │ uint16_t  = 1    │
│ Offset 0x0A  │ text_comp_size   │ uint64_t  (N₁)   │
│ Offset 0x12  │ spans_comp_size  │ uint64_t  (N₂)   │
│ Offset 0x1A  │ text_orig_size   │ uint64_t  (M₁)   │
│ Offset 0x22  │ spans_orig_size  │ uint64_t  (M₂)   │
│ Offset 0x2A  │ checksum         │ uint32_t         │
├────────────────────────────────────────────────────┤
│ Offset 0x2E  │ Payload texto comprimido (N₁ bytes) │
├────────────────────────────────────────────────────┤
│ Offset 0x2E  │ Payload spans comprimido (N₂ bytes) │
│ + N₁         │                                     │
└────────────────────────────────────────────────────┘
```

El parser calcula los offsets aritméticamente sin ninguna búsqueda:
```c
const char *text_cmp_ptr  = mapped + sizeof(FileHeader);
const char *spans_cmp_ptr = text_cmp_ptr + header->text_compressed_size;
```

---

## 4. Escritura Optimizada con Bloques de 4KB

### 4.1 El Problema: Context Switches por Escritura Naïve

Una escritura con `fprintf()` o `fwrite()` de N bytes puede traducirse en múltiples syscalls `write()` con tamaños arbitrarios, forzando transiciones User Mode → Kernel Mode costosas (≈ 1.000–4.000 ciclos de CPU por transición).

### 4.2 La Solución: `write_in_chunks()`

```c
#define CHUNK_SIZE 4096  /* Exactamente una página de memoria del kernel */

static int write_in_chunks(int fd, const char *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        size_t to_write = size - written;
        if (to_write > CHUNK_SIZE) to_write = CHUNK_SIZE;
        ssize_t res = write(fd, data + written, to_write);
        if (res < 0) return -1;
        written += res;
    }
    return 0;
}
```

**¿Por qué exactamente 4096 bytes?**

El subsistema de I/O del kernel de Linux organiza las transferencias en unidades de **páginas** (`PAGE_SIZE = 4096` en x86_64). Al escribir exactamente en múltiplos de 4KB:

1. El buffer del VFS (`page cache`) se llena con una sola operación DMA.
2. No hay escrituras parciales que requieran un `read-modify-write` en la página.
3. El scheduler de I/O puede fusionar (`merge`) bloques contiguos más fácilmente.

### 4.3 Lectura Optimizada con `mmap()`

Para la carga de archivos se utiliza `mmap()` en lugar de `read()` iterativo:

```c
char *mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
```

| Parámetro     | Valor          | Significado                                      |
|---------------|----------------|--------------------------------------------------|
| `addr`        | `NULL`         | El kernel elige la dirección virtual             |
| `prot`        | `PROT_READ`    | Solo lectura, sin riesgo de escritura accidental |
| `flags`       | `MAP_PRIVATE`  | Copia privada; escrituras no afectan el archivo  |
| `fd`          | descriptor     | Referencia al archivo abierto                    |
| `offset`      | `0`            | Mapear desde el inicio                           |

**Ventaja sobre `read()` iterativo:** El kernel gestiona el prefetch de páginas automáticamente. El proceso no consume ciclos de CPU esperando `read()` bloqueante; las páginas se cargan on-demand via fallo de página.

---

## 5. Algoritmo de Compresión: Run-Length Encoding (RLE)

### 5.1 Fundamento

El algoritmo implementado en `src/io/compressor.c` codifica secuencias de caracteres repetidos como un par `(count, char)`:

```
Texto original:  "AAABBBCCDDDDDDDD" (16 bytes)
Comprimido RLE:  \x03A\x03B\x02C\x08D (8 bytes)
Ratio:           50% de reducción
```

### 5.2 Implementación

```c
char *compress_rle(const char *input, size_t input_len, size_t *out_len) {
    /* Peor caso: sin repeticiones → 2x el tamaño original */
    size_t max_out = input_len * 2;
    char  *out_buf = (char *)malloc(max_out);

    size_t out_idx = 0, i = 0;
    while (i < input_len) {
        unsigned char count = 1;
        /* Contamos repeticiones consecutivas (máximo 255 para caber en 1 byte) */
        while (i + count < input_len && count < 255 && input[i] == input[i + count])
            count++;
        out_buf[out_idx++] = (char)count;  /* byte de conteo */
        out_buf[out_idx++] = input[i];     /* carácter       */
        i += count;
    }

    *out_len = out_idx;
    /* realloc para devolver exactamente la memoria necesaria */
    char *shrunk = (char *)realloc(out_buf, out_idx);
    return shrunk ? shrunk : out_buf;
}
```

**Propiedades del algoritmo:**
- **Tiempo:** O(n) — una pasada lineal sobre el input.
- **Espacio peor caso:** 2× el original (texto sin repeticiones).
- **Espacio mejor caso:** 2/n × el original (texto totalmente uniforme, e.g., `\0\0\0...`).
- **Aplicabilidad:** Especialmente eficiente para los **spans** de formato, donde un estilo se aplica a rangos contiguos de caracteres.

### 5.3 Checksum djb2

El checksum almacenado en `FileHeader.checksum` usa el algoritmo djb2:

```c
static uint32_t calculate_checksum(const char *data, size_t len) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (uint8_t)data[i]; /* hash * 33 + c */
    return hash;
}
```

Se calcula sobre el **texto ya comprimido**, lo que garantiza detección de corrupción tanto del proceso de compresión como del almacenamiento en disco.

---

## 6. Estructura del Proyecto

```
.
├── Makefile
├── editor
├── README.md
├── docs/
│   ├── architecture.md          ← Pipeline de I/O, formato binario, layout del header
│   ├── memory_management.md     ← Gap Buffers, análisis de malloc/free, reporte Valgrind
│   └── profiling_report.md      ← Comparativa strace: Clásico vs Propuesto
├── src/
│   ├── main.c                   ← Punto de entrada
│   ├── editor/
│   │   ├── editor_api.{h,c}     ← Contrato público entre capa de edición e I/O
│   │   ├── gap_buffer.{h,c}     ← Estructura de datos O(1) para edición
│   │   ├── line_list.{h,c}      ← Lista de líneas (cada línea = un GapBuffer)
│   │   └── rich_text.{h,c}      ← Metadatos de formato (spans de estilo)
│   ├── io/
│   │   ├── compressor.{h,c}     ← RLE compress/decompress
│   │   └── file_manager.{h,c}   ← Orchestrator: export → compress → write()
│   └── ui/
│       └── ncurses_ui.{h,c}     ← TUI con ncurses
└── tests/
    ├── test_gap_buffer.c         ← Tests unitarios del Gap Buffer
    └── test_editor.c             ← Tests de integración del editor
```


**Regla de dependencias (acíclica):**
```
ncurses_ui → editor_api → gap_buffer, line_list, rich_text
file_manager → editor_api, compressor
main → ncurses_ui, editor_api, file_manager
```

Ningún módulo de la capa `editor/` depende de `io/` ni de `ui/`, garantizando que la lógica de edición puede probarse de forma completamente aislada (ver `tests/test_gap_buffer.c`).

---

## 7. Decisiones de Diseño y Trade-offs

| Decisión | Alternativa Considerada | Razón de la Elección |
|---|---|---|
| RLE como compresor | zlib/Deflate, LZ4 | Sin dependencias externas; O(n) determinístico; suficiente para spans repetitivos |
| `mmap()` para lectura | `read()` iterativo | Cero copias extra; el kernel gestiona prefetch; permite acceso por offset directo |
| `write()` en chunks 4KB | `fwrite()` con buffer de libc | Control explícito de syscalls; compatible con `strace -c` para demostración empírica |
| `__attribute__((packed))` | Estructura con padding | Layout binario determinístico; portabilidad entre arquitecturas |
| `memcmp()` para magic | `strncmp()` | El magic contiene `\0`; `strcmp` terminaría prematuramente |
| `MAP_PRIVATE` en mmap | `MAP_SHARED` | El editor no debe modificar el archivo durante la carga |

---

## 8. Ejemplo de Sesión con `strace`

Al ejecutar `strace -e trace=write,read,mmap,open,close ./editor test.txt`, se observa el pipeline de syscalls reducidas:

```
openat(AT_FDCWD, "test.txt", O_RDONLY)   = 3
fstat(3, {st_size=2048, ...})             = 0
mmap(NULL, 2048, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7f... ← 1 syscall para leer TODO
close(3)                                  = 0
--- [ edición en RAM, CERO syscalls ] ---
openat(AT_FDCWD, "test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 4
write(4, "\x45\x44\x49...", 46)           = 46   ← header
write(4, "\x03A\x03B...",  4096)          = 4096 ← chunk 1
write(4, "\x02C\x08D...",  512)           = 512  ← chunk final
close(4)                                  = 0
```

Comparado con un editor que usa `fprintf()` por carácter, la reducción en número de syscalls es de varios órdenes de magnitud para archivos de texto medianos.

---
## 9. Interfaz gráfica 

La interfaz gráfica (`ncurses_ui.c`) actúa como una capa de presentación pasiva que aísla completamente a los módulos lógicos (Persona 1 y 2) de preocuparse por la consola. Toda la comunicación fluye a través del objeto `EditorState`.

### 9.1. Patrón de Arquitectura: MVC (Modelo-Vista-Controlador)
El sistema implementa una variación del patrón MVC:
*   **Modelo (`EditorState` / `LineList`):** Contiene la verdad absoluta sobre los datos (líneas de texto, atributos y posición real del cursor).
*   **Vista (`ui_render`):** Se encarga exclusivamente de traducir el modelo a la pantalla física. Dibuja un marco delimitador alrededor del área de edición y aplica colores a través de los *color pairs* de `ncurses` (ej. rojo para el borde, blanco para el texto, cian para la barra de estado).
*   **Controlador (`ui_run` / `ui_handle_key`):** Entra en un bucle infinito que escucha el teclado (`wgetch`), procesa atajos de teclado (ej. Ctrl+O, Ctrl+S) delegando la lógica de negocio a la API del Editor (ej. `editor_move_left`, `file_save`), y luego pide a la Vista que se redibuje.

### 9.2. Renderizado Dinámico y Scroll
El editor calcula constantemente la diferencia entre la coordenada "lógica" (dónde está el cursor en la lista enlazada) y la coordenada "física" (qué parte del documento cabe en la pantalla de la terminal). 
Si el archivo tiene 1000 líneas y la terminal solo mide 30 líneas de alto, la interfaz aplica un cálculo de **Offset (desplazamiento vertical y horizontal)** para renderizar únicamente el subconjunto de nodos visibles, garantizando que el consumo de CPU sea constante $O(1)$ sin importar qué tan grande sea el archivo.

### 9.3. Decodificación de Atributos (Spans)
Al dibujar una línea, la interfaz consulta los `Spans` del nodo actual. Activa funciones especiales de `ncurses` como `wattron(A_BOLD)` para negrita o cambia los pares de colores si detecta que la posición del carácter intercepta el rango definido por un estilo de texto enriquecido, apagándolos con `wattroff` cuando el rango termina.
