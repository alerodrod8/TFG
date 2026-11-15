/*
 * aes.h
 * Minimal AES-128 CBC decrypt API compatible with the user's nfc.c.
 * Interface compatible with tiny-AES-c names used in nfc.c.
 */

#ifndef INC_AES_H_
#define INC_AES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define AES_BLOCKLEN 16
#define AES_KEYLEN   16
#define AES_keyExpSize 176

struct AES_ctx
{
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv);
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv);
void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* INC_AES_H_ */
