#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "registry.h"
#include "reservation.h"
#include "pool.h"
#include "ledger.h"
#include "blockchain.h"
#include "mining.h"
#include "persistence.h"
#include "cli.h"

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));

    printf("================================================================================\n");
    printf("   ALU BLOCKCHAIN-BASED LIBRARY BOOK LENDING TRACKER & TOKEN LEDGER SYSTEM       \n");
    printf("   African Leadership University - Low-Level Software Engineering Specialization \n");
    printf("================================================================================\n\n");

    /* 1. Parse optional CLI difficulty */
    int initial_difficulty = 2;
    if (argc > 1) {
        int d = atoi(argv[1]);
        if (d >= 1 && d <= 4) {
            initial_difficulty = d;
        }
    }

    /* 2. Initialize Subsystems */
    reservation_init();
    pool_init();
    ledger_init();
    blockchain_init(initial_difficulty);

    const char *books_path = "data/books.txt";
    const char *members_path = "data/members.txt";
    const char *copies_path = "data/copies.txt";

    if (!registry_init(books_path, members_path, copies_path)) {
        fprintf(stderr, "[FATAL] Failed to load essential library registry datasets.\n");
        return 1;
    }

    /* 3. Check for Persisted Chain State */
    if (persistence_file_exists(CHAIN_DATA_FILE)) {
        printf("[PERSISTENCE] Found existing ledger state at %s. Restoring...\n", CHAIN_DATA_FILE);
        persistence_load_all(CHAIN_DATA_FILE);
        printf("[AUDIT] Re-validating loaded blockchain before resuming operations...\n");
        if (!blockchain_validate(1)) {
            fprintf(stderr, "[SECURITY ALERT] Loaded blockchain failed tamper validation! Aborting.\n");
            return 1;
        }
    } else {
        printf("[PERSISTENCE] No previous chain file detected. Initializing new ledger...\n");
        blockchain_create_genesis();
    }

    /* 4. Launch Interactive Command-Line Interface */
    cli_run();

    /* 5. Cleanup */
    reservation_free_all();
    ledger_free_all();
    blockchain_free();

    return 0;
}
