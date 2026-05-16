/*
 * crypto.h  —  Capa de Cifrado en RAM

 * Recibe los buffers ya comprimidos (RLE, desde file_manager.c) y
 * los transforma criptográficamente en RAM pura, sin ninguna
 * syscall de I/O (open/read/write).  La escritura a disco sigue
 * siendo responsabilidad exclusiva de file_manager.c.
 */

#ifndef CRYPTO_H
#define CRYPTO_H
#include <stddef.h>  /* size_t */

/* ── Códigos de error ───────────────────────────────────────── */
#define CRYPTO_OK       0
#define CRYPTO_ERR     -1   /* error genérico (malloc, mlock, etc.) */
#define CRYPTO_ENULL   -2   /* puntero NULL recibido */
#define CRYPTO_EZERO   -3   /* tamaño cero recibido */

/*  crypto_transform
 *
 * Cifra o descifra el `input` usando RC4.
 * RC4 es simétrico: aplicar la función dos veces con la misma clave
 * devuelve el texto original.  Por eso sirve tanto para cifrar (antes
 * de write()) como para descifrar (después de read()).
 */
char *crypto_transform(const char *input, size_t len,
                       const char *key,   size_t key_len);

/* crypto_lock_memory
 *
 * Bloquea la página de RAM que contiene `ptr` usando mlock(2) y 
 * el kernel NO moverá esa región de RAM a la partición Swap,
 * impidiendo que la clave secreta quede grabada en disco aunque el
 * sistema esté bajo presión de memoria.
 */
int crypto_lock_memory(const void *ptr, size_t size);

/* crypto_unlock_memory
 * 
 * Desbloquea la región previamente bloqueada con crypto_lock_memory().
 * No llamar a munlock() sin haber
 * borrado antes la clave (haría que la página sea paginable con datos
 * sensibles aún presentes).
 */
int crypto_unlock_memory(const void *ptr, size_t size);

/*  crypto_secure_wipe
 * Sobreescribe `size` bytes a partir de `ptr` con ceros, garantizando
 * que el compilador NO elimine la operación como "código muerto".
 * Usa un bucle con puntero `volatile unsigned char *` para engañar
 * al optimizador: el estándar C exige que los accesos a objetos
 * volatile se ejecuten siempre, en orden y sin ser eliminados.
 * Si explicit_bzero(3) está disponible en la plataforma, se usa
 * como alternativa más robusta.
 */
void crypto_secure_wipe(void *ptr, size_t size);

#endif /* CRYPTO_H */
