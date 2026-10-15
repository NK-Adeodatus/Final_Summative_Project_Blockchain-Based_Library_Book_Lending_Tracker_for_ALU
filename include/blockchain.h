#ifndef ALU_BLOCKCHAIN_H
#define ALU_BLOCKCHAIN_H

#include <time.h>
#include <stdint.h>
#include "crypto.h"
#include "transaction.h"
#include "merkle.h"

#define MAX_RECORDS_PER_BLOCK 16
#define GENESIS_PREV_HASH "0000000000000000000000000000000000000000000000000000000000000000"

typedef struct {
    uint32_t index;
    time_t timestamp;
    int record_count;
    char previous_hash[SHA256_HEX_LEN];
    char merkle_root[SHA256_HEX_LEN];
    char hash[SHA256_HEX_LEN];
    char sealed_by[ID_MAX_LEN * 2];
    double token_reward;
    char transaction_id[SHA256_HEX_LEN];
    uint64_t nonce;
    LendingRecord records[MAX_RECORDS_PER_BLOCK];
} Block;

typedef struct {
    Block *blocks;
    int count;
    int capacity;
    int difficulty;
} Blockchain;

#ifdef __cplusplus
extern "C" {
#endif

/* Global blockchain instance */
extern Blockchain g_blockchain;

/* Initialization */
void blockchain_init(int initial_difficulty);

/* Genesis block creation */
Block* blockchain_create_genesis();

/* Hash calculation for block header fields */
void block_compute_hash(const Block *b, char *out_hash_hex);

/* Proof of work difficulty check (checks for 'difficulty' leading zeros in hex) */
int block_meets_difficulty(const char *hash_hex, int difficulty);

/* Append a confirmed block to chain and apply state transitions */
int blockchain_add_block(const Block *b);

/* Full chain validation and tamper detection */
int blockchain_validate(int verbose);

/* Views */
void blockchain_print_chain();
void blockchain_print_block(int index);
void blockchain_print_lending_history();
void blockchain_print_member_history(const char *member_id);
void blockchain_print_book_history(const char *book_id);

/* Cleanup */
void blockchain_free();

#ifdef __cplusplus
}
#endif

#endif /* ALU_BLOCKCHAIN_H */
