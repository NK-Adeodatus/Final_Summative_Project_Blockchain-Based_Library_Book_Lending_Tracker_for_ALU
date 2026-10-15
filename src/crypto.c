#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/sha.h>
#include <openssl/obj_mac.h>

void bytes_to_hex(const unsigned char *bytes, size_t len, char *hex_out) {
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex_out[i * 2]     = hex_digits[(bytes[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_digits[bytes[i] & 0x0F];
    }
    hex_out[len * 2] = '\0';
}

int hex_to_bytes(const char *hex_in, unsigned char *bytes_out, size_t max_bytes, size_t *out_len) {
    if (!hex_in || !bytes_out) return 0;
    size_t hex_len = strlen(hex_in);
    if (hex_len % 2 != 0 || (hex_len / 2) > max_bytes) return 0;

    for (size_t i = 0; i < hex_len; i += 2) {
        char byte_str[3] = { hex_in[i], hex_in[i + 1], '\0' };
        char *endptr = NULL;
        unsigned long val = strtoul(byte_str, &endptr, 16);
        if (endptr != &byte_str[2]) return 0;
        bytes_out[i / 2] = (unsigned char)val;
    }
    if (out_len) *out_len = hex_len / 2;
    return 1;
}

void sha256_buffer(const unsigned char *data, size_t len, char *output_hex) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        memset(output_hex, '0', 64);
        output_hex[64] = '\0';
        return;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) == 1 &&
        EVP_DigestUpdate(mdctx, data, len) == 1 &&
        EVP_DigestFinal_ex(mdctx, hash, &hash_len) == 1) {
        bytes_to_hex(hash, hash_len, output_hex);
    } else {
        memset(output_hex, '0', 64);
        output_hex[64] = '\0';
    }
    EVP_MD_CTX_free(mdctx);
}

void sha256_string(const char *str, char *output_hex) {
    if (!str) {
        sha256_buffer((const unsigned char *)"", 0, output_hex);
    } else {
        sha256_buffer((const unsigned char *)str, strlen(str), output_hex);
    }
}

int crypto_generate_keypair(char *privkey_pem, size_t priv_size, char *pubkey_pem, size_t pub_size) {
    if (!privkey_pem || !pubkey_pem) return 0;

    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) return 0;

    if (EVP_PKEY_keygen_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_secp256k1) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        return 0;
    }

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        return 0;
    }
    EVP_PKEY_CTX_free(pctx);

    /* Export Private Key */
    BIO *bio_priv = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio_priv, pkey, NULL, NULL, 0, NULL, NULL);
    int priv_read = BIO_read(bio_priv, privkey_pem, (int)priv_size - 1);
    if (priv_read > 0) {
        privkey_pem[priv_read] = '\0';
    } else {
        privkey_pem[0] = '\0';
    }
    BIO_free(bio_priv);

    /* Export Public Key */
    BIO *bio_pub = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio_pub, pkey);
    int pub_read = BIO_read(bio_pub, pubkey_pem, (int)pub_size - 1);
    if (pub_read > 0) {
        pubkey_pem[pub_read] = '\0';
    } else {
        pubkey_pem[0] = '\0';
    }
    BIO_free(bio_pub);

    EVP_PKEY_free(pkey);
    return (priv_read > 0 && pub_read > 0) ? 1 : 0;
}

#ifdef _WIN32
  #include <direct.h>
  #define make_key_dir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #define make_key_dir(p) mkdir(p, 0755)
#endif

int crypto_save_keypair(const char *member_id, const char *privkey_pem, const char *pubkey_pem) {
    if (!member_id || !privkey_pem || !pubkey_pem) return 0;
    make_key_dir("chain_data");
    make_key_dir("chain_data/keys");

    char priv_path[256], pub_path[256];
    snprintf(priv_path, sizeof(priv_path), "chain_data/keys/%s_priv.pem", member_id);
    snprintf(pub_path, sizeof(pub_path), "chain_data/keys/%s_pub.pem", member_id);

    FILE *fpr = fopen(priv_path, "w");
    if (fpr) {
        fputs(privkey_pem, fpr);
        fclose(fpr);
    }
    FILE *fpu = fopen(pub_path, "w");
    if (fpu) {
        fputs(pubkey_pem, fpu);
        fclose(fpu);
    }
    return (fpr && fpu) ? 1 : 0;
}

int crypto_load_keypair(const char *member_id, char *privkey_pem, size_t priv_size, char *pubkey_pem, size_t pub_size) {
    if (!member_id || !privkey_pem || !pubkey_pem) return 0;
    char priv_path[256], pub_path[256];
    snprintf(priv_path, sizeof(priv_path), "chain_data/keys/%s_priv.pem", member_id);
    snprintf(pub_path, sizeof(pub_path), "chain_data/keys/%s_pub.pem", member_id);

    FILE *fpr = fopen(priv_path, "r");
    FILE *fpu = fopen(pub_path, "r");
    if (!fpr || !fpu) {
        if (fpr) fclose(fpr);
        if (fpu) fclose(fpu);
        return 0;
    }

    size_t r_priv = fread(privkey_pem, 1, priv_size - 1, fpr);
    privkey_pem[r_priv] = '\0';
    fclose(fpr);

    size_t r_pub = fread(pubkey_pem, 1, pub_size - 1, fpu);
    pubkey_pem[r_pub] = '\0';
    fclose(fpu);

    return (r_priv > 0 && r_pub > 0) ? 1 : 0;
}

int crypto_sign_message(const char *privkey_pem, const char *message, char *sig_hex, size_t sig_hex_size) {
    if (!privkey_pem || !message || !sig_hex) return 0;

    BIO *bio_priv = BIO_new_mem_buf(privkey_pem, -1);
    if (!bio_priv) return 0;

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio_priv, NULL, NULL, NULL);
    BIO_free(bio_priv);
    if (!pkey) return 0;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return 0;
    }

    int success = 0;
    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
        EVP_DigestSignUpdate(mdctx, message, strlen(message)) == 1) {
        size_t siglen = 0;
        if (EVP_DigestSignFinal(mdctx, NULL, &siglen) == 1 && siglen > 0) {
            unsigned char *sig_buf = (unsigned char *)malloc(siglen);
            if (sig_buf) {
                if (EVP_DigestSignFinal(mdctx, sig_buf, &siglen) == 1) {
                    if (siglen * 2 + 1 <= sig_hex_size) {
                        bytes_to_hex(sig_buf, siglen, sig_hex);
                        success = 1;
                    }
                }
                free(sig_buf);
            }
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return success;
}

int crypto_verify_signature(const char *pubkey_pem, const char *message, const char *sig_hex) {
    if (!pubkey_pem || !message || !sig_hex) return 0;

    unsigned char sig_bytes[256];
    size_t sig_len = 0;
    if (!hex_to_bytes(sig_hex, sig_bytes, sizeof(sig_bytes), &sig_len)) {
        return 0;
    }

    BIO *bio_pub = BIO_new_mem_buf(pubkey_pem, -1);
    if (!bio_pub) return 0;

    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio_pub, NULL, NULL, NULL);
    BIO_free(bio_pub);
    if (!pkey) return 0;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return 0;
    }

    int valid = 0;
    if (EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
        EVP_DigestVerifyUpdate(mdctx, message, strlen(message)) == 1) {
        if (EVP_DigestVerifyFinal(mdctx, sig_bytes, sig_len) == 1) {
            valid = 1;
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return valid;
}
