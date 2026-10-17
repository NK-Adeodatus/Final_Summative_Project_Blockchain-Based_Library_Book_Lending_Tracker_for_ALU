#include "ledger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LedgerModel s_current_model = LEDGER_MODEL_UTXO;

/* UTXO storage */
static UTXO s_utxos[MAX_UTXOS];
static int s_utxo_count = 0;

/* Account history storage per member */
static MemberAccountHistory s_account_histories[MAX_MEMBERS];
static int s_history_count = 0;
static uint32_t s_tx_counter = 5000;

void ledger_init() {
    s_utxo_count = 0;
    ledger_free_all();
    s_history_count = 0;
}

void ledger_set_model(LedgerModel model) {
    s_current_model = model;
    printf("[LEDGER] Active transaction model switched to: %s\n", ledger_model_to_string(model));
}

LedgerModel ledger_get_model() {
    return s_current_model;
}

const char* ledger_model_to_string(LedgerModel model) {
    switch (model) {
        case LEDGER_MODEL_UTXO:    return "UTXO";
        case LEDGER_MODEL_ACCOUNT: return "ACCOUNT";
        default:                   return "UNKNOWN";
    }
}

static MemberAccountHistory* get_or_create_history(const char *member_id) {
    if (!member_id) return NULL;
    for (int i = 0; i < s_history_count; i++) {
        if (strcmp(s_account_histories[i].member_id, member_id) == 0) {
            return &s_account_histories[i];
        }
    }
    if (s_history_count < MAX_MEMBERS) {
        MemberAccountHistory *h = &s_account_histories[s_history_count++];
        strncpy(h->member_id, member_id, sizeof(h->member_id) - 1);
        h->history_head = NULL;
        h->tx_count = 0;
        return h;
    }
    return NULL;
}

static void record_account_history_node(const char *tx_id, const char *sender, const char *recipient,
                                        double amount, double fee, uint32_t nonce, time_t t) {
    MemberAccountHistory *h_sender = get_or_create_history(sender);
    if (h_sender) {
        AccountTxNode *node = (AccountTxNode *)malloc(sizeof(AccountTxNode));
        if (node) {
            snprintf(node->tx_id, sizeof(node->tx_id), "%s", tx_id);
            snprintf(node->sender, sizeof(node->sender), "%s", sender);
            snprintf(node->recipient, sizeof(node->recipient), "%s", recipient);
            node->amount = amount;
            node->fee = fee;
            node->nonce = nonce;
            node->timestamp = t;
            node->next = h_sender->history_head;
            h_sender->history_head = node;
            h_sender->tx_count++;
        }
    }

    if (strcmp(sender, recipient) != 0) {
        MemberAccountHistory *h_recip = get_or_create_history(recipient);
        if (h_recip) {
            AccountTxNode *node = (AccountTxNode *)malloc(sizeof(AccountTxNode));
            if (node) {
                snprintf(node->tx_id, sizeof(node->tx_id), "%s", tx_id);
                snprintf(node->sender, sizeof(node->sender), "%s", sender);
                snprintf(node->recipient, sizeof(node->recipient), "%s", recipient);
                node->amount = amount;
                node->fee = fee;
                node->nonce = nonce;
                node->timestamp = t;
                node->next = h_recip->history_head;
                h_recip->history_head = node;
                h_recip->tx_count++;
            }
        }
    }
}

int ledger_get_utxo_count() {
    return s_utxo_count;
}

const UTXO* ledger_get_utxo(int index) {
    if (index >= 0 && index < s_utxo_count) return &s_utxos[index];
    return NULL;
}

void ledger_add_loaded_utxo(const UTXO *u) {
    if (u && s_utxo_count < MAX_UTXOS) {
        s_utxos[s_utxo_count++] = *u;
    }
}

void ledger_clear_utxos() {
    s_utxo_count = 0;
}

