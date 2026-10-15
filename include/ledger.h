#ifndef ALU_LEDGER_H
#define ALU_LEDGER_H

#include <time.h>
#include <stdint.h>
#include "transaction.h"
#include "registry.h"

#define MAX_UTXOS 2048
#define TRANSACTION_FEE 1.0
#define REWARD_ON_TIME 10.0
#define REWARD_LATE 5.0
#define DEFAULT_MINING_REWARD 50.0

typedef enum {
    LEDGER_MODEL_UTXO = 0,
    LEDGER_MODEL_ACCOUNT = 1
} LedgerModel;

typedef struct {
    char tx_id[SHA256_HEX_LEN];
    int output_index;
    char recipient[ID_MAX_LEN];
    double amount;
    int is_spent;
    int block_index;
    time_t created_at;
} UTXO;

typedef struct AccountTxNode {
    char tx_id[SHA256_HEX_LEN];
    char sender[ID_MAX_LEN];
    char recipient[ID_MAX_LEN];
    double amount;
    double fee;
    uint32_t nonce;
    time_t timestamp;
    struct AccountTxNode *next;
} AccountTxNode;

typedef struct {
    char member_id[ID_MAX_LEN];
    AccountTxNode *history_head;
    int tx_count;
} MemberAccountHistory;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize ledger structures */
void ledger_init();

/* Model selection */
void ledger_set_model(LedgerModel model);
LedgerModel ledger_get_model();
const char* ledger_model_to_string(LedgerModel model);

/* Process returns and mining confirmation rewards into balances or UTXOs */
double ledger_process_block_rewards(const LendingRecord *records, int record_count,
                                   const char *miner_id, double miner_reward,
                                   int block_index, const char *tx_id);

/* --- UTXO Model Engine --- */
double ledger_utxo_get_balance(const char *member_id);
int ledger_utxo_transfer(const char *sender_id, const char *recipient_id, double amount, double fee);
void ledger_utxo_view();
int ledger_utxo_create_coinbase(const char *recipient_id, double amount, int block_index, const char *tx_id);
int ledger_get_utxo_count();
const UTXO* ledger_get_utxo(int index);
void ledger_add_loaded_utxo(const UTXO *u);
void ledger_clear_utxos();

/* --- Account-Based Engine --- */
double ledger_account_get_balance(const char *member_id);
int ledger_account_transfer(const char *sender_id, const char *recipient_id, double amount, double fee, uint32_t nonce);
uint32_t ledger_account_get_nonce(const char *member_id);
void ledger_account_print_history(const char *member_id);
void ledger_account_credit(const char *member_id, double amount);

/* --- High-level Unified Commands --- */
double ledger_get_balance(const char *member_id);
int ledger_transfer(const char *sender_id, const char *recipient_id, double amount);

/* Cleanup */
void ledger_free_all();

#ifdef __cplusplus
}
#endif

#endif /* ALU_LEDGER_H */
