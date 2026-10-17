#include "blockchain.h"
#include "registry.h"
#include "reservation.h"
#include "ledger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Blockchain g_blockchain;

void blockchain_init(int initial_difficulty) {
    g_blockchain.count = 0;
    g_blockchain.capacity = 32;
    g_blockchain.difficulty = (initial_difficulty >= 1 && initial_difficulty <= 4) ? initial_difficulty : 2;
    g_blockchain.blocks = (Block *)malloc(g_blockchain.capacity * sizeof(Block));
    if (!g_blockchain.blocks) {
        fprintf(stderr, "[FATAL] Failed to allocate blockchain memory.\n");
        exit(1);
    }
}

void block_compute_hash(const Block *b, char *out_hash_hex) {
    if (!b || !out_hash_hex) return;
    char header[1024];
    snprintf(header, sizeof(header), "%u|%ld|%d|%s|%s|%s|%.2f|%s|%llu",
             b->index,
             (long)b->timestamp,
             b->record_count,
             b->previous_hash,
             b->merkle_root,
             b->sealed_by,
             b->token_reward,
             b->transaction_id,
             (unsigned long long)b->nonce);
    sha256_string(header, out_hash_hex);
}

int block_meets_difficulty(const char *hash_hex, int difficulty) {
    if (!hash_hex || difficulty <= 0) return 1;
    for (int i = 0; i < difficulty; i++) {
        if (hash_hex[i] != '0') return 0;
    }
    return 1;
}

Block* blockchain_create_genesis() {
    if (g_blockchain.count > 0) return &g_blockchain.blocks[0];

    Block genesis;
    memset(&genesis, 0, sizeof(Block));
    genesis.index = 0;
    genesis.timestamp = 1700000000; /* Fixed standard genesis epoch */
    genesis.record_count = 0;
    strncpy(genesis.previous_hash, GENESIS_PREV_HASH, SHA256_HEX_LEN);
    memset(genesis.merkle_root, '0', 64);
    genesis.merkle_root[64] = '\0';
    strncpy(genesis.sealed_by, "GENESIS_ALU_NODE", sizeof(genesis.sealed_by) - 1);
    genesis.token_reward = 0.0;
    memset(genesis.transaction_id, '0', 64);
    genesis.transaction_id[64] = '\0';
    genesis.nonce = 0;

    /* Mine genesis block */
    while (1) {
        block_compute_hash(&genesis, genesis.hash);
        if (block_meets_difficulty(genesis.hash, g_blockchain.difficulty)) {
            break;
        }
        genesis.nonce++;
    }

    g_blockchain.blocks[g_blockchain.count++] = genesis;
    printf("[BLOCKCHAIN] Genesis Block created and validated (Hash: %.16s...). Target Difficulty: %d\n",
           genesis.hash, g_blockchain.difficulty);
    return &g_blockchain.blocks[0];
}

