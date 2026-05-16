/*
 * crypto.c — Cifrado RC4 en RAM pura.
 *
 * Recibe buffers ya comprimidos (RLE), los cifra y los devuelve.
 * No hace ninguna llamada de I/O (open/read/write).
 */

#include "crypto.h"

#include <stdlib.h>    /* malloc, free   */
#include <string.h>    /* memcpy         */
#include <sys/mman.h>  /* mlock, munlock */

#define RC4_SBOX_SIZE 256

/* crypto_secure_wipe
 * Borra `size` bytes con ceros.
 * Usa `volatile` para que gcc -O2 no elimine la escritura.
 * Sin esto, el compilador puede saltarse el borrado si detecta
 * que la variable no se vuelve a leer (y la clave quedaría en RAM).
 */
void crypto_secure_wipe(void *ptr, size_t size) {
    if (!ptr || size == 0) return;

    /* volatile obliga al compilador a ejecutar cada escritura */
    volatile unsigned char *vptr = (volatile unsigned char *)ptr;
    size_t i;
    for (i = 0; i < size; i++) {
        vptr[i] = 0x00;
    }

    /* Barrera para evitar que el CPU reordene las escrituras */
    __asm__ __volatile__("" : : "r"(ptr) : "memory");
}

/* crypto_lock_memory
 * Usa mlock() para que el kernel no mueva esta página al Swap.
 * Si la clave llega al Swap (disco), un atacante podría leerla
 * incluso después de que el programa termine.
 */
int crypto_lock_memory(const void *ptr, size_t size) {
    if (!ptr)      return CRYPTO_ENULL;
    if (size == 0) return CRYPTO_EZERO;

    /* Si falla (p.ej. límite de memoria bloqueada superado),
     * el programa sigue pero con seguridad reducida. */
    if (mlock(ptr, size) != 0) return CRYPTO_ERR;
    return CRYPTO_OK;
}

/* crypto_unlock_memory
 * Libera el bloqueo de mlock().
 * Llamar SIEMPRE después de crypto_secure_wipe(), nunca antes.
 */
int crypto_unlock_memory(const void *ptr, size_t size) {
    if (!ptr)      return CRYPTO_ENULL;
    if (size == 0) return CRYPTO_EZERO;

    if (munlock(ptr, size) != 0) return CRYPTO_ERR;
    return CRYPTO_OK;
}

/* RC4 — Key Scheduling (KSA)
 * Mezcla el array S[256] usando la clave para generar
 * una permutación única derivada de ella.
 */
static void rc4_ksa(unsigned char S[RC4_SBOX_SIZE],
                    const unsigned char *key, size_t key_len) {
    size_t i, j;

    /* Inicializar S como permutación identidad: S[i] = i */
    for (i = 0; i < RC4_SBOX_SIZE; i++) {
        S[i] = (unsigned char)i;
    }

    /* Mezclar S con los bytes de la clave */
    j = 0;
    for (i = 0; i < RC4_SBOX_SIZE; i++) {
        j = (j + S[i] + key[i % key_len]) % RC4_SBOX_SIZE;
        unsigned char tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;
    }
}

/* RC4 — Generación del keystream (PRGA) + XOR
 * Genera un byte pseudoaleatorio por cada byte de entrada
 * y lo combina con XOR para cifrar o descifrar.
 */
static void rc4_prga(unsigned char S[RC4_SBOX_SIZE],
                     const unsigned char *input,
                     unsigned char *output,
                     size_t len) {
    size_t i   = 0;
    size_t idx = 0;
    size_t j   = 0;

    for (idx = 0; idx < len; idx++) {
        i = (i + 1) % RC4_SBOX_SIZE;
        j = (j + S[i]) % RC4_SBOX_SIZE;

        /* Intercambio */
        unsigned char tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;

        /* XOR del byte de entrada con el byte del keystream */
        unsigned char ks = S[(S[i] + S[j]) % RC4_SBOX_SIZE];
        output[idx] = input[idx] ^ ks;
    }
}

/* crypto_transform
 * Cifra o descifra `input` con la clave dada usando RC4.
 * Devuelve un nuevo buffer en el Heap; el llamador hace free().
 * Al terminar, borra el estado interno (S-box) para no dejar
 * material criptográfico en el stack.
 *
 * Uso en file_manager.c:
 *   char *cifrado = crypto_transform(rle_buf, rle_size, key, key_len);
 *   write_in_chunks(fd, cifrado, rle_size);
 *   crypto_secure_wipe(cifrado, rle_size);
 *   free(cifrado);
 *
 * ----------------------------------------------------------- */
char *crypto_transform(const char *input, size_t len,
                       const char *key,   size_t key_len) {
    if (!input || !key || len == 0 || key_len == 0) return NULL;

    char *output = (char *)malloc(len);
    if (!output) return NULL;

    /* S-box en el stack: 256 bytes, nunca toca el disco */
    unsigned char S[RC4_SBOX_SIZE];

    rc4_ksa(S, (const unsigned char *)key, key_len);
    rc4_prga(S, (const unsigned char *)input, (unsigned char *)output, len);

    /* Borrar la S-box para no exponer el estado del cifrado */
    crypto_secure_wipe(S, sizeof(S));

    return output;
}
