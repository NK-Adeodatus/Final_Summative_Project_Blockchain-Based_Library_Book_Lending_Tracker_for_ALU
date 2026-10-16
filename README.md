# ALU Blockchain-Based Library Book Lending Tracker

A decentralized, immutable, and transparent library book lending and token reward ledger built in C (C99) for the African Leadership University (ALU).

---

## Table of Contents
- [Features](#features)
- [Directory Layout](#directory-layout)
- [Prerequisites & Dependencies](#prerequisites--dependencies)
- [Compilation & Execution](#compilation--execution)
- [Automated Verification Suite](#automated-verification-suite)
- [CLI Command Reference](#cli-command-reference)
- [Ledger Models (UTXO vs. Account)](#ledger-models-utxo-vs-account)
- [Proof-of-Work & Mining Simulations](#proof-of-work--mining-simulations)
- [Security & ECDSA Signatures](#security--ecdsa-signatures)
- [Disk Persistence & Tamper Detection](#disk-persistence--tamper-detection)

---

## Features

1. **Blockchain Data Structures**:
   - Chained blocks with SHA-256 hash pointers, timestamps, nonces, and miner tags.
   - **Custom Merkle Tree built from scratch** without external libraries.
   - Real **ECDSA (`secp256k1`)** digital signatures on every lending transaction.
2. **Copy-Level Inventory Tracking**:
   - Availability state machine (`AVAILABLE`, `BORROWED`, `RESERVED`, `LOST`).
   - Condition tracking (`NEW`, `GOOD`, `WORN`, `LOST`).
   - Librarian loss override with audit reason codes.
3. **FIFO Reservation Queue & Fines**:
   - FIFO title queue when all copies are on loan.
   - **Automatic copy allocation** to waiting members upon book return.
   - 14-day loan lifecycle, 7-day renewals (max 2 renewals).
   - Overdue scanning and fine settlement.
4. **Dual Token Ledger Engines**:
   - Return rewards: **10 coins** (on-time), **5 coins** (late), **0 coins** (unreturned).
   - **UTXO Model**: Unspent outputs, fixed transaction fees (1.0 coin), change outputs, double-spend rejection.
   - **Account Model**: Direct balance accounting, per-account nonces, linked-list transaction history.
5. **Mining Simulations (Proof of Work)**:
   - Configurable difficulty (1 to 4 leading hex zeros).
   - **Solo Mining**: 50-coin block reward confirmed to single miner.
   - **Pool Mining**: Proportional reward sharing among $N$ miners with a 2% pool fee.
   - **Cloud Mining**: Multi-round rental contract simulation with profitability tracking.
6. **Misuse & Fraud Detection Heuristics**:
   - Flags excessive borrows ($>3$), rapid re-borrows ($<24\text{h}$), duplicate IDs, and suspicious condition downgrades.
   - Suspicious requests are withheld from mining until approved or rejected via `fraud_review`.
7. **Disk Persistence & Tamper Detection**:
   - Full ledger and cryptographic keys saved to disk across sessions.
   - Automatic tamper audit validating block hashes, PoW targets, Merkle roots, and ECDSA signatures on reload.

---

## Directory Layout

```text
├── Makefile                     # Cross-platform build script (Linux / macOS / WSL / Windows)
├── README.md                    # Project documentation and CLI guide
├── data/
│   ├── books.txt                # Initial catalog registry (ID|Title|Author)
│   ├── members.txt              # Initial student registry (ID|Name|Course)
│   └── copies.txt               # Physical copy inventory (CopyID|BookID|Condition)
├── include/
│   ├── blockchain.h             # Block headers, chain validation, and history logs
│   ├── cli.h                    # Interactive CLI REPL
│   ├── crypto.h                 # SHA-256 and ECDSA (secp256k1) key management and signing
│   ├── ledger.h                 # Dual UTXO and Account-based token ledgers
│   ├── merkle.h                 # Merkle tree construction from scratch
│   ├── mining.h                 # Solo, Pool (2% fee table), and Cloud mining simulations
│   ├── persistence.h            # Plain-text structured disk persistence
│   ├── pool.h                   # Prioritized pending pool and fraud heuristics
│   ├── registry.h               # Book, member, and copy inventory models
│   ├── reservation.h            # Title-level FIFO reservation queues
│   └── transaction.h            # Lending record structure and serialization
└── src/
    ├── blockchain.c
    ├── cli.c
    ├── crypto.c
    ├── ledger.c
    ├── main.c                   # Entry point and auto-reload orchestration
    ├── merkle.c
    ├── mining.c
    ├── persistence.c
    ├── pool.c
    ├── registry.c
    ├── reservation.c
    └── transaction.c
```

---

## Prerequisites & Dependencies

The project is written in standard C (C99) and requires **OpenSSL** (`libssl` and `libcrypto`):

### Linux (Ubuntu / Debian / WSL)
```bash
sudo apt update
sudo apt install -y build-essential libssl-dev pkg-config
```

### macOS
```bash
brew install openssl
```

### Windows (Native MinGW-w64 / MSYS2)
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-openssl make
```

---

## Compilation & Execution

The unified `Makefile` automatically detects your operating system and configures paths accordingly:

```bash
# 1. Clean previous build artifacts
make clean

# 2. Compile executable
make

# 3. Launch interactive CLI
./alu_blockchain_library
```

---


## CLI Command Reference

### Membership Management
| Command | Description |
| :--- | :--- |
| `register_member <id> <name> <course>` | Register student and generate ECDSA keypair |
| `view_member [id]` | Display member profile and on-chain public key |
| `member_nonce <id>` | Display replay-protection sequence nonce |

### Book & Copy Management
| Command | Description |
| :--- | :--- |
| `register_book <id> <title> <author>` | Register book title into catalog |
| `view_book [id]` | Display catalog titles and availability |
| `add_copy <copy_id> <book_id> <cond>` | Add physical copy (`NEW`, `GOOD`, `WORN`) |
| `view_copy [copy_id]` | Display inventory states and active holders |
| `mark_lost <copy_id> <reason_code>` | Librarian override to mark copy `LOST` |

### Lending Operations
| Command | Description |
| :--- | :--- |
| `borrow_book <book_id> <copy_id> <member>` | Sign and submit borrow request to pool |
| `return_book <copy_id> <member> [cond] [rc]` | Sign and submit return request (awards coins) |
| `renew_book <copy_id> <member_id>` | Extend active loan by 7 days (max 2 renewals) |
| `reserve_book <book_id> <member_id>` | Enqueue for unavailable book title |
| `reservation_status [book_id]` | View FIFO reservation queues |

### Fines & Overdue
| Command | Description |
| :--- | :--- |
| `check_overdue` | Scan borrowed copies, flag overdue, accrue fines |
| `view_fines [member_id]` | View outstanding overdue fines |
| `settle_fine <member_id> <amount>` | Settle overdue fines using reward tokens |

### Token & Ledger Operations
| Command | Description |
| :--- | :--- |
| `token_balance <member_id>` | Check balance in active model |
| `token_transfer <from> <to> <amount>` | Transfer tokens with fee deduction |
| `utxo_view` | Inspect active unspent transaction output set |
| `account_balance <member_id>` | Check balance in Account model |
| `account_transfer <from> <to> <amount>` | Direct transfer with account nonce check |
| `account_nonce <member_id>` | View outgoing nonce for Account model |
| `transaction_history <member_id>` | Traverses linked-list history log |
| `switch_model <UTXO\|ACCOUNT>` | Switch active transaction engine |

### Blockchain & Mining Operations
| Command | Description |
| :--- | :--- |
| `pool_view` | View prioritized pending requests and flagged items |
| `mine_solo [miner_id]` | Confirm blocks via Solo Proof of Work |
| `mine_pool [num_miners]` | Mine as pool with 2% fee and share table |
| `mine_cloud <rounds> [renter_id]` | Simulate rented cloud contract (1–5 rounds) |
| `chain_view` | Display blocks, Merkle roots, and signatures |
| `validate_chain` | Run deep cryptographic audit across full chain |
| `chain_save` | Serialize current state to `chain_data/blockchain.dat` |
| `chain_load` | Reload full state from disk and validate |
| `difficulty_status` | View current PoW difficulty target (1–4) |
| `set_difficulty <1-4>` | Adjust mining difficulty level |

### Fraud & Audit Operations
| Command | Description |
| :--- | :--- |
| `fraud_review` | Inspect requests flagged by misuse heuristics |
| `approve_suspicious <request_id>` | Librarian override to approve flagged request |
| `reject_suspicious <request_id>` | Librarian rejection to discard flagged request |
| `lending_history` | Complete immutable audit trail of lending events |
| `member_history <member_id>` | On-chain lending history for specific student |
| `book_history <book_id>` | On-chain lending history for specific book |

---

## Ledger Models (UTXO vs. Account)

To switch between ledger engines at runtime:
```text
ALU-Library> switch_model UTXO
ALU-Library> switch_model ACCOUNT
```

- **UTXO Model (Default)**: Tracks discrete unspent transaction outputs. Transfers require specifying input UTXOs, automatically deduct a 1.0 coin fee, and return any excess as a change UTXO back to the sender. Double-spending is prevented by validating `is_spent`.
- **Account Model**: Maintains direct member balances with replay protection via sequential nonces. Out-of-order or reused nonces are rejected. Full transaction history is recorded in an in-memory linked list per account.

---

## Proof-of-Work & Mining Simulations

Mining confirms pending lending requests into permanent blocks:
- **Solo Mining**: `mine_solo Miner_Alice`
  Computes PoW for top-priority requests and awards the 50-coin block reward to the designated miner.
- **Pool Mining**: `mine_pool 4`
  Simulates 4 miners with variable hash rates, deducts a 2% pool fee (1.0 coin), and renders a distribution table showing attempts, percentage shares, and net rewards.
- **Cloud Mining**: `mine_cloud 3 M001`
  Simulates a 3-round hash rental contract (15.0 coins/round), displaying round-by-round and cumulative profit/loss with warning triggers if fees exceed returns.

---

## Security & ECDSA Signatures

All actions committed to the chain are authenticated with **real ECDSA signatures** over the `secp256k1` curve:
1. Keys are persisted in `chain_data/keys/<member_id>_priv.pem` and `<member_id>_pub.pem`.
2. Every request is signed before entering the pool.
3. Every block computes a custom **Merkle root** from leaf transactions.
4. `validate_chain` cryptographically verifies previous-hash linkage, PoW difficulty targets, Merkle roots, and ECDSA signatures across all blocks.

---