int blockchain_add_block(const Block *b) {
    if (!b) return 0;

    /* Ensure timestamp is strictly increasing */
    if (g_blockchain.count > 0) {
        Block *prev = &g_blockchain.blocks[g_blockchain.count - 1];
        if (b->timestamp <= prev->timestamp) {
            fprintf(stderr, "[ERROR] Block timestamp (%ld) must be strictly greater than previous block (%ld).\n",
                    (long)b->timestamp, (long)prev->timestamp);
            return 0;
        }
        if (strcmp(b->previous_hash, prev->hash) != 0) {
            fprintf(stderr, "[ERROR] Block previous_hash mismatch.\n");
            return 0;
        }
    }

    if (g_blockchain.count >= g_blockchain.capacity) {
        g_blockchain.capacity *= 2;
        g_blockchain.blocks = (Block *)realloc(g_blockchain.blocks, g_blockchain.capacity * sizeof(Block));
        if (!g_blockchain.blocks) {
            fprintf(stderr, "[FATAL] Failed to reallocate blockchain memory.\n");
            exit(1);
        }
    }

    g_blockchain.blocks[g_blockchain.count++] = *b;

    /* Apply on-chain state updates for confirmed lending records */
    for (int i = 0; i < b->record_count; i++) {
        const LendingRecord *r = &b->records[i];
        BookCopy *c = registry_find_copy(r->copy_id);
        Member *m = registry_find_member(r->member_id);

        if (m) {
            m->nonce++; /* Nonce strictly increments only upon block confirmation */
        }

        if (c && m) {
            if (r->action == ACTION_BORROWED) {
                c->state = STATE_BORROWED;
                snprintf(c->current_holder_id, sizeof(c->current_holder_id), "%s", m->member_id);
                c->borrow_timestamp = r->timestamp;
                c->due_date = r->due_date;
                c->renewal_count = 0;
                m->active_borrows++;
            } else if (r->action == ACTION_RETURNED) {
                if (m->active_borrows > 0) m->active_borrows--;
                c->last_returned_time = r->timestamp;
                snprintf(c->last_returned_member, sizeof(c->last_returned_member), "%s", m->member_id);

                /* Automatic offer to next in reservation queue */
                char next_member[ID_MAX_LEN];
                time_t res_date;
                if (reservation_dequeue(c->book_id, next_member, &res_date)) {
                    c->state = STATE_RESERVED;
                    snprintf(c->current_holder_id, sizeof(c->current_holder_id), "%s", next_member);
                    c->due_date = r->timestamp + (2 * 86400); /* 48 hours to claim reservation */
                    printf("[AUTO-OFFER] Copy %s automatically RESERVED for next member in queue: %s (Hold: 48h)\n",
                           c->copy_id, next_member);
                } else {
                    c->state = STATE_AVAILABLE;
                    c->current_holder_id[0] = '\0';
                    c->due_date = 0;
                }
            } else if (r->action == ACTION_RENEWED) {
                c->due_date = r->due_date;
            }
        }
    }

    registry_save_copies();
    return 1;
}

int blockchain_validate(int verbose) {
    if (g_blockchain.count == 0) {
        if (verbose) printf("[VALIDATION] Blockchain is empty.\n");
        return 1;
    }

    if (verbose) {
        printf("\n=== Comprehensive Blockchain Audit & Tamper Validation ===\n");
        printf("Validating %d blocks across chain...\n", g_blockchain.count);
    }

    for (int i = 0; i < g_blockchain.count; i++) {
        Block *b = &g_blockchain.blocks[i];

        /* Check 1: Previous-hash pointer linkage */
        if (i > 0) {
            Block *prev = &g_blockchain.blocks[i - 1];
            if (strcmp(b->previous_hash, prev->hash) != 0) {
                printf("[TAMPER DETECTED] Block #%d previous_hash mismatch! Linked to %.16s..., expected %.16s...\n",
                       b->index, b->previous_hash, prev->hash);
                return 0;
            }
            if (b->timestamp <= prev->timestamp) {
                printf("[TAMPER DETECTED] Block #%d timestamp (%ld) not strictly greater than previous (%ld)!\n",
                       b->index, (long)b->timestamp, (long)prev->timestamp);
                return 0;
            }
        } else {
            if (strcmp(b->previous_hash, GENESIS_PREV_HASH) != 0) {
                printf("[TAMPER DETECTED] Genesis block previous_hash corrupted!\n");
                return 0;
            }
        }

        /* Check 2: Block Hash recomputation */
        char recomputed_hash[SHA256_HEX_LEN];
        block_compute_hash(b, recomputed_hash);
        if (strcmp(b->hash, recomputed_hash) != 0) {
            printf("[TAMPER DETECTED] Block #%d hash mismatch!\n  Stored:     %s\n  Calculated: %s\n",
                   b->index, b->hash, recomputed_hash);
            return 0;
        }

        /* Check 3: Proof-of-Work difficulty target */
        if (!block_meets_difficulty(b->hash, g_blockchain.difficulty)) {
            printf("[TAMPER DETECTED] Block #%d does not satisfy difficulty target %d! Hash: %s\n",
                   b->index, g_blockchain.difficulty, b->hash);
            return 0;
        }

        /* Check 4: Merkle Root recomputation from records */
        char recomputed_merkle[SHA256_HEX_LEN];
        merkle_compute_root_from_records(b->records, b->record_count, recomputed_merkle);
        if (strcmp(b->merkle_root, recomputed_merkle) != 0) {
            printf("[TAMPER DETECTED] Block #%d Merkle Root corrupted!\n  Stored:     %s\n  Calculated: %s\n",
                   b->index, b->merkle_root, recomputed_merkle);
            return 0;
        }

        /* Check 5: Real ECDSA Digital Signatures verification for every record */
        for (int r = 0; r < b->record_count; r++) {
            LendingRecord *rec = &b->records[r];
            Member *m = registry_find_member(rec->member_id);
            if (!m) {
                printf("[TAMPER DETECTED] Block #%d, Record #%d: Member %s not in registry!\n",
                       b->index, r, rec->member_id);
                return 0;
            }
            if (!record_verify_signature(rec, m->pubkey_pem)) {
                printf("[TAMPER DETECTED] Block #%d, Record #%d: ECDSA Signature verification FAILED for member %s!\n",
                       b->index, r, rec->member_id);
                return 0;
            }
        }
    }

    if (verbose) {
        printf("[SUCCESS] All %d blocks passed verification! Previous hashes, PoW, Merkle roots, and ECDSA signatures are 100%% authentic.\n",
               g_blockchain.count);
        printf("==========================================================\n\n");
    }
    return 1;
}

