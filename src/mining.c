#include "mining.h"
#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mining_set_difficulty(int diff) {
    if (diff < 1 || diff > 4) {
        printf("[ERROR] Difficulty must be between 1 and 4.\n");
        return 0;
    }
    g_blockchain.difficulty = diff;
    printf("[MINING] Proof-of-Work difficulty set to: %d (Hash prefix target: ", diff);
    for (int i = 0; i < diff; i++) putchar('0');
    printf(")\n");
    return 1;
}

int mining_get_difficulty() {
    return g_blockchain.difficulty;
}

static int prepare_candidate_block(Block *out_block, PendingRequest *out_batch, int *out_batch_count, const char *sealed_by, double miner_reward) {
    memset(out_block, 0, sizeof(Block));
    out_block->index = g_blockchain.count;
    out_block->timestamp = time(NULL);

    if (g_blockchain.count > 0) {
        Block *prev = &g_blockchain.blocks[g_blockchain.count - 1];
        if (out_block->timestamp <= prev->timestamp) {
            out_block->timestamp = prev->timestamp + 1;
        }
        strncpy(out_block->previous_hash, prev->hash, SHA256_HEX_LEN);
    } else {
        strncpy(out_block->previous_hash, GENESIS_PREV_HASH, SHA256_HEX_LEN);
    }

    strncpy(out_block->sealed_by, sealed_by, sizeof(out_block->sealed_by) - 1);

    /* Fetch top prioritized pending requests (excluding suspicious) */
    *out_batch_count = pool_get_top_requests(out_batch, MAX_RECORDS_PER_BLOCK);
    out_block->record_count = *out_batch_count;

    double reward_sum = miner_reward;
    for (int i = 0; i < *out_batch_count; i++) {
        out_block->records[i] = out_batch[i].record;
        if (out_batch[i].record.action == ACTION_RETURNED) {
            if (out_batch[i].record.timestamp <= out_batch[i].record.due_date) {
                reward_sum += REWARD_ON_TIME;
            } else {
                reward_sum += REWARD_LATE;
            }
        }
    }
    out_block->token_reward = reward_sum;

    /* Pre-compute reward transaction ID so it is included in block hash before mining */
    char tx_payload[512];
    snprintf(tx_payload, sizeof(tx_payload), "REWARD-BLOCK-%u-%ld-%s-%.2f",
             out_block->index, (long)out_block->timestamp, sealed_by, reward_sum);
    sha256_string(tx_payload, out_block->transaction_id);

    /* Compute Merkle Root from records */
    merkle_compute_root_from_records(out_block->records, out_block->record_count, out_block->merkle_root);
    return 1;
}

int mining_solo(const char *miner_id) {
    const char *miner = (miner_id && strlen(miner_id) > 0) ? miner_id : DEFAULT_MINER_ID;
    printf("\n=== Initiating Solo Mining ===\n");
    printf("Miner: %s | Target PoW Difficulty: %d\n", miner, g_blockchain.difficulty);

    Block block;
    PendingRequest batch[MAX_RECORDS_PER_BLOCK];
    int batch_count = 0;
    prepare_candidate_block(&block, batch, &batch_count, miner, DEFAULT_MINING_REWARD);

    printf("Selected %d pending requests from pool for confirmation.\n", batch_count);
    printf("Computing Proof of Work...\n");

    uint64_t attempts = 0;
    block.nonce = 0;
    while (1) {
        attempts++;
        block_compute_hash(&block, block.hash);
        if (block_meets_difficulty(block.hash, g_blockchain.difficulty)) {
            break;
        }
        block.nonce++;
    }

    printf("[SUCCESS] Block #%u solved! Nonce: %llu | Hash Attempts: %llu\n",
           block.index, (unsigned long long)block.nonce, (unsigned long long)attempts);
    printf("Confirmed Block Hash: %s\n", block.hash);
    printf("Merkle Root:          %s\n", block.merkle_root);

    /* Process on-chain ledger rewards */
    ledger_process_block_rewards(block.records, block.record_count, miner,
                                DEFAULT_MINING_REWARD, block.index, block.transaction_id);

    /* Add block and purge confirmed pool requests */
    if (blockchain_add_block(&block)) {
        pool_remove_confirmed(batch, batch_count);
        printf("[CONFIRMED] Block #%u confirmed into blockchain. Solo miner %s credited %.1f coins.\n",
               block.index, miner, DEFAULT_MINING_REWARD);
        ledger_utxo_view();
    }

    printf("==============================\n\n");
    return 1;
}

