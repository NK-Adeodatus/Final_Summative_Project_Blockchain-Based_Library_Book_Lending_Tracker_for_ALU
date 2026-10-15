#include "cli.h"
#include "registry.h"
#include "reservation.h"
#include "pool.h"
#include "ledger.h"
#include "blockchain.h"
#include "mining.h"
#include "persistence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cli_print_help() {
    printf("\n============================ ALU BLOCKCHAIN LIBRARY CLI COMMANDS ============================\n");
    printf("--- Membership Management ---\n");
    printf("  register_member <id> <name> <course>    : Register a new student/member with ECDSA keypair\n");
    printf("  view_member [id]                       : View details and keys of a specific or all members\n");
    printf("  member_nonce <id>                      : View current replay-protection nonce for member\n\n");

    printf("--- Book & Copy Management ---\n");
    printf("  register_book <id> <title> <author>    : Register a new library book title\n");
    printf("  view_book [id]                         : View book title details\n");
    printf("  add_copy <copy_id> <book_id> <cond>    : Add physical copy (Condition: NEW, GOOD, WORN)\n");
    printf("  view_copy [copy_id]                    : View physical copy inventory and availability state\n");
    printf("  mark_lost <copy_id> <reason_code>      : Librarian override to mark physical copy LOST\n\n");

    printf("--- Lending Operations ---\n");
    printf("  borrow_book <book_id> <copy_id> <mem>  : Sign and submit BORROW request to pending pool\n");
    printf("  return_book <copy_id> <mem> [cond] [rc]: Sign and submit RETURN request (triggers token reward)\n");
    printf("  renew_book <copy_id> <member_id>       : Request loan renewal (extends due date by 7 days)\n");
    printf("  reserve_book <book_id> <member_id>     : Enter FIFO reservation queue for unavailable book\n");
    printf("  reservation_status [book_id]           : View waiting queues for books\n\n");

    printf("--- Fines & Overdue Lifecycle ---\n");
    printf("  check_overdue                          : Scan borrowed copies, flag overdue, accrue fines\n");
    printf("  view_fines [member_id]                 : View accumulated overdue fines\n");
    printf("  settle_fine <member_id> <amount>       : Pay overdue fines using earned reward tokens\n\n");

    printf("--- Token & Ledger Operations ---\n");
    printf("  token_transfer <from> <to> <amount>    : Transfer tokens using active ledger model\n");
    printf("  token_balance <member_id>              : Check balance in active ledger model\n");
    printf("  utxo_view                              : Inspect active unspent transaction output set\n");
    printf("  account_balance <member_id>            : Inspect balance in Account model\n");
    printf("  account_transfer <from> <to> <amount>  : Direct transfer with account nonce check\n");
    printf("  account_nonce <member_id>              : Check outgoing nonce for Account model\n");
    printf("  transaction_history <member_id>        : Display full linked-list transaction history\n");
    printf("  switch_model <UTXO|ACCOUNT>            : Switch active ledger transaction model\n\n");

    printf("--- Blockchain & Mining Operations ---\n");
    printf("  pool_view                              : View prioritized pending pool and suspicious queue\n");
    printf("  mine_solo [miner_id]                   : Confirm pending blocks via Solo Proof of Work\n");
    printf("  mine_pool [num_miners]                 : Mine as pool with 2%% fee and proportional shares\n");
    printf("  mine_cloud <rounds> [renter_id]        : Simulate rented cloud mining contract (1-5 rounds)\n");
    printf("  chain_view                             : Display all blocks, Merkle roots, and signatures\n");
    printf("  validate_chain                         : Run deep tamper audit (PoW, Merkle, ECDSA, links)\n");
    printf("  chain_save                             : Save full state to chain_data/blockchain.dat\n");
    printf("  chain_load                             : Reload full state from disk and validate\n");
    printf("  difficulty_status                      : Check current PoW difficulty target\n");
    printf("  set_difficulty <1-4>                   : Adjust mining difficulty level\n\n");

    printf("--- Fraud & Audit Operations ---\n");
    printf("  fraud_review                           : Inspect requests flagged by misuse heuristics\n");
    printf("  approve_suspicious <request_id>        : Librarian override to approve suspicious request\n");
    printf("  reject_suspicious <request_id>         : Librarian rejection to discard suspicious request\n");
    printf("  lending_history                        : Complete audit trail of confirmed lending events\n");
    printf("  member_history <member_id>             : On-chain lending history for specific member\n");
    printf("  book_history <book_id>                 : On-chain lending history for specific book\n\n");

    printf("--- General Commands ---\n");
    printf("  help                                   : Show this commands overview\n");
    printf("  exit / quit                            : Save state and exit application\n");
    printf("==============================================================================================\n\n");
}

