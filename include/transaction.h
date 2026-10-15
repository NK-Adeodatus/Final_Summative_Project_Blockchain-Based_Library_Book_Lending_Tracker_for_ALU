#ifndef ALU_TRANSACTION_H
#define ALU_TRANSACTION_H

#include <time.h>
#include <stdint.h>
#include "crypto.h"

#define ID_MAX_LEN 32
#define STR_MAX_LEN 128
#define RECORD_ID_MAX_LEN 64

typedef enum {
    ACTION_BORROWED = 0,
    ACTION_RETURNED,
    ACTION_RESERVED,
    ACTION_RENEWED,
    ACTION_OVERDUE
} LendingAction;

typedef struct {
    char record_id[RECORD_ID_MAX_LEN];
    char book_id[ID_MAX_LEN];
    char copy_id[ID_MAX_LEN];
    char book_title[STR_MAX_LEN];
    char member_id[ID_MAX_LEN];
    char member_name[STR_MAX_LEN];
    LendingAction action;
    time_t timestamp;
    time_t due_date;
    double fine_amount;
    uint32_t nonce;
    char signature[SIG_HEX_MAX_LEN];
} LendingRecord;

#ifdef __cplusplus
extern "C" {
#endif

/* Convert action enum to readable string */
const char* action_to_string(LendingAction action);

/* Convert string to action enum */
LendingAction string_to_action(const char *str);

/* Create record hash digest for signing and Merkle tree leaves */
void record_get_signable_data(const LendingRecord *rec, char *out_buf, size_t buf_size);

/* Compute SHA-256 hash of a lending record */
void record_compute_hash(const LendingRecord *rec, char *out_hash_hex);

/* Sign a lending record using member's private key */
int record_sign(LendingRecord *rec, const char *privkey_pem);

/* Verify a lending record's signature using member's public key */
int record_verify_signature(const LendingRecord *rec, const char *pubkey_pem);

#ifdef __cplusplus
}
#endif

#endif /* ALU_TRANSACTION_H */
