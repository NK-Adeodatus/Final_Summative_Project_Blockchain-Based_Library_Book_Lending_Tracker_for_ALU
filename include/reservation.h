#ifndef ALU_RESERVATION_H
#define ALU_RESERVATION_H

#include <time.h>
#include "transaction.h"
#include "registry.h"

#define MAX_RESERVATION_QUEUES MAX_BOOKS

typedef struct ReservationNode {
    char member_id[ID_MAX_LEN];
    time_t reservation_date;
    struct ReservationNode *next;
} ReservationNode;

typedef struct {
    char book_id[ID_MAX_LEN];
    ReservationNode *front;
    ReservationNode *rear;
    int count;
} BookReservationQueue;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize reservation queues */
void reservation_init();

/* Enqueue a member's reservation for a book */
int reservation_enqueue(const char *book_id, const char *member_id, time_t res_date);

/* Dequeue the next member in line for a book */
int reservation_dequeue(const char *book_id, char *out_member_id, time_t *out_date);

/* Peek at the next member without removing */
int reservation_peek(const char *book_id, char *out_member_id, time_t *out_date);

/* Get number of waiting members for a book */
int reservation_get_queue_length(const char *book_id);

/* Check if a member is already waiting in queue for a book */
int reservation_is_member_queued(const char *book_id, const char *member_id);

/* Print reservation status for a book or all books */
void reservation_print_status(const char *book_id);

/* Clear and free all reservation queues */
void reservation_free_all();

#ifdef __cplusplus
}
#endif

#endif /* ALU_RESERVATION_H */
