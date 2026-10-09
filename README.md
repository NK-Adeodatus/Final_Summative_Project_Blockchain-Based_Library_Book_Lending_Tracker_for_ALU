# Blockchain-Based Library Book Lending Tracker

This project is a complete C implementation of a blockchain-based lending ledger for a university library, designed as the summative assessment for the Software Engineering Low-Level Specialization at ALU.

## Compilation Instructions

The project uses a standard `Makefile` configured for Linux environments (like WSL).

1. Open your WSL or Linux terminal.
2. Navigate to the project root directory.
3. Run `make` to build the executable.

```bash
make clean
make
```

The output executable will be named `alu_blockchain_library`. You can run it with:
```bash
./alu_blockchain_library
```

## Ledger Models (UTXO vs Account)

The system supports two transaction models for managing token rewards:

1. **UTXO Model (Default):** Tracks unspent transaction outputs.
2. **Account Model:** Direct balance tracking with nonce-based replay protection.

You can switch between them at runtime in the CLI:
```text
ALU-Library> switch_model UTXO
ALU-Library> switch_model ACCOUNT
```

## Mining Difficulty

The Proof-of-Work difficulty is adjustable. It defaults to 2 (hash must start with two '0's).
To view or change the difficulty:
```text
ALU-Library> difficulty_status
ALU-Library> set_difficulty 3
```

## Mining Simulations

You can test three types of mining to confirm pending transactions into a block:

1. **Solo Mining:** One miner earns the full block reward.
   ```text
   ALU-Library> mine_solo
   ```
2. **Pool Mining:** Simulates multiple miners sharing the reward proportionally to their simulated hash power, deducting a 2% pool fee.
   ```text
   ALU-Library> mine_pool 4
   ```
   *(Where 4 is the number of simulated miners)*
3. **Cloud Mining:** Rent hash power for a given number of rounds and calculate profitability vs fees.
   ```text
   ALU-Library> mine_cloud 3
   ```
   *(Where 3 is the number of rounds to rent)*

## Dependencies

This project links against the **OpenSSL** library (`libssl` and `libcrypto`).
- It uses the OpenSSL `EVP` API for **SHA-256** hashing.
- It uses the OpenSSL `EC_KEY` and `ECDSA` APIs for real cryptographic signatures over the **secp256k1** curve.

To compile successfully, ensure OpenSSL development headers are installed on your Linux/WSL system (e.g., `sudo apt install libssl-dev`).

## Usage Data

Sample data files are provided in the `data/` directory:
- `books.txt`: Register of library titles
- `members.txt`: Register of students/members
- `copies.txt`: Multi-copy inventory mapping to books

Upon exit using the `exit` command, the chain state is persisted to `chain_data/blockchain.dat`.