int ledger_utxo_create_coinbase(const char *recipient_id, double amount, int block_index, const char *tx_id) {
    if (!recipient_id || amount <= 0.0 || s_utxo_count >= MAX_UTXOS) return 0;

    UTXO *u = &s_utxos[s_utxo_count++];
    snprintf(u->tx_id, sizeof(u->tx_id), "%s", tx_id);
    u->output_index = 0;
    u->amount = amount;
    u->is_spent = 0;
    u->block_index = block_index;
    u->created_at = time(NULL);
    snprintf(u->recipient, sizeof(u->recipient), "%s", recipient_id);
    return 1;
}

double ledger_process_block_rewards(const LendingRecord *records, int record_count,
                                   const char *miner_id, double miner_reward,
                                   int block_index, const char *tx_id) {
    double total_coins = 0.0;
    time_t now = time(NULL);

    char active_tx_id[SHA256_HEX_LEN];
    if (tx_id && strlen(tx_id) > 0) {
        snprintf(active_tx_id, sizeof(active_tx_id), "%s", tx_id);
    } else {
        char tx_payload[512];
        snprintf(tx_payload, sizeof(tx_payload), "REWARD-BLOCK-%d-%ld-%u", block_index, (long)now, ++s_tx_counter);
        sha256_string(tx_payload, active_tx_id);
    }

    /* 1. Process Book Return Rewards */
    for (int i = 0; i < record_count; i++) {
        const LendingRecord *r = &records[i];
        if (r->action == ACTION_RETURNED) {
            double reward = 0.0;
            if (r->timestamp <= r->due_date) {
                reward = REWARD_ON_TIME; /* 10 coins for on-time */
            } else {
                reward = REWARD_LATE;    /* 5 coins for late */
            }

            if (reward > 0.0) {
                total_coins += reward;
                if (s_current_model == LEDGER_MODEL_UTXO) {
                    ledger_utxo_create_coinbase(r->member_id, reward, block_index, active_tx_id);
                } else {
                    ledger_account_credit(r->member_id, reward);
                    record_account_history_node(active_tx_id, "SYSTEM_REWARD", r->member_id, reward, 0.0, 0, now);
                }
                printf("[TOKEN REWARD] Member %s credited %.1f coins for returning %s (%s).\n",
                       r->member_id, reward, r->book_id, (r->timestamp <= r->due_date) ? "On-Time" : "Late");
            }
        }
    }

    /* 2. Process Miner Confirmation Reward */
    if (miner_id && strlen(miner_id) > 0 && miner_reward > 0.0) {
        total_coins += miner_reward;
        if (s_current_model == LEDGER_MODEL_UTXO) {
            ledger_utxo_create_coinbase(miner_id, miner_reward, block_index, active_tx_id);
        } else {
            ledger_account_credit(miner_id, miner_reward);
            record_account_history_node(active_tx_id, "SYSTEM_MINING", miner_id, miner_reward, 0.0, 0, now);
        }
    }

    return total_coins;
}

double ledger_utxo_get_balance(const char *member_id) {
    if (!member_id) return 0.0;
    double bal = 0.0;
    for (int i = 0; i < s_utxo_count; i++) {
        if (!s_utxos[i].is_spent && strcmp(s_utxos[i].recipient, member_id) == 0) {
            bal += s_utxos[i].amount;
        }
    }
    return bal;
}