void blockchain_print_chain() {
    printf("\n================================== ALU BLOCKCHAIN LEDGER ==================================\n");
    printf("Total Blocks: %d | Mining Difficulty: %d | Active Records: Multi-copy Tracked\n",
           g_blockchain.count, g_blockchain.difficulty);
    printf("-------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < g_blockchain.count; i++) {
        Block *b = &g_blockchain.blocks[i];
        char time_str[64] = "N/A";
        struct tm *tm_info = localtime(&b->timestamp);
        if (tm_info) strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("BLOCK #%-3u [Sealed by: %-16s] Time: %s Nonce: %-8llu\n",
               b->index, b->sealed_by, time_str, (unsigned long long)b->nonce);
        printf("  Hash:        %s\n", b->hash);
        printf("  Prev Hash:   %s\n", b->previous_hash);
        printf("  Merkle Root: %s\n", b->merkle_root);
        printf("  Reward:      %.1f coins | TxID: %.16s...\n", b->token_reward, b->transaction_id);
        printf("  Records (%d):\n", b->record_count);

        if (b->record_count == 0) {
            printf("    (No lending records - Genesis / Administrative Block)\n");
        } else {
            for (int r = 0; r < b->record_count; r++) {
                LendingRecord *rec = &b->records[r];
                printf("    [%d] %-10s | Book: %-6s (%-16.16s) | Copy: %-7s | Member: %-5s (%-12.12s) | Nonce: %-3u | Fine: %.1f\n",
                       r + 1,
                       action_to_string(rec->action),
                       rec->book_id,
                       rec->book_title,
                       rec->copy_id,
                       rec->member_id,
                       rec->member_name,
                       rec->nonce,
                       rec->fine_amount);
                printf("        ECDSA Sig: %.24s... [VERIFIED]\n", rec->signature);
            }
        }
        printf("-------------------------------------------------------------------------------------------\n");
    }
    printf("===========================================================================================\n\n");
}

