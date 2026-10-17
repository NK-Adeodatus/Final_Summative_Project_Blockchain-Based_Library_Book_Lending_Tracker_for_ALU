#include "pool.h"
#include "reservation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PendingPool g_pool;
static uint32_t s_request_counter = 1000;

static void safe_str_copy(char *dest, const char *src, size_t dest_size) {
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dest_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void pool_init() {
    memset(&g_pool, 0, sizeof(PendingPool));
}

const char* request_status_to_string(RequestStatus status) {
    switch (status) {
        case STATUS_PENDING:    return "PENDING";
        case STATUS_CONFIRMED:  return "CONFIRMED";
        case STATUS_SUSPICIOUS: return "SUSPICIOUS";
        default:                return "UNKNOWN";
    }
}

int pool_check_excessive_borrows(const char *member_id) {
    Member *m = registry_find_member(member_id);
    if (!m) return 0;
    return (m->active_borrows >= MAX_BORROWS_PER_MEMBER);
}

int pool_check_rapid_reborrow(const char *book_id, const char *member_id) {
    time_t now = time(NULL);
    for (int i = 0; i < g_registry.copy_count; i++) {
        BookCopy *c = &g_registry.copies[i];
        if (strcmp(c->book_id, book_id) == 0 &&
            strcmp(c->last_returned_member, member_id) == 0) {
            if (c->last_returned_time > 0 && (now - c->last_returned_time) < 86400) {
                return 1; /* Re-borrowed within 24 hours */
            }
        }
    }
    return 0;
}

int pool_check_duplicate_request(const char *request_id) {
    if (!request_id) return 0;
    for (int i = 0; i < g_pool.count; i++) {
        if (strcmp(g_pool.requests[i].request_id, request_id) == 0) {
            return 1;
        }
    }
    return 0;
}

int pool_check_condition_downgrade(CopyCondition prev_cond, CopyCondition new_cond, const char *override_reason) {
    if ((int)prev_cond - (int)new_cond > 1) {
        if (!override_reason || strlen(override_reason) == 0) {
            return 1; /* Downgraded > 1 level without librarian code */
        }
    }
    return 0;
}

static void generate_request_id(char *out_id, size_t size, const char *prefix) {
    snprintf(out_id, size, "%s-%ld-%u", prefix, (long)time(NULL), ++s_request_counter);
}

int pool_add_borrow_request(const char *book_id, const char *copy_id, const char *member_id) {
    if (!book_id || !copy_id || !member_id) return 0;
    if (g_pool.count >= MAX_POOL_REQUESTS) {
        printf("[ERROR] Pending pool is full.\n");
        return 0;
    }

    Book *b = registry_find_book(book_id);
    Member *m = registry_find_member(member_id);
    BookCopy *c = registry_find_copy(copy_id);

    if (!b || !m || !c) {
        printf("[ERROR] Referential integrity error: Book, Member, or Copy not found.\n");
        return 0;
    }

    if (c->state != STATE_AVAILABLE && c->state != STATE_RESERVED) {
        printf("[ERROR] Copy %s is not available (Current State: %s).\n",
               c->copy_id, copy_state_to_string(c->state));
        return 0;
    }

    /* Check if reserved for someone else */
    if (c->state == STATE_RESERVED && strcmp(c->current_holder_id, member_id) != 0) {
        printf("[ERROR] Copy %s is reserved for member %s.\n", c->copy_id, c->current_holder_id);
        return 0;
    }

    PendingRequest *req = &g_pool.requests[g_pool.count++];
    memset(req, 0, sizeof(PendingRequest));
    generate_request_id(req->request_id, sizeof(req->request_id), "REQ-BORROW");
    safe_str_copy(req->book_id, book_id, sizeof(req->book_id));
    safe_str_copy(req->copy_id, copy_id, sizeof(req->copy_id));
    safe_str_copy(req->member_id, member_id, sizeof(req->member_id));
    req->action_requested = ACTION_BORROWED;
    req->created_at = time(NULL);
    req->status = STATUS_PENDING;
    req->priority = 10;
    req->suspicious_reason[0] = '\0';

    /* Build Lending Record */
    safe_str_copy(req->record.record_id, req->request_id, sizeof(req->record.record_id));
    safe_str_copy(req->record.book_id, b->book_id, sizeof(req->record.book_id));
    safe_str_copy(req->record.copy_id, c->copy_id, sizeof(req->record.copy_id));
    safe_str_copy(req->record.book_title, b->title, sizeof(req->record.book_title));
    safe_str_copy(req->record.member_id, m->member_id, sizeof(req->record.member_id));
    safe_str_copy(req->record.member_name, m->full_name, sizeof(req->record.member_name));
    req->record.action = ACTION_BORROWED;
    req->record.timestamp = req->created_at;
    req->record.due_date = req->created_at + DEFAULT_LOAN_DURATION_SECONDS;
    req->record.fine_amount = 0.0;
    req->record.nonce = m->nonce;

    /* Digitally sign the record */
    record_sign(&req->record, m->privkey_pem);

    /* Heuristic Checks */
    if (pool_check_excessive_borrows(member_id)) {
        req->status = STATUS_SUSPICIOUS;
        safe_str_copy(req->suspicious_reason, "Excessive concurrent borrows limit exceeded", sizeof(req->suspicious_reason));
        printf("[FRAUD WARNING] Borrow request flagged SUSPICIOUS: %s\n", req->suspicious_reason);
    } else if (pool_check_rapid_reborrow(book_id, member_id)) {
        req->status = STATUS_SUSPICIOUS;
        safe_str_copy(req->suspicious_reason, "Rapid re-borrow pattern within 24 hours", sizeof(req->suspicious_reason));
        printf("[FRAUD WARNING] Borrow request flagged SUSPICIOUS: %s\n", req->suspicious_reason);
    } else {
        printf("[SUCCESS] Borrow request %s submitted to pending pool.\n", req->request_id);
    }

    return 1;
}

int pool_add_return_request(const char *copy_id, const char *member_id, CopyCondition returned_cond, const char *override_reason) {
    if (!copy_id || !member_id) return 0;
    if (g_pool.count >= MAX_POOL_REQUESTS) {
        printf("[ERROR] Pending pool is full.\n");
        return 0;
    }

    BookCopy *c = registry_find_copy(copy_id);
    Member *m = registry_find_member(member_id);
    if (!c || !m) {
        printf("[ERROR] Copy or Member not found.\n");
        return 0;
    }

    if (c->state != STATE_BORROWED || strcmp(c->current_holder_id, member_id) != 0) {
        printf("[ERROR] Copy %s is not currently borrowed by member %s.\n", copy_id, member_id);
        return 0;
    }

    Book *b = registry_find_book(c->book_id);
    PendingRequest *req = &g_pool.requests[g_pool.count++];
    memset(req, 0, sizeof(PendingRequest));
    generate_request_id(req->request_id, sizeof(req->request_id), "REQ-RETURN");
    safe_str_copy(req->book_id, c->book_id, sizeof(req->book_id));
    safe_str_copy(req->copy_id, copy_id, sizeof(req->copy_id));
    safe_str_copy(req->member_id, member_id, sizeof(req->member_id));
    req->action_requested = ACTION_RETURNED;
    req->created_at = time(NULL);
    req->status = STATUS_PENDING;
    req->suspicious_reason[0] = '\0';

    /* Calculate Overdue and Fines */
    double fine = 0.0;
    int is_late = (req->created_at > c->due_date);
    if (is_late) {
        double seconds_late = difftime(req->created_at, c->due_date);
        int days_late = (int)(seconds_late / 86400) + 1;
        fine = days_late * FINE_PER_DAY;
        req->priority = 0; /* Overdue requests get top priority */
    } else {
        req->priority = 10;
    }

    safe_str_copy(req->record.record_id, req->request_id, sizeof(req->record.record_id));
    safe_str_copy(req->record.book_id, c->book_id, sizeof(req->record.book_id));
    safe_str_copy(req->record.copy_id, c->copy_id, sizeof(req->record.copy_id));
    safe_str_copy(req->record.book_title, b ? b->title : "Unknown", sizeof(req->record.book_title));
    safe_str_copy(req->record.member_id, m->member_id, sizeof(req->record.member_id));
    safe_str_copy(req->record.member_name, m->full_name, sizeof(req->record.member_name));
    req->record.action = ACTION_RETURNED;
    req->record.timestamp = req->created_at;
    req->record.due_date = c->due_date;
    req->record.fine_amount = fine;
    req->record.nonce = m->nonce;

    record_sign(&req->record, m->privkey_pem);

    /* Heuristic check for condition manipulation */
    if (pool_check_condition_downgrade(c->condition, returned_cond, override_reason)) {
        req->status = STATUS_SUSPICIOUS;
        safe_str_copy(req->suspicious_reason, "Condition downgraded > 1 level without librarian override", sizeof(req->suspicious_reason));
        printf("[FRAUD WARNING] Return request flagged SUSPICIOUS: %s\n", req->suspicious_reason);
    } else {
        c->condition = returned_cond;
        printf("[SUCCESS] Return request %s submitted (Late: %s, Fine: %.2f coins).\n",
               req->request_id, is_late ? "YES" : "NO", fine);
    }

    return 1;
}

int pool_add_reserve_request(const char *book_id, const char *member_id) {
    if (!book_id || !member_id) return 0;
    Book *b = registry_find_book(book_id);
    Member *m = registry_find_member(member_id);
    if (!b || !m) {
        printf("[ERROR] Book or Member not found.\n");
        return 0;
    }

    /* Check if an available copy exists */
    BookCopy *avail = registry_find_available_copy(book_id);
    if (avail) {
        printf("[INFO] Copy %s is currently AVAILABLE! You can borrow directly instead of reserving.\n",
               avail->copy_id);
    }

    time_t now = time(NULL);
    if (!reservation_enqueue(book_id, member_id, now)) {
        printf("[ERROR] Member is already queued or reservation failed.\n");
        return 0;
    }

    if (g_pool.count < MAX_POOL_REQUESTS) {
        PendingRequest *req = &g_pool.requests[g_pool.count++];
        memset(req, 0, sizeof(PendingRequest));
        generate_request_id(req->request_id, sizeof(req->request_id), "REQ-RESERVE");
        safe_str_copy(req->book_id, book_id, sizeof(req->book_id));
        safe_str_copy(req->copy_id, "ANY", sizeof(req->copy_id));
        safe_str_copy(req->member_id, member_id, sizeof(req->member_id));
        req->action_requested = ACTION_RESERVED;
        req->created_at = now;
        req->status = STATUS_PENDING;
        req->priority = 20;

        safe_str_copy(req->record.record_id, req->request_id, sizeof(req->record.record_id));
        safe_str_copy(req->record.book_id, b->book_id, sizeof(req->record.book_id));
        safe_str_copy(req->record.copy_id, "QUEUE", sizeof(req->record.copy_id));
        safe_str_copy(req->record.book_title, b->title, sizeof(req->record.book_title));
        safe_str_copy(req->record.member_id, m->member_id, sizeof(req->record.member_id));
        safe_str_copy(req->record.member_name, m->full_name, sizeof(req->record.member_name));
        req->record.action = ACTION_RESERVED;
        req->record.timestamp = now;
        req->record.due_date = 0;
        req->record.fine_amount = 0.0;
        req->record.nonce = m->nonce;
        record_sign(&req->record, m->privkey_pem);
    }

    printf("[SUCCESS] Member %s successfully added to reservation queue for book %s.\n",
           member_id, book_id);
    return 1;
}

int pool_add_renew_request(const char *copy_id, const char *member_id) {
    if (!copy_id || !member_id) return 0;
    BookCopy *c = registry_find_copy(copy_id);
    Member *m = registry_find_member(member_id);
    if (!c || !m) {
        printf("[ERROR] Copy or Member not found.\n");
        return 0;
    }

    if (c->state != STATE_BORROWED || strcmp(c->current_holder_id, member_id) != 0) {
        printf("[ERROR] Copy %s is not borrowed by member %s.\n", copy_id, member_id);
        return 0;
    }

    if (c->renewal_count >= MAX_RENEWALS_PER_COPY) {
        printf("[ERROR] Maximum renewal limit (%d) reached for this copy.\n", MAX_RENEWALS_PER_COPY);
        return 0;
    }

    if (reservation_get_queue_length(c->book_id) > 0) {
        printf("[ERROR] Cannot renew: title %s has members waiting in reservation queue.\n", c->book_id);
        return 0;
    }

    Book *b = registry_find_book(c->book_id);
    PendingRequest *req = &g_pool.requests[g_pool.count++];
    memset(req, 0, sizeof(PendingRequest));
    generate_request_id(req->request_id, sizeof(req->request_id), "REQ-RENEW");
    safe_str_copy(req->book_id, c->book_id, sizeof(req->book_id));
    safe_str_copy(req->copy_id, copy_id, sizeof(req->copy_id));
    safe_str_copy(req->member_id, member_id, sizeof(req->member_id));
    req->action_requested = ACTION_RENEWED;
    req->created_at = time(NULL);
    req->status = STATUS_PENDING;
    req->priority = 15;

    c->due_date += RENEWAL_EXTENSION_SECONDS;
    c->renewal_count++;

    safe_str_copy(req->record.record_id, req->request_id, sizeof(req->record.record_id));
    safe_str_copy(req->record.book_id, c->book_id, sizeof(req->record.book_id));
    safe_str_copy(req->record.copy_id, c->copy_id, sizeof(req->record.copy_id));
    safe_str_copy(req->record.book_title, b ? b->title : "Unknown", sizeof(req->record.book_title));
    safe_str_copy(req->record.member_id, m->member_id, sizeof(req->record.member_id));
    safe_str_copy(req->record.member_name, m->full_name, sizeof(req->record.member_name));
    req->record.action = ACTION_RENEWED;
    req->record.timestamp = req->created_at;
    req->record.due_date = c->due_date;
    req->record.fine_amount = 0.0;
    req->record.nonce = m->nonce;
    record_sign(&req->record, m->privkey_pem);

    printf("[SUCCESS] Renewal request created. New due date extended by 7 days (Renewals used: %d/%d).\n",
           c->renewal_count, MAX_RENEWALS_PER_COPY);
    return 1;
}

static int compare_requests(const void *a, const void *b) {
    const PendingRequest *ra = (const PendingRequest *)a;
    const PendingRequest *rb = (const PendingRequest *)b;

    /* Suspicious requests always sorted to the end */
    if (ra->status == STATUS_SUSPICIOUS && rb->status != STATUS_SUSPICIOUS) return 1;
    if (ra->status != STATUS_SUSPICIOUS && rb->status == STATUS_SUSPICIOUS) return -1;

    /* Priority ascending (0 = overdue first) */
    if (ra->priority != rb->priority) {
        return ra->priority - rb->priority;
    }
    /* Tie-breaker: earlier creation/reservation time first */
    if (ra->created_at < rb->created_at) return -1;
    if (ra->created_at > rb->created_at) return 1;
    return 0;
}

void pool_sort_by_priority() {
    if (g_pool.count > 1) {
        qsort(g_pool.requests, g_pool.count, sizeof(PendingRequest), compare_requests);
    }
}

int pool_get_top_requests(PendingRequest *out_batch, int max_records) {
    if (!out_batch || max_records <= 0) return 0;
    pool_sort_by_priority();

    int selected = 0;
    for (int i = 0; i < g_pool.count && selected < max_records; i++) {
        /* Strictly never include SUSPICIOUS requests in a mined block */
        if (g_pool.requests[i].status == STATUS_PENDING) {
            out_batch[selected++] = g_pool.requests[i];
        }
    }
    return selected;
}

void pool_remove_confirmed(const PendingRequest *confirmed_batch, int count) {
    if (!confirmed_batch || count <= 0) return;

    for (int b = 0; b < count; b++) {
        for (int i = 0; i < g_pool.count; i++) {
            if (strcmp(g_pool.requests[i].request_id, confirmed_batch[b].request_id) == 0) {
                /* Remove by shifting subsequent items */
                for (int j = i; j < g_pool.count - 1; j++) {
                    g_pool.requests[j] = g_pool.requests[j + 1];
                }
                g_pool.count--;
                break;
            }
        }
    }
}

void pool_print_fraud_review() {
    printf("\n=== Fraud & Misuse Review Queue ===\n");
    int suspicious_count = 0;
    for (int i = 0; i < g_pool.count; i++) {
        PendingRequest *r = &g_pool.requests[i];
        if (r->status == STATUS_SUSPICIOUS) {
            suspicious_count++;
            printf("ID: %-20s | Member: %-6s | Action: %-10s | Reason: %s\n",
                   r->request_id, r->member_id, action_to_string(r->action_requested), r->suspicious_reason);
        }
    }
    if (suspicious_count == 0) {
        printf("No suspicious requests currently flagged.\n");
    } else {
        printf("\nUse 'approve_suspicious <request_id>' or 'reject_suspicious <request_id>'.\n");
    }
    printf("===================================\n\n");
}

int pool_approve_suspicious(const char *request_id) {
    if (!request_id) return 0;
    for (int i = 0; i < g_pool.count; i++) {
        if (strcmp(g_pool.requests[i].request_id, request_id) == 0) {
            if (g_pool.requests[i].status == STATUS_SUSPICIOUS) {
                g_pool.requests[i].status = STATUS_PENDING;
                g_pool.requests[i].suspicious_reason[0] = '\0';
                printf("[AUDIT] Request %s approved by librarian. Status changed to PENDING.\n", request_id);
                return 1;
            }
        }
    }
    printf("[ERROR] Request ID %s not found in suspicious queue.\n", request_id);
    return 0;
}

int pool_reject_suspicious(const char *request_id) {
    if (!request_id) return 0;
    for (int i = 0; i < g_pool.count; i++) {
        if (strcmp(g_pool.requests[i].request_id, request_id) == 0) {
            for (int j = i; j < g_pool.count - 1; j++) {
                g_pool.requests[j] = g_pool.requests[j + 1];
            }
            g_pool.count--;
            printf("[AUDIT] Request %s rejected and discarded from pool.\n", request_id);
            return 1;
        }
    }
    printf("[ERROR] Request ID %s not found in pool.\n", request_id);
    return 0;
}

void pool_print_all() {
    pool_sort_by_priority();
    printf("\n=== Pending Lending & Token Request Pool (%d total) ===\n", g_pool.count);
    if (g_pool.count == 0) {
        printf("Pool is empty.\n");
    }
    for (int i = 0; i < g_pool.count; i++) {
        PendingRequest *r = &g_pool.requests[i];
        printf("[%02d] ID: %-20s | Book: %-6s | Copy: %-8s | Member: %-6s | Action: %-10s | Priority: %-3d | Status: %-10s\n",
               i + 1, r->request_id, r->book_id, r->copy_id, r->member_id,
               action_to_string(r->action_requested), r->priority, request_status_to_string(r->status));
        if (r->status == STATUS_SUSPICIOUS) {
            printf("     >>> Flagged Reason: %s\n", r->suspicious_reason);
        }
    }
    printf("========================================================\n\n");
}
