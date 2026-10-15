#include "transaction.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char* action_to_string(LendingAction action) {
    switch (action) {
        case ACTION_BORROWED: return "BORROWED";
        case ACTION_RETURNED: return "RETURNED";
        case ACTION_RESERVED: return "RESERVED";
        case ACTION_RENEWED:  return "RENEWED";
        case ACTION_OVERDUE:  return "OVERDUE";
        default:              return "UNKNOWN";
    }
}

LendingAction string_to_action(const char *str) {
    if (!str) return ACTION_BORROWED;
    if (strcmp(str, "BORROWED") == 0) return ACTION_BORROWED;
    if (strcmp(str, "RETURNED") == 0) return ACTION_RETURNED;
    if (strcmp(str, "RESERVED") == 0) return ACTION_RESERVED;
    if (strcmp(str, "RENEWED") == 0)  return ACTION_RENEWED;
    if (strcmp(str, "OVERDUE") == 0)  return ACTION_OVERDUE;
    return ACTION_BORROWED;
}

void record_get_signable_data(const LendingRecord *rec, char *out_buf, size_t buf_size) {
    if (!rec || !out_buf || buf_size == 0) return;
    snprintf(out_buf, buf_size, "%s|%s|%s|%s|%s|%s|%s|%ld|%ld|%.2f|%u",
             rec->record_id,
             rec->book_id,
             rec->copy_id,
             rec->book_title,
             rec->member_id,
             rec->member_name,
             action_to_string(rec->action),
             (long)rec->timestamp,
             (long)rec->due_date,
             rec->fine_amount,
             rec->nonce);
}

void record_compute_hash(const LendingRecord *rec, char *out_hash_hex) {
    if (!rec || !out_hash_hex) return;
    char payload[1024];
    char signable[512];
    record_get_signable_data(rec, signable, sizeof(signable));
    snprintf(payload, sizeof(payload), "%s|%s", signable, rec->signature);
    sha256_string(payload, out_hash_hex);
}

int record_sign(LendingRecord *rec, const char *privkey_pem) {
    if (!rec || !privkey_pem) return 0;
    char signable[512];
    record_get_signable_data(rec, signable, sizeof(signable));
    return crypto_sign_message(privkey_pem, signable, rec->signature, sizeof(rec->signature));
}

int record_verify_signature(const LendingRecord *rec, const char *pubkey_pem) {
    if (!rec || !pubkey_pem) return 0;
    char signable[512];
    record_get_signable_data(rec, signable, sizeof(signable));
    return crypto_verify_signature(pubkey_pem, signable, rec->signature);
}
