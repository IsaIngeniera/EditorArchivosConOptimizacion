# Editor de Texto Optimizado para I/O (Linux Native)

> **Proyecto:** Wendy Vanesa Atehortua, Isabella Cadavid, Isabella Ocampo y Juan Manuel Hernández
> **Área:** Sistemas Operativos / Programación de Sistemas  
> **Plataforma:** Linux nativo (Kernel 5.x+), C99

---

## Tabla de Contenido

- [1. Descripción](#1-descripción)
- [2. Requisitos del Sistema](#2-requisitos-del-sistema)
- [3. Componentes Técnicos Clave](#3-componentes-técnicos-clave)
  - [3.1 Arquitectura de I/O](#31-arquitectura-de-io)
  - [3.2 Estructura de Datos: Gap Buffer](#32-estructura-de-datos-gap-buffer)
  - [3.3 Formato Binario en Disco](#33-formato-binario-en-disco)
  - [3.4 Compresión RLE (Run-Length Encoding)](#34-compresión-rle-run-length-encoding)
  - [3.5 Seguridad y Opacidad de Datos](#35-seguridad-y-opacidad-de-datos)
- [4. Instrucciones de Compilación y Ejecución](#4-instrucciones-de-compilación-y-ejecución)
  - [4.1 Controles del Editor](#41-controles-del-editor)
- [5. Profiling de Llamadas al Sistema](#5-profiling-de-llamadas-al-sistema)
  - [5.1 Verificar que el Archivo es Binario](#51-verificar-que-el-archivo-es-binario)
- [6. Estructura del Repositorio](#6-estructura-del-repositorio)
- [7. Guía de Documentación](#7-guía-de-documentación)
- [8. Licencia](#8-licencia)

---

## 1. Descripción

Este proyecto es un editor de texto desarrollado en **C nativo** que aborda el cuello de botella de la interacción con el hardware mediante la optimización de llamadas al sistema (syscalls) y la manipulación directa de la memoria. Implementa un **pipeline de compresión en User Space** que garantiza que ningún byte de texto viaja al disco en claro antes de pasar por el compresor y ser agrupado en bloques alineados al tamaño de página del kernel (4096 bytes).

La arquitectura está dividida en dos capas desacopladas con una interfaz contractual explícita (`editor_api.h`): la **capa de edición en RAM** (Gap Buffers, listas de líneas, metadatos de formato) y la **capa de I/O al disco** (compresión RLE, escritura con `write()` en chunks de 4KB, lectura con `mmap()`).

---

## 2. Requisitos del Sistema

```
Sistema Operativo:  Linux (Kernel 5.x o superior)
Arquitectura:       x86_64 (compatible ARM64)
Compilador:         gcc 9.x o superior
```

**Herramientas de compilación y análisis:**

```bash
sudo apt-get install build-essential libncurses-dev \
    strace valgrind linux-tools-common linux-tools-$(uname -r)
```

| Herramienta | Versión mínima | Propósito |
|---|---|---|
| `gcc` | 9.0 | Compilación con `-O2`, soporte `__attribute__((packed))` |
| `make` | 4.0 | Build system |
| `libncurses-dev` | 6.x | TUI (interfaz de texto) |
| `strace` | 5.x | Tracing de syscalls para profiling |
| `valgrind` | 3.15 | Detección de memory leaks |
| `perf` | kernel-tools | Profiling de CPU y cache misses |

---

## 3. Componentes Técnicos Clave

### 3.1. Arquitectura de I/O

El editor utiliza exclusivamente la **API POSIX de bajo nivel** para todas las operaciones de disco. Está explícitamente prohibido el uso de `fprintf`, `fopen`, `fwrite` o cualquier función de la libc que abstraiga las syscalls:

- **Escritura:** `write()` con buffers alineados a páginas de 4KB (`CHUNK_SIZE = 4096`), minimizando las transiciones User Mode → Kernel Mode.
- **Lectura:** `mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0)` para cargar archivos completos en una sola operación, eliminando las `read()` iterativas.
- **Apertura/cierre:** `open()` / `close()` con flags explícitos (`O_RDONLY`, `O_WRONLY | O_CREAT | O_TRUNC`).

### 3.2. Estructura de Datos: Gap Buffer

La edición de texto en RAM se implementa con un **Gap Buffer** (`src/editor/gap_buffer.c`): un array de caracteres con un hueco posicionado en el cursor que hace que insertar y borrar sean operaciones O(1) amortizado, sin mover texto ajeno al cursor.

```
Texto "Hola mundo", cursor entre "Hola" y " mundo":

[ H | o | l | a | _ | _ | _ | _ |   | m | u | n | d | o ]
 ←── texto izquierdo ──→ ←── gap ──→ ←────── texto derecho ──────→

Insertar 'X': solo llenar un slot del gap → O(1)
Mover cursor: solo mover un char al borde del gap → O(1)
```

La estructura usa `__attribute__((packed))` para evitar que el compilador inserte bytes de padding, garantizando un layout de memoria determinístico.

### 3.3. Formato Binario en Disco

Cada archivo guardado por el editor es un **binario con formato propio**, ilegible con editores de texto estándar:

```
┌──────────────────────────────────────────────────┐
│ Magic Number: "EDITORF\0" (8 bytes)              │
├──────────────────────────────────────────────────┤
│ FileHeader __attribute__((packed)):              │
│   version            uint16_t                   │
│   text_compressed_size  uint64_t                │
│   spans_compressed_size uint64_t                │
│   text_original_size    uint64_t                │
│   spans_original_size   uint64_t                │
│   checksum           uint32_t  (djb2)           │
├──────────────────────────────────────────────────┤
│ Payload texto comprimido (RLE)                   │
├──────────────────────────────────────────────────┤
│ Payload spans comprimido (RLE)                   │
└──────────────────────────────────────────────────┘
```

El **checksum djb2** se calcula sobre el texto ya comprimido, detectando corrupción tanto del proceso de compresión como del almacenamiento físico.

### 3.4. Compresión RLE (Run-Length Encoding)

Antes de cualquier `write()`, los datos pasan por `compress_rle()` en `src/io/compressor.c`. El algoritmo codifica secuencias repetidas como pares `(count, char)` en tiempo O(n) sin dependencias externas. Especialmente efectivo para los **spans de formato** (un mismo estilo aplicado a rangos continuos de texto).

### 3.5. Seguridad y Opacidad de Datos

Ningún dato viaja al disco en texto claro. La combinación de compresión + header binario + magic number hace que el archivo sea:

- **Ilegible** con `cat`, `nano`, `notepad` u otros editores.
- **Verificable** mediante `xxd` y la inspección del magic `EDITORF\0`.
- **Íntegro** mediante el checksum que detecta modificaciones no autorizadas.

---

## 4. Instrucciones de Compilación y Ejecución

```bash
# Clonar y compilar el proyecto
git clone <repo-url>
cd EditorArchivosConOptimizacion
make clean
make

# Ejecutar el editor (archivo nuevo)
./editor

# Ejecutar el editor (abrir archivo existente)
./editor [nombre_archivo]

# Compilar con símbolos de debug para profiling
make clean && make all CFLAGS="-g -O0"

# Ejecutar tests unitarios
make test

# Limpiar artefactos de compilación
make clean
```

### 4.1. Controles del Editor

| Tecla | Acción |
|---|---|
| `Ctrl+S` | Guardar (comprime y escribe en formato binario) |
| `Ctrl+Q` | Salir (avisa si hay cambios sin guardar) |
| `Ctrl+O` | Abrir archivo (carga con `mmap` + descompresión) |
| Flechas | Mover cursor (O(1) en el Gap Buffer) |
| `Backspace` | Borrar carácter antes del cursor |
| `Delete` | Borrar carácter después del cursor |

---

## 5. Profiling de Llamadas al Sistema

### Con `strace`

```bash
# Contar y resumir todas las syscalls de una sesión de guardado
strace -c ./editor [archivo_prueba]

# Ver syscalls de escritura/lectura en tiempo real
strace -e trace=write,read,mmap,open,close ./editor [archivo_prueba]

# Comparar contra enfoque clásico (baseline)
strace -c cat /dev/urandom > /tmp/classic_baseline.txt 2>&1
```

### Con `perf`

```bash
# Profiling de CPU con call graph
perf record -g ./editor test_document.txt
perf report --stdio | head -40

# Estadísticas de hardware counters
perf stat -e cycles,instructions,cache-misses,context-switches \
    ./editor test_document.txt
```

### Con `valgrind`

```bash
# Verificación de ausencia de memory leaks
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    ./editor test_document.txt

# Resultado esperado:
# All heap blocks were freed -- no leaks are possible
# ERROR SUMMARY: 0 errors from 0 contexts
```

### 5.1. Verificar que el Archivo es Binario

```bash
# Debe mostrar "data", no "ASCII text"
file output_file.edf

# Inspeccionar el magic number y el header
xxd output_file.edf | head -4
# Primeros 8 bytes: 45 44 49 54 4f 52 46 00 → "EDITORF\0"
```

---

## 6. Estructura del Repositorio

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

---

## 7. Guía de Documentación

Para profundizar en los detalles técnicos de implementación, consultar la carpeta `/docs`:

- [**Arquitectura de I/O**](./docs/architecture.md): Pipeline completo de datos (editor → compresor → buffer 4KB → disco), formato binario con magic number, estructura `FileHeader` packed, justificación de `mmap()` vs `read()`, ejemplo de sesión `strace`.

- [**Gestión de Memoria**](./docs/memory_management.md): Internals del Gap Buffer (visualizaciones de `gb_grow()`, `gb_move_left()`), flujo exhaustivo de `malloc/realloc/free`, ownership explícito de buffers, patrón de uso de `mmap/munmap`, guía de análisis con Valgrind.

- [**Reporte de Profiling**](./docs/profiling_report.md): Metodología de benchmark, plantilla con resultados de `strace -c`, métricas de context switches, comparativa Sys Time vs User Time, análisis de `perf stat`, verificación de opacidad del archivo en disco.

---

## 8. Licencia

Este proyecto está bajo la Licencia MIT.

Copyright (c) 2026 Wendy Vanesa Atehortua, Isabella Cadavid Posada, Isabella Ocampo, Juan Manuel Hernández.