static void cmd_check_overdue() {
    time_t now = time(NULL);
    printf("\n=== Checking Overdue Copies & Accruing Fines ===\n");
    int overdue_count = 0;
    printf("%-8s | %-6s | %-6s | %-12s | %-10s | %s\n",
           "Copy ID", "Book", "Holder", "Days Overdue", "Fine (Coins)", "Due Date");
    printf("----------------------------------------------------------------------\n");

    for (int i = 0; i < g_registry.copy_count; i++) {
        BookCopy *c = &g_registry.copies[i];
        if (c->state == STATE_BORROWED && c->due_date > 0 && now > c->due_date) {
            overdue_count++;
            double seconds_late = difftime(now, c->due_date);
            int days_late = (int)(seconds_late / 86400) + 1;
            double fine = days_late * FINE_PER_DAY;

            Member *m = registry_find_member(c->current_holder_id);
            if (m) {
                m->fines_due += fine;
            }

            char due_str[64] = "N/A";
            struct tm *tm_info = localtime(&c->due_date);
            if (tm_info) strftime(due_str, sizeof(due_str), "%Y-%m-%d", tm_info);

            printf("%-8s | %-6s | %-6s | %-12d | %-12.2f | %s\n",
                   c->copy_id, c->book_id, c->current_holder_id, days_late, fine, due_str);
        }
    }

    if (overdue_count == 0) {
        printf("All currently borrowed books are within their active loan periods.\n");
    } else {
        printf("Flagged %d overdue copies. Fines have been billed to member profiles.\n", overdue_count);
    }
    printf("======================================================================\n\n");
}

static void cmd_view_fines(const char *member_id) {
    printf("\n=== Member Overdue Fines Status ===\n");
    int count = 0;
    for (int i = 0; i < g_registry.member_count; i++) {
        Member *m = &g_registry.members[i];
        if (!member_id || strcmp(m->member_id, member_id) == 0) {
            count++;
            printf("Member: %-6s (%-16s) | Outstanding Fines: %.2f coins | Token Balance: %.2f coins\n",
                   m->member_id, m->full_name, m->fines_due, ledger_get_balance(m->member_id));
        }
    }
    if (count == 0) printf("No matching members found.\n");
    printf("===================================\n\n");
}

static void cmd_settle_fine(const char *member_id, double amount) {
    if (!member_id || amount <= 0.0) {
        printf("[ERROR] Usage: settle_fine <member_id> <amount>\n");
        return;
    }
    Member *m = registry_find_member(member_id);
    if (!m) {
        printf("[ERROR] Member %s not found.\n", member_id);
        return;
    }

    if (m->fines_due <= 0.0) {
        printf("[INFO] Member %s has no outstanding fines to settle.\n", member_id);
        return;
    }

    double current_balance = ledger_get_balance(member_id);
    if (current_balance < amount) {
        printf("[ERROR] Insufficient token balance to settle fine. Balance: %.2f coins, Settle Amount: %.2f coins.\n",
               current_balance, amount);
        return;
    }

    double settled = (amount > m->fines_due) ? m->fines_due : amount;
    if (ledger_get_model() == LEDGER_MODEL_UTXO) {
        if (!ledger_utxo_transfer(member_id, "LIBRARY_TREASURY", settled, 0.0)) {
            printf("[ERROR] Failed to transfer fine payment.\n");
            return;
        }
    } else {
        m->token_balance -= settled;
    }

    m->fines_due -= settled;
    printf("[SUCCESS] Settled %.2f coins of fine for member %s. Remaining fine due: %.2f coins.\n",
           settled, member_id, m->fines_due);
}

