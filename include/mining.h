#ifndef ALU_MINING_H
#define ALU_MINING_H

#include "blockchain.h"
#include "pool.h"
#include "ledger.h"

#define DEFAULT_MINER_ID "Miner_Solo_1"
#define MAX_POOL_MINERS 10
#define CLOUD_FEE_PER_ROUND 15.0

#ifdef __cplusplus
extern "C" {
#endif

/* Adjust Proof-of-Work difficulty target (1 to 4) */
int mining_set_difficulty(int diff);
int mining_get_difficulty();

/* 1. Solo Mining */
int mining_solo(const char *miner_id);

/* 2. Pool Mining with 2% fee deduction and proportional reward distribution table */
int mining_pool(int num_miners);

/* 3. Cloud Mining simulation with rental rounds, profit/loss tracking, and warning */
int mining_cloud(int rounds, const char *renter_id);

#ifdef __cplusplus
}
#endif

#endif /* ALU_MINING_H */
