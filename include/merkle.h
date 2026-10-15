#ifndef ALU_MERKLE_H
#define ALU_MERKLE_H

#include "crypto.h"
#include "transaction.h"

#define MAX_TREE_LEAVES 256

#ifdef __cplusplus
extern "C" {
#endif

/* Computes the Merkle root from an array of SHA-256 leaf hex strings */
void merkle_compute_root(const char leaf_hashes[][SHA256_HEX_LEN], int count, char *root_hex);

/* Computes the Merkle root directly from an array of LendingRecord structures */
void merkle_compute_root_from_records(const LendingRecord *records, int count, char *root_hex);

#ifdef __cplusplus
}
#endif

#endif /* ALU_MERKLE_H */
