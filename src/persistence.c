#include "persistence.h"
#include "blockchain.h"
#include "registry.h"
#include "reservation.h"
#include "pool.h"
#include "ledger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
  #include <io.h>
  #define make_dir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #define make_dir(p) mkdir(p, 0755)
#endif

int persistence_ensure_dir() {
    return make_dir(CHAIN_DATA_DIR);
}

int persistence_file_exists(const char *filepath) {
    if (!filepath) return 0;
    FILE *f = fopen(filepath, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

int persistence_save_all(const char *filepath) {
    persistence_ensure_dir();
    FILE *f = fopen(filepath, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open persistence file for writing: %s\n", filepath);
        return 0;
    }

    /* 1. Metadata */
    fprintf(f, "[METADATA]\n");
    fprintf(f, "difficulty=%d\n", g_blockchain.difficulty);
    fprintf(f, "ledger_model=%s\n", ledger_model_to_string(ledger_get_model()));
    fprintf(f, "block_count=%d\n", g_blockchain.count);
    fprintf(f, "copy_count=%d\n", g_registry.copy_count);
    fprintf(f, "member_count=%d\n", g_registry.member_count);
    fprintf(f, "utxo_count=%d\n", ledger_get_utxo_count());
    fprintf(f, "pool_count=%d\n", g_pool.count);

    /* 2. Members state */
    fprintf(f, "\n[MEMBERS]\n");
    for (int i = 0; i < g_registry.member_count; i++) {
        Member *m = &g_registry.members[i];
        fprintf(f, "%s|%u|%.2f|%.2f|%d\n",
                m->member_id, m->nonce, m->token_balance, m->fines_due, m->active_borrows);
    }

    /* 3. Copies inventory state */
    fprintf(f, "\n[COPIES]\n");
    for (int i = 0; i < g_registry.copy_count; i++) {
        BookCopy *c = &g_registry.copies[i];
        fprintf(f, "%s|%s|%s|%s|%s|%ld|%ld|%d|%ld|%s|%s\n",
                c->copy_id,
                c->book_id,
                condition_to_string(c->condition),
                copy_state_to_string(c->state),
                strlen(c->current_holder_id) > 0 ? c->current_holder_id : "NONE",
                (long)c->borrow_timestamp,
                (long)c->due_date,
                c->renewal_count,
                (long)c->last_returned_time,
                strlen(c->last_returned_member) > 0 ? c->last_returned_member : "NONE",
                condition_to_string(c->last_condition));
    }

    /* 4. Blocks and Lending Records */
    fprintf(f, "\n[BLOCKS]\n");
    for (int i = 0; i < g_blockchain.count; i++) {
        Block *b = &g_blockchain.blocks[i];
        fprintf(f, "BLOCK|%u|%ld|%d|%s|%s|%s|%s|%.2f|%s|%llu\n",
                b->index,
                (long)b->timestamp,
                b->record_count,
                b->previous_hash,
                b->merkle_root,
                b->hash,
                b->sealed_by,
                b->token_reward,
                b->transaction_id,
                (unsigned long long)b->nonce);

        for (int r = 0; r < b->record_count; r++) {
            LendingRecord *rec = &b->records[r];
            fprintf(f, "RECORD|%s|%s|%s|%s|%s|%s|%s|%ld|%ld|%.2f|%u|%s\n",
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
                    rec->nonce,
                    rec->signature);
        }
    }

    /* 5. UTXOs */
    fprintf(f, "\n[UTXOS]\n");
    int utxo_total = ledger_get_utxo_count();
    for (int i = 0; i < utxo_total; i++) {
        const UTXO *u = ledger_get_utxo(i);
        if (u) {
            fprintf(f, "UTXO|%s|%d|%s|%.2f|%d|%d|%ld\n",
                    u->tx_id, u->output_index, u->recipient, u->amount,
                    u->is_spent, u->block_index, (long)u->created_at);
        }
    }

    /* 6. Pending Pool Requests */
    fprintf(f, "\n[POOL]\n");
    for (int i = 0; i < g_pool.count; i++) {
        PendingRequest *req = &g_pool.requests[i];
        fprintf(f, "REQ|%s|%s|%s|%s|%s|%d|%d|%s|%ld\n",
                req->request_id,
                req->book_id,
                req->copy_id,
                req->member_id,
                action_to_string(req->action_requested),
                req->priority,
                (int)req->status,
                strlen(req->suspicious_reason) > 0 ? req->suspicious_reason : "NONE",
                (long)req->created_at);

        LendingRecord *rec = &req->record;
        fprintf(f, "REC|%s|%s|%s|%s|%s|%s|%s|%ld|%ld|%.2f|%u|%s\n",
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
                rec->nonce,
                rec->signature);
    }

    fclose(f);
    printf("[PERSISTENCE] System state successfully serialized to %s.\n", filepath);
    return 1;
}

int persistence_load_all(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char line[1024];
    char section[64] = "";
    Block current_block;
    memset(&current_block, 0, sizeof(Block));
    int in_block = 0;

    /* Temporary pool parsing */
    PendingRequest temp_req;
    memset(&temp_req, 0, sizeof(PendingRequest));
    int in_req = 0;

    ledger_clear_utxos();
    g_pool.count = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_whitespace(line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            if (in_block && g_blockchain.count < g_blockchain.capacity) {
                g_blockchain.blocks[g_blockchain.count++] = current_block;
                in_block = 0;
            }
            snprintf(section, sizeof(section), "%.63s", line);
            continue;
        }

        if (strcmp(section, "[METADATA]") == 0) {
            char key[64], val[64];
            if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
                if (strcmp(key, "difficulty") == 0) {
                    g_blockchain.difficulty = atoi(val);
                } else if (strcmp(key, "ledger_model") == 0) {
                    if (strcmp(val, "ACCOUNT") == 0) {
                        ledger_set_model(LEDGER_MODEL_ACCOUNT);
                    } else {
                        ledger_set_model(LEDGER_MODEL_UTXO);
                    }
                }
            }
        } else if (strcmp(section, "[MEMBERS]") == 0) {
            char mid[32];
            uint32_t nonce;
            double bal, fines;
            int active;
            if (sscanf(line, "%31[^|]|%u|%lf|%lf|%d", mid, &nonce, &bal, &fines, &active) == 5) {
                Member *m = registry_find_member(mid);
                if (m) {
                    m->nonce = nonce;
                    m->token_balance = bal;
                    m->fines_due = fines;
                    m->active_borrows = active;
                }
            }
        } else if (strcmp(section, "[COPIES]") == 0) {
            char cid[32], bid[32], cond[16], state[16], holder[32], last_mem[32], last_c[16];
            long b_time, d_time, r_time;
            int renew;
            if (sscanf(line, "%31[^|]|%31[^|]|%15[^|]|%15[^|]|%31[^|]|%ld|%ld|%d|%ld|%31[^|]|%15s",
                       cid, bid, cond, state, holder, &b_time, &d_time, &renew, &r_time, last_mem, last_c) >= 10) {
                BookCopy *c = registry_find_copy(cid);
                if (c) {
                    c->condition = string_to_condition(cond);
                    c->state = string_to_copy_state(state);
                    if (strcmp(holder, "NONE") != 0) {
                        snprintf(c->current_holder_id, sizeof(c->current_holder_id), "%s", holder);
                    } else {
                        c->current_holder_id[0] = '\0';
                    }
                    c->borrow_timestamp = b_time;
                    c->due_date = d_time;
                    c->renewal_count = renew;
                    c->last_returned_time = r_time;
                    if (strcmp(last_mem, "NONE") != 0) {
                        snprintf(c->last_returned_member, sizeof(c->last_returned_member), "%s", last_mem);
                    } else {
                        c->last_returned_member[0] = '\0';
                    }
                    c->last_condition = string_to_condition(last_c);
                }
            }
        } else if (strcmp(section, "[BLOCKS]") == 0) {
            if (strncmp(line, "BLOCK|", 6) == 0) {
                if (in_block) {
                    g_blockchain.blocks[g_blockchain.count++] = current_block;
                }
                memset(&current_block, 0, sizeof(Block));
                in_block = 1;
                char prev[65], mrk[65], hsh[65], sby[64], txid[65];
                uint32_t idx;
                long t;
                int rcnt;
                double rew;
                unsigned long long nnc;
                if (sscanf(line + 6, "%u|%ld|%d|%64[^|]|%64[^|]|%64[^|]|%63[^|]|%lf|%64[^|]|%llu",
                           &idx, &t, &rcnt, prev, mrk, hsh, sby, &rew, txid, &nnc) == 10) {
                    current_block.index = idx;
                    current_block.timestamp = t;
                    current_block.record_count = 0; /* Will populate via RECORD lines */
                    snprintf(current_block.previous_hash, sizeof(current_block.previous_hash), "%s", prev);
                    snprintf(current_block.merkle_root, sizeof(current_block.merkle_root), "%s", mrk);
                    snprintf(current_block.hash, sizeof(current_block.hash), "%s", hsh);
                    snprintf(current_block.sealed_by, sizeof(current_block.sealed_by), "%s", sby);
                    current_block.token_reward = rew;
                    snprintf(current_block.transaction_id, sizeof(current_block.transaction_id), "%s", txid);
                    current_block.nonce = nnc;
                }
            } else if (strncmp(line, "RECORD|", 7) == 0 && in_block) {
                if (current_block.record_count < MAX_RECORDS_PER_BLOCK) {
                    LendingRecord *rec = &current_block.records[current_block.record_count++];
                    char act_str[32];
                    long t_act, t_due;
                    unsigned int nnc;
                    sscanf(line + 7, "%63[^|]|%31[^|]|%31[^|]|%127[^|]|%31[^|]|%127[^|]|%31[^|]|%ld|%ld|%lf|%u|%255s",
                           rec->record_id, rec->book_id, rec->copy_id, rec->book_title,
                           rec->member_id, rec->member_name, act_str, &t_act, &t_due,
                           &rec->fine_amount, &nnc, rec->signature);
                    rec->action = string_to_action(act_str);
                    rec->timestamp = t_act;
                    rec->due_date = t_due;
                    rec->nonce = nnc;
                }
            }
        } else if (strcmp(section, "[UTXOS]") == 0) {
            if (strncmp(line, "UTXO|", 5) == 0) {
                UTXO u;
                memset(&u, 0, sizeof(UTXO));
                long t;
                sscanf(line + 5, "%64[^|]|%d|%31[^|]|%lf|%d|%d|%ld",
                       u.tx_id, &u.output_index, u.recipient, &u.amount,
                       &u.is_spent, &u.block_index, &t);
                u.created_at = t;
                ledger_add_loaded_utxo(&u);
            }
        } else if (strcmp(section, "[POOL]") == 0) {
            if (strncmp(line, "REQ|", 4) == 0) {
                if (in_req && g_pool.count < MAX_POOL_REQUESTS) {
                    g_pool.requests[g_pool.count++] = temp_req;
                }
                memset(&temp_req, 0, sizeof(PendingRequest));
                in_req = 1;
                char act[32], reason[128];
                long t_cr;
                int prio, stat;
                sscanf(line + 4, "%63[^|]|%31[^|]|%31[^|]|%31[^|]|%31[^|]|%d|%d|%127[^|]|%ld",
                       temp_req.request_id, temp_req.book_id, temp_req.copy_id, temp_req.member_id,
                       act, &prio, &stat, reason, &t_cr);
                temp_req.action_requested = string_to_action(act);
                temp_req.priority = prio;
                temp_req.status = (RequestStatus)stat;
                if (strcmp(reason, "NONE") != 0) {
                    snprintf(temp_req.suspicious_reason, sizeof(temp_req.suspicious_reason), "%s", reason);
                }
                temp_req.created_at = t_cr;
            } else if (strncmp(line, "REC|", 4) == 0 && in_req) {
                LendingRecord *rec = &temp_req.record;
                char act_str[32];
                long t_act, t_due;
                unsigned int nnc;
                sscanf(line + 4, "%63[^|]|%31[^|]|%31[^|]|%127[^|]|%31[^|]|%127[^|]|%31[^|]|%ld|%ld|%lf|%u|%255s",
                       rec->record_id, rec->book_id, rec->copy_id, rec->book_title,
                       rec->member_id, rec->member_name, act_str, &t_act, &t_due,
                       &rec->fine_amount, &nnc, rec->signature);
                rec->action = string_to_action(act_str);
                rec->timestamp = t_act;
                rec->due_date = t_due;
                rec->nonce = nnc;
            }
        }
    }

    if (in_block && g_blockchain.count < g_blockchain.capacity) {
        g_blockchain.blocks[g_blockchain.count++] = current_block;
    }
    if (in_req && g_pool.count < MAX_POOL_REQUESTS) {
        g_pool.requests[g_pool.count++] = temp_req;
    }

    fclose(f);
    printf("[PERSISTENCE] Successfully restored %d blocks, %d UTXOs, %d pending requests from %s.\n",
           g_blockchain.count, ledger_get_utxo_count(), g_pool.count, filepath);
    return 1;
}