int mining_pool(int num_miners) {
    if (num_miners < 2) num_miners = 4;
    if (num_miners > MAX_POOL_MINERS) num_miners = MAX_POOL_MINERS;

    printf("\n=== Initiating Mining Pool Simulation ===\n");
    printf("Participating Miners: %d | Target PoW Difficulty: %d | Pool Fee: 2.0%%\n",
           num_miners, g_blockchain.difficulty);

    Block block;
    PendingRequest batch[MAX_RECORDS_PER_BLOCK];
    int batch_count = 0;
    prepare_candidate_block(&block, batch, &batch_count, "Mining_Pool_ALU", DEFAULT_MINING_REWARD);

    /* Simulate miner contributions */
    uint64_t miner_attempts[MAX_POOL_MINERS];
    uint64_t total_attempts = 0;

    for (int i = 0; i < num_miners; i++) {
        miner_attempts[i] = (rand() % 400) + 150;
        total_attempts += miner_attempts[i];
    }

    /* Perform actual PoW */
    block.nonce = 0;
    while (1) {
        block_compute_hash(&block, block.hash);
        if (block_meets_difficulty(block.hash, g_blockchain.difficulty)) {
            break;
        }
        block.nonce++;
    }
    total_attempts += block.nonce;
    miner_attempts[0] += block.nonce; /* Winning miner share increment */

    printf("[SUCCESS] Block #%u successfully mined by pool! Nonce: %llu | Total Attempts: %llu\n",
           block.index, (unsigned long long)block.nonce, (unsigned long long)total_attempts);
    printf("Confirmed Block Hash: %s\n", block.hash);

    /* Deduct 2% pool fee */
    double block_reward = DEFAULT_MINING_REWARD;
    double pool_fee = block_reward * 0.02;
    double net_reward = block_reward - pool_fee;

    /* Credit return rewards to borrowers */
    ledger_process_block_rewards(block.records, block.record_count, NULL, 0.0, block.index, block.transaction_id);

    /* Formatted distribution table */
    printf("\n============================ POOL REWARD DISTRIBUTION TABLE ============================\n");
    printf("%-12s | %-16s | %-16s | %-18s\n", "Miner ID", "Hash Attempts", "Share Percentage", "Reward Received");
    printf("-----------------------------------------------------------------------------------------\n");

    for (int i = 0; i < num_miners; i++) {
        char mid[32];
        snprintf(mid, sizeof(mid), "Miner_%02d", i + 1);
        double share_pct = ((double)miner_attempts[i] / (double)total_attempts) * 100.0;
        double reward_share = ((double)miner_attempts[i] / (double)total_attempts) * net_reward;

        /* Credit in active ledger */
        if (ledger_get_model() == LEDGER_MODEL_UTXO) {
            ledger_utxo_create_coinbase(mid, reward_share, block.index, block.transaction_id);
        } else {
            ledger_account_credit(mid, reward_share);
        }

        printf("%-12s | %-16llu | %-15.2f%% | %-14.2f coins\n",
               mid, (unsigned long long)miner_attempts[i], share_pct, reward_share);
    }

    printf("-----------------------------------------------------------------------------------------\n");
    printf("Gross Block Reward: %.2f coins | Pool Fee (2%%): %.2f coins | Distributed: %.2f coins\n",
           block_reward, pool_fee, net_reward);
    printf("=========================================================================================\n");

    if (blockchain_add_block(&block)) {
        pool_remove_confirmed(batch, batch_count);
    }
    printf("\n");
    return 1;
}

int mining_cloud(int rounds, const char *renter_id) {
    if (rounds < 1) rounds = 1;
    if (rounds > 5) rounds = 5;

    const char *renter = (renter_id && strlen(renter_id) > 0) ? renter_id : "M001";
    printf("\n=============================== CLOUD MINING CONTRACT SIMULATION ===============================\n");
    printf("Renter Account: %s | Duration: %d Rounds | Rental Fee Rate: %.2f coins/round\n",
           renter, rounds, CLOUD_FEE_PER_ROUND);
    printf("-------------------------------------------------------------------------------------------------\n");
    printf("%-7s | %-16s | %-16s | %-16s | %-16s\n",
           "Round", "Rental Fee Paid", "Mining Reward", "Net Round P/L", "Cumulative P/L");
    printf("-------------------------------------------------------------------------------------------------\n");

    double cumulative_fees = 0.0;
    double cumulative_earnings = 0.0;

    for (int r = 1; r <= rounds; r++) {
        double fee = CLOUD_FEE_PER_ROUND;
        cumulative_fees += fee;

        /* Simulated cloud mining return: between 10.0 and 22.0 coins based on simulated luck */
        double round_reward = 11.0 + (rand() % 110) / 10.0;
        cumulative_earnings += round_reward;

        double round_net = round_reward - fee;
        double cumulative_net = cumulative_earnings - cumulative_fees;

        printf("Round %d | %-16.2f | %-16.2f | %-+16.2f | %-+16.2f coins\n",
               r, fee, round_reward, round_net, cumulative_net);

        if (cumulative_fees > cumulative_earnings) {
            printf("  >>> [WARNING] At Round %d: Cumulative fees (%.2f) exceed cumulative rewards (%.2f)!\n",
                   r, cumulative_fees, cumulative_earnings);
        }
    }

    double final_net = cumulative_earnings - cumulative_fees;
    printf("-------------------------------------------------------------------------------------------------\n");
    printf("Contract Summary: Gross Earnings: %.2f coins | Total Rental Fees: %.2f coins | Net Profit: %+.2f coins\n",
           cumulative_earnings, cumulative_fees, final_net);

    if (final_net >= 0) {
        printf("Contract Outcome: PROFITABLE (Net return: +%.2f coins)\n", final_net);
    } else {
        printf("Contract Outcome: UNPROFITABLE (Net loss: %.2f coins). Adjust rental duration or hash difficulty!\n", final_net);
    }
    printf("=================================================================================================\n\n");
    return 1;
}