void cli_run() {
    char input[512];
    printf("ALU Blockchain Library Tracker initialized.\nType 'help' to view available commands.\n\n");

    while (1) {
        printf("ALU-Library [%s|Diff:%d]> ", ledger_model_to_string(ledger_get_model()), g_blockchain.difficulty);
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        trim_whitespace(input);
        if (strlen(input) == 0) continue;

        char cmd[64] = "";
        char arg1[128] = "";
        char arg2[128] = "";
        char arg3[128] = "";
        char arg4[128] = "";

        int parsed = sscanf(input, "%63s %127s %127s %127s %127s", cmd, arg1, arg2, arg3, arg4);
        if (parsed <= 0) continue;

        /* Exit */
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            persistence_save_all(CHAIN_DATA_FILE);
            printf("State persisted. Exiting ALU Blockchain Library Tracker. Goodbye!\n");
            break;
        }

        /* Help */
        else if (strcmp(cmd, "help") == 0) {
            cli_print_help();
        }

        /* Membership */
        else if (strcmp(cmd, "register_member") == 0) {
            if (parsed < 4) {
                printf("[ERROR] Usage: register_member <id> <name> <course>\n");
            } else {
                if (registry_add_member(arg1, arg2, arg3)) {
                    printf("[SUCCESS] Member %s (%s) registered with newly generated ECDSA secp256k1 keypair.\n", arg1, arg2);
                } else {
                    printf("[ERROR] Registration failed. Member may already exist or registry is full.\n");
                }
            }
        }
        else if (strcmp(cmd, "view_member") == 0) {
            printf("\n=== Registered Members ===\n");
            int found = 0;
            for (int i = 0; i < g_registry.member_count; i++) {
                Member *m = &g_registry.members[i];
                if (parsed < 2 || strcmp(m->member_id, arg1) == 0) {
                    found++;
                    printf("ID: %-6s | Name: %-18s | Course: %-8s | Nonce: %-3u | Balance: %-8.2f | Borrows: %d/%d\n",
                           m->member_id, m->full_name, m->course_code, m->nonce,
                           ledger_get_balance(m->member_id), m->active_borrows, MAX_BORROWS_PER_MEMBER);
                    printf("  On-Chain Public Key: %.32s...\n", m->pubkey_pem);
                }
            }
            if (found == 0) printf("No member found matching criteria.\n");
            printf("==========================\n\n");
        }
        else if (strcmp(cmd, "member_nonce") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: member_nonce <member_id>\n");
            } else {
                Member *m = registry_find_member(arg1);
                if (m) {
                    printf("Member [%s] %s | Next Valid Nonce: %u\n", m->member_id, m->full_name, m->nonce);
                } else {
                    printf("[ERROR] Member %s not found.\n", arg1);
                }
            }
        }

        /* Books & Copies */
        else if (strcmp(cmd, "register_book") == 0) {
            if (parsed < 4) {
                printf("[ERROR] Usage: register_book <book_id> <title> <author>\n");
            } else {
                if (registry_add_book(arg1, arg2, arg3)) {
                    printf("[SUCCESS] Book %s (%s by %s) registered in library catalog.\n", arg1, arg2, arg3);
                } else {
                    printf("[ERROR] Book ID %s already exists or registry full.\n", arg1);
                }
            }
        }
        else if (strcmp(cmd, "view_book") == 0) {
            printf("\n=== Book Catalog ===\n");
            int found = 0;
            for (int i = 0; i < g_registry.book_count; i++) {
                Book *b = &g_registry.books[i];
                if (parsed < 2 || strcmp(b->book_id, arg1) == 0) {
                    found++;
                    int avail_count = 0, total_copies = 0;
                    for (int c = 0; c < g_registry.copy_count; c++) {
                        if (strcmp(g_registry.copies[c].book_id, b->book_id) == 0) {
                            total_copies++;
                            if (g_registry.copies[c].state == STATE_AVAILABLE) avail_count++;
                        }
                    }
                    printf("ID: %-6s | Title: %-28s | Author: %-22s | Copies Available: %d/%d\n",
                           b->book_id, b->title, b->author, avail_count, total_copies);
                }
            }
            if (found == 0) printf("No books found.\n");
            printf("====================\n\n");
        }
        else if (strcmp(cmd, "add_copy") == 0) {
            if (parsed < 4) {
                printf("[ERROR] Usage: add_copy <copy_id> <book_id> <condition: NEW|GOOD|WORN>\n");
            } else {
                CopyCondition cond = string_to_condition(arg3);
                if (registry_add_copy(arg1, arg2, cond)) {
                    printf("[SUCCESS] Added copy %s for book %s (Condition: %s, State: AVAILABLE).\n",
                           arg1, arg2, condition_to_string(cond));
                } else {
                    printf("[ERROR] Failed to add copy. Check that Book ID %s exists and Copy ID is unique.\n", arg2);
                }
            }
        }
        else if (strcmp(cmd, "view_copy") == 0) {
            printf("\n=== Physical Copy Inventory ===\n");
            int found = 0;
            for (int i = 0; i < g_registry.copy_count; i++) {
                BookCopy *c = &g_registry.copies[i];
                if (parsed < 2 || strcmp(c->copy_id, arg1) == 0) {
                    found++;
                    Book *b = registry_find_book(c->book_id);
                    printf("Copy: %-8s | Book: %-6s (%-16.16s) | Condition: %-5s | State: %-10s | Holder: %s\n",
                           c->copy_id, c->book_id, b ? b->title : "Unknown",
                           condition_to_string(c->condition), copy_state_to_string(c->state),
                           strlen(c->current_holder_id) > 0 ? c->current_holder_id : "NONE");
                }
            }
            if (found == 0) printf("No copies found.\n");
            printf("===============================\n\n");
        }
        else if (strcmp(cmd, "mark_lost") == 0) {
            if (parsed < 3) {
                printf("[ERROR] Usage: mark_lost <copy_id> <reason_code>\n");
            } else {
                if (registry_mark_lost(arg1, arg2)) {
                    printf("[SUCCESS] Copy %s marked as LOST. Reason recorded: %s\n", arg1, arg2);
                } else {
                    printf("[ERROR] Copy %s not found or reason missing.\n", arg1);
                }
            }
        }

        /* Lending */
        else if (strcmp(cmd, "borrow_book") == 0) {
            if (parsed < 4) {
                printf("[ERROR] Usage: borrow_book <book_id> <copy_id> <member_id>\n");
            } else {
                pool_add_borrow_request(arg1, arg2, arg3);
            }
        }
        else if (strcmp(cmd, "return_book") == 0) {
            if (parsed < 3) {
                printf("[ERROR] Usage: return_book <copy_id> <member_id> [condition: NEW|GOOD|WORN|LOST] [override_reason]\n");
            } else {
                CopyCondition cond = (parsed >= 4) ? string_to_condition(arg3) : COND_GOOD;
                const char *reason = (parsed >= 5) ? arg4 : NULL;
                pool_add_return_request(arg1, arg2, cond, reason);
            }
        }
        else if (strcmp(cmd, "renew_book") == 0) {
            if (parsed < 3) {
                printf("[ERROR] Usage: renew_book <copy_id> <member_id>\n");
            } else {
                pool_add_renew_request(arg1, arg2);
            }
        }
        else if (strcmp(cmd, "reserve_book") == 0) {
            if (parsed < 3) {
                printf("[ERROR] Usage: reserve_book <book_id> <member_id>\n");
            } else {
                pool_add_reserve_request(arg1, arg2);
            }
        }
        else if (strcmp(cmd, "reservation_status") == 0) {
            reservation_print_status((parsed >= 2) ? arg1 : NULL);
        }

        /* Fines */
        else if (strcmp(cmd, "check_overdue") == 0) {
            cmd_check_overdue();
        }
        else if (strcmp(cmd, "view_fines") == 0) {
            cmd_view_fines((parsed >= 2) ? arg1 : NULL);
        }
        else if (strcmp(cmd, "settle_fine") == 0) {
            if (parsed < 3) {
                printf("[ERROR] Usage: settle_fine <member_id> <amount>\n");
            } else {
                cmd_settle_fine(arg1, atof(arg2));
            }
        }

        /* Tokens & Ledgers */
        else if (strcmp(cmd, "token_transfer") == 0) {
            if (parsed < 4) {
                printf("[ERROR] Usage: token_transfer <sender_id> <recipient_id> <amount>\n");
            } else {
                ledger_transfer(arg1, arg2, atof(arg3));
            }
        }
        else if (strcmp(cmd, "token_balance") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: token_balance <member_id>\n");
            } else {
                printf("Member [%s] Token Balance (%s model): %.2f coins\n",
                       arg1, ledger_model_to_string(ledger_get_model()), ledger_get_balance(arg1));
            }
        }
        else if (strcmp(cmd, "utxo_view") == 0) {
            ledger_utxo_view();
        }
        else if (strcmp(cmd, "account_balance") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: account_balance <member_id>\n");
            } else {
                printf("Member [%s] Balance (Account Model): %.2f coins\n",
                       arg1, ledger_account_get_balance(arg1));
            }
        }
        else if (strcmp(cmd, "account_transfer") == 0) {
            if (parsed < 4) {
                printf("[ERROR] Usage: account_transfer <sender_id> <recipient_id> <amount>\n");
            } else {
                uint32_t nonce = ledger_account_get_nonce(arg1);
                ledger_account_transfer(arg1, arg2, atof(arg3), TRANSACTION_FEE, nonce);
            }
        }
        else if (strcmp(cmd, "account_nonce") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: account_nonce <member_id>\n");
            } else {
                printf("Member [%s] Account Model Outgoing Nonce: %u\n",
                       arg1, ledger_account_get_nonce(arg1));
            }
        }
        else if (strcmp(cmd, "transaction_history") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: transaction_history <member_id>\n");
            } else {
                ledger_account_print_history(arg1);
            }
        }
        else if (strcmp(cmd, "switch_model") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: switch_model <UTXO|ACCOUNT>\n");
            } else {
                if (strcmp(arg1, "UTXO") == 0) {
                    ledger_set_model(LEDGER_MODEL_UTXO);
                } else if (strcmp(arg1, "ACCOUNT") == 0) {
                    ledger_set_model(LEDGER_MODEL_ACCOUNT);
                } else {
                    printf("[ERROR] Unknown model '%s'. Choose UTXO or ACCOUNT.\n", arg1);
                }
            }
        }

        /* Blockchain & Mining */
        else if (strcmp(cmd, "pool_view") == 0) {
            pool_print_all();
        }
        else if (strcmp(cmd, "mine_solo") == 0) {
            mining_solo((parsed >= 2) ? arg1 : NULL);
        }
        else if (strcmp(cmd, "mine_pool") == 0) {
            int num = (parsed >= 2) ? atoi(arg1) : 4;
            mining_pool(num);
        }
        else if (strcmp(cmd, "mine_cloud") == 0) {
            int rounds = (parsed >= 2) ? atoi(arg1) : 3;
            const char *renter = (parsed >= 3) ? arg2 : "M001";
            mining_cloud(rounds, renter);
        }
        else if (strcmp(cmd, "chain_view") == 0) {
            blockchain_print_chain();
        }
        else if (strcmp(cmd, "validate_chain") == 0) {
            blockchain_validate(1);
        }
        else if (strcmp(cmd, "chain_save") == 0) {
            persistence_save_all(CHAIN_DATA_FILE);
        }
        else if (strcmp(cmd, "chain_load") == 0) {
            persistence_load_all(CHAIN_DATA_FILE);
            blockchain_validate(1);
        }
        else if (strcmp(cmd, "difficulty_status") == 0) {
            printf("Current Proof-of-Work Difficulty Target: %d (Leading zeros required)\n",
                   mining_get_difficulty());
        }
        else if (strcmp(cmd, "set_difficulty") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: set_difficulty <1-4>\n");
            } else {
                mining_set_difficulty(atoi(arg1));
            }
        }

        /* Fraud & Audit */
        else if (strcmp(cmd, "fraud_review") == 0) {
            pool_print_fraud_review();
        }
        else if (strcmp(cmd, "approve_suspicious") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: approve_suspicious <request_id>\n");
            } else {
                pool_approve_suspicious(arg1);
            }
        }
        else if (strcmp(cmd, "reject_suspicious") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: reject_suspicious <request_id>\n");
            } else {
                pool_reject_suspicious(arg1);
            }
        }
        else if (strcmp(cmd, "lending_history") == 0) {
            blockchain_print_lending_history();
        }
        else if (strcmp(cmd, "member_history") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: member_history <member_id>\n");
            } else {
                blockchain_print_member_history(arg1);
            }
        }
        else if (strcmp(cmd, "book_history") == 0) {
            if (parsed < 2) {
                printf("[ERROR] Usage: book_history <book_id>\n");
            } else {
                blockchain_print_book_history(arg1);
            }
        }

        /* Unknown Command */
        else {
            printf("[ERROR] Unknown command: '%s'. Type 'help' to view all commands.\n", cmd);
        }
    }
}
