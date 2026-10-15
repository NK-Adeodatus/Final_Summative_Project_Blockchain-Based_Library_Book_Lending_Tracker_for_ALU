#ifndef ALU_POOL_H
#define ALU_POOL_H

#include "transaction.h"
#include "registry.h"

#define MAX_POOL_REQUESTS 512

typedef enum {
    STATUS_PENDING = 0,
    STATUS_CONFIRMED,
    STATUS_SUSPICIOUS
} RequestStatus;

typedef struct {
    char request_id[RECORD_ID_MAX_LEN];
    char book_id[ID_MAX_LEN];
    char copy_id[ID_MAX_LEN];
    char member_id[ID_MAX_LEN];
    LendingAction action_requested;
    int priority; /* Lower number = higher priority; 0 = overdue action, 10+ = standard actions by wait time */
    RequestStatus status;
    char suspicious_reason[STR_MAX_LEN];
    LendingRecord record;
    time_t created_at;
} PendingRequest;

typedef struct {
    PendingRequest requests[MAX_POOL_REQUESTS];
    int count;
} PendingPool;

#ifdef __cplusplus
extern "C" {
#endif

/* Global pending pool instance */
extern PendingPool g_pool;

/* Pool lifecycle */
void pool_init();
const char* request_status_to_string(RequestStatus status);

/* Fraud & Heuristic Checks */
int pool_check_excessive_borrows(const char *member_id);
int pool_check_rapid_reborrow(const char *book_id, const char *member_id);
int pool_check_duplicate_request(const char *request_id);
int pool_check_condition_downgrade(CopyCondition prev_cond, CopyCondition new_cond, const char *override_reason);

/* Add requests to pending pool with automatic heuristic verification */
int pool_add_borrow_request(const char *book_id, const char *copy_id, const char *member_id);
int pool_add_return_request(const char *copy_id, const char *member_id, CopyCondition returned_cond, const char *override_reason);
int pool_add_reserve_request(const char *book_id, const char *member_id);
int pool_add_renew_request(const char *copy_id, const char *member_id);

/* Priority queue sorting: overdue first, then earlier reservation/creation timestamps */
void pool_sort_by_priority();

/* Miner selection: extracts up to max_records confirmed/pending requests */
int pool_get_top_requests(PendingRequest *out_batch, int max_records);

/* Post-mining cleanup: removes confirmed requests from pool */
void pool_remove_confirmed(const PendingRequest *confirmed_batch, int count);

/* Fraud review commands */
void pool_print_fraud_review();
int pool_approve_suspicious(const char *request_id);
int pool_reject_suspicious(const char *request_id);

/* View pool */
void pool_print_all();

#ifdef __cplusplus
}
#endif

#endif /* ALU_POOL_H */