int ledger_utxo_transfer(const char *sender_id, const char *recipient_id, double amount, double fee) {
    if (!sender_id || !recipient_id || amount <= 0.0) {
        printf("[ERROR] Invalid transfer parameters.\n");
        return 0;
    }

    double total_required = amount + fee;
    double accumulated = 0.0;
    int inputs_used[MAX_UTXOS];
    int inputs_count = 0;

    for (int i = 0; i < s_utxo_count; i++) {
        if (!s_utxos[i].is_spent && strcmp(s_utxos[i].recipient, sender_id) == 0) {
            inputs_used[inputs_count++] = i;
            accumulated += s_utxos[i].amount;
            if (accumulated >= total_required) break;
        }
    }

    if (accumulated < total_required) {
        printf("[ERROR] Insufficient UTXO funds. Required: %.2f (Amount %.2f + Fee %.2f), Available: %.2f\n",
               total_required, amount, fee, accumulated);
        return 0;
    }

    /* Mark inputs as spent (Double-spend prevention) */
    for (int i = 0; i < inputs_count; i++) {
        s_utxos[inputs_used[i]].is_spent = 1;
    }

    /* Create new TX ID */
    char tx_id[SHA256_HEX_LEN];
    char raw_tx[256];
    snprintf(raw_tx, sizeof(raw_tx), "TX-UTXO-%s-%s-%.2f-%ld-%u", sender_id, recipient_id, amount, (long)time(NULL), ++s_tx_counter);
    sha256_string(raw_tx, tx_id);

    /* Output 0: Recipient */
    if (s_utxo_count < MAX_UTXOS) {
        UTXO *out0 = &s_utxos[s_utxo_count++];
        snprintf(out0->tx_id, sizeof(out0->tx_id), "%s", tx_id);
        out0->output_index = 0;
        out0->amount = amount;
        out0->is_spent = 0;
        out0->block_index = -1; /* Confirmed in pending transaction */
        out0->created_at = time(NULL);
        snprintf(out0->recipient, sizeof(out0->recipient), "%s", recipient_id);
    }

    /* Output 1: Change output to sender */
    double change = accumulated - total_required;
    if (change > 0.0 && s_utxo_count < MAX_UTXOS) {
        UTXO *out1 = &s_utxos[s_utxo_count++];
        snprintf(out1->tx_id, sizeof(out1->tx_id), "%s", tx_id);
        out1->output_index = 1;
        out1->amount = change;
        out1->is_spent = 0;
        out1->block_index = -1;
        out1->created_at = time(NULL);
        snprintf(out1->recipient, sizeof(out1->recipient), "%s", sender_id);
    }

    printf("[UTXO SUCCESS] Sent %.2f coins to %s (Fee: %.2f, Change: %.2f, Inputs Spent: %d). TxID: %.16s...\n",
           amount, recipient_id, fee, change, inputs_count, tx_id);
    return 1;
}

void ledger_utxo_view() {
    printf("\n=== Current Unspent Transaction Output (UTXO) Set ===\n");
    printf("%-18s | %-6s | %-10s | %-10s | %s\n", "Tx Hash (Prefix)", "OutIdx", "Owner", "Amount", "Status");
    printf("----------------------------------------------------------------------\n");

    int active_count = 0;
    double total_circulation = 0.0;
    for (int i = 0; i < s_utxo_count; i++) {
        if (!s_utxos[i].is_spent) {
            active_count++;
            total_circulation += s_utxos[i].amount;
            char short_tx[17];
            strncpy(short_tx, s_utxos[i].tx_id, 16);
            short_tx[16] = '\0';
            printf("%-18s | %-6d | %-10s | %-10.2f | UNSPENT\n",
                   short_tx, s_utxos[i].output_index, s_utxos[i].recipient, s_utxos[i].amount);
        }
    }
    printf("----------------------------------------------------------------------\n");
    printf("Total Active UTXOs: %d | Total Circulating Coins: %.2f\n\n", active_count, total_circulation);
}

double ledger_account_get_balance(const char *member_id) {
    Member *m = registry_find_member(member_id);
    return m ? m->token_balance : 0.0;
}

uint32_t ledger_account_get_nonce(const char *member_id) {
    Member *m = registry_find_member(member_id);
    return m ? m->nonce : 0;
}

void ledger_account_credit(const char *member_id, double amount) {
    Member *m = registry_find_member(member_id);
    if (m) {
        m->token_balance += amount;
    }
}

