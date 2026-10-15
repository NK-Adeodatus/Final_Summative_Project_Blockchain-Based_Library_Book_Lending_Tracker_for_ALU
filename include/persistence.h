#ifndef ALU_PERSISTENCE_H
#define ALU_PERSISTENCE_H

#define CHAIN_DATA_DIR "chain_data"
#define CHAIN_DATA_FILE "chain_data/blockchain.dat"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize persistence directory */
int persistence_ensure_dir();

/* Save all system state to disk */
int persistence_save_all(const char *filepath);

/* Load all system state from disk */
int persistence_load_all(const char *filepath);

/* Check if persistence file exists */
int persistence_file_exists(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* ALU_PERSISTENCE_H */
