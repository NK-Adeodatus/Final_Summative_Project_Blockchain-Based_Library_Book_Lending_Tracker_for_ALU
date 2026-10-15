#ifndef ALU_REGISTRY_H
#define ALU_REGISTRY_H

#include <time.h>
#include <stdint.h>
#include "crypto.h"
#include "transaction.h"

#define MAX_BOOKS 256
#define MAX_MEMBERS 256
#define MAX_COPIES 512
#define MAX_BORROWS_PER_MEMBER 3
#define MAX_RENEWALS_PER_COPY 2
#define DEFAULT_LOAN_DURATION_SECONDS (14 * 86400) // 14 days
#define RENEWAL_EXTENSION_SECONDS (7 * 86400)      // 7 days
#define FINE_PER_DAY 1.0                           // 1 coin per day late

typedef enum {
    COND_LOST = 0,
    COND_WORN = 1,
    COND_GOOD = 2,
    COND_NEW  = 3
} CopyCondition;

typedef enum {
    STATE_AVAILABLE = 0,
    STATE_BORROWED,
    STATE_RESERVED,
    STATE_LOST
} CopyState;

typedef struct {
    char book_id[ID_MAX_LEN];
    char title[STR_MAX_LEN];
    char author[STR_MAX_LEN];
} Book;

typedef struct {
    char member_id[ID_MAX_LEN];
    char full_name[STR_MAX_LEN];
    char course_code[ID_MAX_LEN];
    char privkey_pem[PRIVKEY_PEM_MAX_LEN];
    char pubkey_pem[PUBKEY_PEM_MAX_LEN];
    uint32_t nonce;
    double token_balance;
    double fines_due;
    int active_borrows;
} Member;

typedef struct {
    char copy_id[ID_MAX_LEN];
    char book_id[ID_MAX_LEN];
    CopyCondition condition;
    CopyState state;
    char current_holder_id[ID_MAX_LEN];
    time_t borrow_timestamp;
    time_t due_date;
    int renewal_count;
    time_t last_returned_time;
    char last_returned_member[ID_MAX_LEN];
    CopyCondition last_condition;
} BookCopy;

typedef struct {
    Book books[MAX_BOOKS];
    int book_count;

    Member members[MAX_MEMBERS];
    int member_count;

    BookCopy copies[MAX_COPIES];
    int copy_count;

    char books_filepath[256];
    char members_filepath[256];
    char copies_filepath[256];
} Registry;

#ifdef __cplusplus
extern "C" {
#endif

/* Global registry instance */
extern Registry g_registry;

/* String helpers for condition and state */
const char* condition_to_string(CopyCondition cond);
CopyCondition string_to_condition(const char *str);
const char* copy_state_to_string(CopyState state);
CopyState string_to_copy_state(const char *str);

/* Initialize and load registries from data files */
int registry_init(const char *books_file, const char *members_file, const char *copies_file);

/* Lookups */
Book* registry_find_book(const char *book_id);
Member* registry_find_member(const char *member_id);
BookCopy* registry_find_copy(const char *copy_id);
BookCopy* registry_find_available_copy(const char *book_id);

/* Add new items with file appending */
int registry_add_book(const char *book_id, const char *title, const char *author);
int registry_add_member(const char *member_id, const char *full_name, const char *course_code);
int registry_add_copy(const char *copy_id, const char *book_id, CopyCondition cond);

/* State transitions */
int registry_mark_lost(const char *copy_id, const char *reason_code);

/* Save updated copy states back to file */
int registry_save_copies();

/* Trim whitespace and CRLF */
void trim_whitespace(char *str);

#ifdef __cplusplus
}
#endif

#endif /* ALU_REGISTRY_H */
