#ifndef ALU_CRYPTO_H
#define ALU_CRYPTO_H

#include <stddef.h>

#define SHA256_HEX_LEN 65
#define SIG_HEX_MAX_LEN 256
#define PRIVKEY_PEM_MAX_LEN 1024
#define PUBKEY_PEM_MAX_LEN 512

#ifdef __cplusplus
extern "C" {
#endif

/* Computes SHA-256 of raw buffer and writes 64-char hex string + null terminator */
void sha256_buffer(const unsigned char *data, size_t len, char *output_hex);

/* Computes SHA-256 of null-terminated string and writes 64-char hex string */
void sha256_string(const char *str, char *output_hex);

/* Generates an ECDSA keypair on the secp256k1 curve in PEM format */
int crypto_generate_keypair(char *privkey_pem, size_t priv_size, char *pubkey_pem, size_t pub_size);

/* Save and load persistent member keypairs to/from disk */
int crypto_save_keypair(const char *member_id, const char *privkey_pem, const char *pubkey_pem);
int crypto_load_keypair(const char *member_id, char *privkey_pem, size_t priv_size, char *pubkey_pem, size_t pub_size);

/* Signs a message with ECDSA secp256k1 private key, returning DER signature in hex */
int crypto_sign_message(const char *privkey_pem, const char *message, char *sig_hex, size_t sig_hex_size);

/* Verifies an ECDSA signature against the signer's PEM public key (returns 1 for valid, 0 for invalid) */
int crypto_verify_signature(const char *pubkey_pem, const char *message, const char *sig_hex);

/* Utility functions for hex conversions */
void bytes_to_hex(const unsigned char *bytes, size_t len, char *hex_out);
int hex_to_bytes(const char *hex_in, unsigned char *bytes_out, size_t max_bytes, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ALU_CRYPTO_H */
