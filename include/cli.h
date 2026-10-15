#ifndef ALU_CLI_H
#define ALU_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize and run interactive REPL loop */
void cli_run();

/* Help menu */
void cli_print_help();

#ifdef __cplusplus
}
#endif

#endif /* ALU_CLI_H */
