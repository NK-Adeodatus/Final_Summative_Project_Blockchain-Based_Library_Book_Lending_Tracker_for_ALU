#include "merkle.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void merkle_compute_root(const char leaf_hashes[][SHA256_HEX_LEN], int count, char *root_hex) {
    if (!root_hex) return;

    if (count <= 0) {
        /* Empty tree root representation: 64 zeros */
        memset(root_hex, '0', 64);
        root_hex[64] = '\0';
        return;
    }

    if (count == 1) {
        strncpy(root_hex, leaf_hashes[0], SHA256_HEX_LEN);
        root_hex[64] = '\0';
        return;
    }

    /* Allocate buffer for iterative tree reduction */
    int current_count = count;
    char current_hashes[MAX_TREE_LEAVES * 2][SHA256_HEX_LEN];

    for (int i = 0; i < current_count && i < MAX_TREE_LEAVES; i++) {
        strncpy(current_hashes[i], leaf_hashes[i], SHA256_HEX_LEN);
        current_hashes[i][64] = '\0';
    }

    while (current_count > 1) {
        /* If odd number of elements, duplicate the last element */
        if (current_count % 2 != 0) {
            strncpy(current_hashes[current_count], current_hashes[current_count - 1], SHA256_HEX_LEN);
            current_hashes[current_count][64] = '\0';
            current_count++;
        }

        int next_count = current_count / 2;
        char next_hashes[MAX_TREE_LEAVES][SHA256_HEX_LEN];

        for (int i = 0; i < next_count; i++) {
            char combined[SHA256_HEX_LEN * 2 + 1];
            snprintf(combined, sizeof(combined), "%s%s", current_hashes[2 * i], current_hashes[2 * i + 1]);
            sha256_string(combined, next_hashes[i]);
        }

        for (int i = 0; i < next_count; i++) {
            strncpy(current_hashes[i], next_hashes[i], SHA256_HEX_LEN);
            current_hashes[i][64] = '\0';
        }
        current_count = next_count;
    }

    strncpy(root_hex, current_hashes[0], SHA256_HEX_LEN);
    root_hex[64] = '\0';
}

void merkle_compute_root_from_records(const LendingRecord *records, int count, char *root_hex) {
    if (!records || count <= 0) {
        merkle_compute_root(NULL, 0, root_hex);
        return;
    }

    char leaf_hashes[MAX_TREE_LEAVES][SHA256_HEX_LEN];
    int leaf_count = (count > MAX_TREE_LEAVES) ? MAX_TREE_LEAVES : count;

    for (int i = 0; i < leaf_count; i++) {
        record_compute_hash(&records[i], leaf_hashes[i]);
    }

    merkle_compute_root((const char (*)[SHA256_HEX_LEN])leaf_hashes, leaf_count, root_hex);
}