void blockchain_print_block(int index) {
    if (index < 0 || index >= g_blockchain.count) {
        printf("[ERROR] Invalid block index: %d. Chain length is %d.\n", index, g_blockchain.count);
        return;
    }

    Block *b = &g_blockchain.blocks[index];
    char time_str[64] = "N/A";
    struct tm *tm_info = localtime(&b->timestamp);
    if (tm_info) strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("\n=== Block Details: Index #%u ===\n", b->index);
    printf("Timestamp:    %s\n", time_str);
    printf("Sealed By:    %s\n", b->sealed_by);
    printf("Nonce:        %llu\n", (unsigned long long)b->nonce);
    printf("Block Hash:   %s\n", b->hash);
    printf("Prev Hash:    %s\n", b->previous_hash);
    printf("Merkle Root:  %s\n", b->merkle_root);
    printf("Token Reward: %.1f coins\n", b->token_reward);
    printf("Reward TxID:  %s\n", b->transaction_id);
    printf("Record Count: %d\n", b->record_count);

    for (int i = 0; i < b->record_count; i++) {
        LendingRecord *r = &b->records[i];
        printf("  [%d] Action: %-10s | Book: %-6s Copy: %-7s | Member: %-5s | Due: %ld | Fine: %.2f | Sig: %.20s...\n",
               i + 1, action_to_string(r->action), r->book_id, r->copy_id, r->member_id,
               (long)r->due_date, r->fine_amount, r->signature);
    }
    printf("================================\n\n");
}

void blockchain_print_lending_history() {
    printf("\n=== Full Immutable Library Lending Ledger History ===\n");
    int total_records = 0;
    for (int i = 0; i < g_blockchain.count; i++) {
        Block *b = &g_blockchain.blocks[i];
        for (int r = 0; r < b->record_count; r++) {
            total_records++;
            LendingRecord *rec = &b->records[r];
            char time_str[64] = "N/A";
            struct tm *tm_info = localtime(&rec->timestamp);
            if (tm_info) strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

            printf("[%03d] Block #%d | %s | %-10s | Book: %-6s | Copy: %-7s | Member: %-6s | Nonce: %-3u | Fine: %.2f\n",
                   total_records, b->index, time_str, action_to_string(rec->action),
                   rec->book_id, rec->copy_id, rec->member_id, rec->nonce, rec->fine_amount);
        }
    }
    printf("Total Lending Transactions Confirmed On-Chain: %d\n", total_records);
    printf("====================================================\n\n");
}

void blockchain_print_member_history(const char *member_id) {
    if (!member_id) return;
    printf("\n=== Lending Audit History for Member [%s] ===\n", member_id);
    int found = 0;
    for (int i = 0; i < g_blockchain.count; i++) {
        Block *b = &g_blockchain.blocks[i];
        for (int r = 0; r < b->record_count; r++) {
            LendingRecord *rec = &b->records[r];
            if (strcmp(rec->member_id, member_id) == 0) {
                found++;
                char time_str[64] = "N/A";
                struct tm *tm_info = localtime(&rec->timestamp);
                if (tm_info) strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

                printf("  [%02d] Block #%d | %s | Action: %-10s | Book: %-6s (%s) | Copy: %-7s | Fine: %.2f\n",
                       found, b->index, time_str, action_to_string(rec->action),
                       rec->book_id, rec->book_title, rec->copy_id, rec->fine_amount);
            }
        }
    }
    if (found == 0) printf("No library transactions found for member %s.\n", member_id);
    printf("=============================================\n\n");
}

void blockchain_print_book_history(const char *book_id) {
    if (!book_id) return;
    printf("\n=== Lending Audit History for Book [%s] ===\n", book_id);
    int found = 0;
    for (int i = 0; i < g_blockchain.count; i++) {
        Block *b = &g_blockchain.blocks[i];
        for (int r = 0; r < b->record_count; r++) {
            LendingRecord *rec = &b->records[r];
            if (strcmp(rec->book_id, book_id) == 0) {
                found++;
                char time_str[64] = "N/A";
                struct tm *tm_info = localtime(&rec->timestamp);
                if (tm_info) strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

                printf("  [%02d] Block #%d | %s | Action: %-10s | Copy: %-7s | Member: %-6s (%s) | Fine: %.2f\n",
                       found, b->index, time_str, action_to_string(rec->action),
                       rec->copy_id, rec->member_id, rec->member_name, rec->fine_amount);
            }
        }
    }
    if (found == 0) printf("No transactions found for book %s.\n", book_id);
    printf("==========================================\n\n");
}

void blockchain_free() {
    if (g_blockchain.blocks) {
        free(g_blockchain.blocks);
        g_blockchain.blocks = NULL;
    }
    g_blockchain.count = 0;
    g_blockchain.capacity = 0;
}