int ledger_account_transfer(const char *sender_id, const char *recipient_id, double amount, double fee, uint32_t nonce) {
    if (!sender_id || !recipient_id || amount <= 0.0) {
        printf("[ERROR] Invalid account transfer arguments.\n");
        return 0;
    }

    Member *sender = registry_find_member(sender_id);
    Member *recip = registry_find_member(recipient_id);

    if (!sender || !recip) {
        printf("[ERROR] Sender or recipient not registered.\n");
        return 0;
    }

    /* Replay Protection: Nonce Check */
    if (nonce != sender->nonce) {
        printf("[ERROR] Nonce mismatch! Expected: %u, Got: %u (Transaction rejected for replay protection).\n",
               sender->nonce, nonce);
        return 0;
    }

    double total_cost = amount + fee;
    if (sender->token_balance < total_cost) {
        printf("[ERROR] Insufficient balance. Balance: %.2f, Required: %.2f (Amount %.2f + Fee %.2f)\n",
               sender->token_balance, total_cost, amount, fee);
        return 0;
    }

    /* Apply Balance Updates */
    sender->token_balance -= total_cost;
    recip->token_balance += amount;
    sender->nonce++;

    char tx_id[SHA256_HEX_LEN];
    char raw[256];
    snprintf(raw, sizeof(raw), "TX-ACC-%s-%s-%.2f-%u-%ld", sender_id, recipient_id, amount, nonce, (long)time(NULL));
    sha256_string(raw, tx_id);

    record_account_history_node(tx_id, sender_id, recipient_id, amount, fee, nonce, time(NULL));

    printf("[ACCOUNT SUCCESS] Transferred %.2f coins to %s (Fee: %.2f, New Nonce: %u). TxID: %.16s...\n",
           amount, recipient_id, fee, sender->nonce, tx_id);
    return 1;
}

void ledger_account_print_history(const char *member_id) {
    MemberAccountHistory *h = get_or_create_history(member_id);
    printf("\n=== Account Transaction History for Member [%s] ===\n", member_id);
    if (!h || !h->history_head) {
        printf("No transactions on record for %s.\n", member_id);
        printf("====================================================\n\n");
        return;
    }

    printf("%-18s | %-14s | %-14s | %-8s | %-6s | %-6s | %s\n",
           "TxID", "Sender", "Recipient", "Amount", "Fee", "Nonce", "Timestamp");
    printf("----------------------------------------------------------------------------------------\n");

    AccountTxNode *curr = h->history_head;
    while (curr) {
        char date_str[64] = "N/A";
        struct tm *tm_info = localtime(&curr->timestamp);
        if (tm_info) strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);

        char short_tx[17];
        strncpy(short_tx, curr->tx_id, 16);
        short_tx[16] = '\0';

        printf("%-18s | %-14s | %-14s | %-8.2f | %-6.2f | %-6u | %s\n",
               short_tx, curr->sender, curr->recipient, curr->amount, curr->fee, curr->nonce, date_str);
        curr = curr->next;
    }
    printf("========================================================================================\n\n");
}

double ledger_get_balance(const char *member_id) {
    if (s_current_model == LEDGER_MODEL_UTXO) {
        return ledger_utxo_get_balance(member_id);
    } else {
        return ledger_account_get_balance(member_id);
    }
}

int ledger_transfer(const char *sender_id, const char *recipient_id, double amount) {
    if (s_current_model == LEDGER_MODEL_UTXO) {
        return ledger_utxo_transfer(sender_id, recipient_id, amount, TRANSACTION_FEE);
    } else {
        uint32_t current_nonce = ledger_account_get_nonce(sender_id);
        return ledger_account_transfer(sender_id, recipient_id, amount, TRANSACTION_FEE, current_nonce);
    }
}

void ledger_free_all() {
    for (int i = 0; i < s_history_count; i++) {
        AccountTxNode *curr = s_account_histories[i].history_head;
        while (curr) {
            AccountTxNode *tmp = curr;
            curr = curr->next;
            free(tmp);
        }
        s_account_histories[i].history_head = NULL;
        s_account_histories[i].tx_count = 0;
    }
    s_history_count = 0;
}
