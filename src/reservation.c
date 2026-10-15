#include "reservation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BookReservationQueue s_queues[MAX_RESERVATION_QUEUES];
static int s_queue_count = 0;

static BookReservationQueue* get_or_create_queue(const char *book_id) {
    if (!book_id) return NULL;
    for (int i = 0; i < s_queue_count; i++) {
        if (strcmp(s_queues[i].book_id, book_id) == 0) {
            return &s_queues[i];
        }
    }
    if (s_queue_count < MAX_RESERVATION_QUEUES) {
        BookReservationQueue *q = &s_queues[s_queue_count++];
        strncpy(q->book_id, book_id, sizeof(q->book_id) - 1);
        q->front = NULL;
        q->rear = NULL;
        q->count = 0;
        return q;
    }
    return NULL;
}

void reservation_init() {
    reservation_free_all();
    s_queue_count = 0;
}

int reservation_enqueue(const char *book_id, const char *member_id, time_t res_date) {
    if (!book_id || !member_id) return 0;
    if (reservation_is_member_queued(book_id, member_id)) return 0; // Already in queue

    BookReservationQueue *q = get_or_create_queue(book_id);
    if (!q) return 0;

    ReservationNode *node = (ReservationNode *)malloc(sizeof(ReservationNode));
    if (!node) return 0;

    strncpy(node->member_id, member_id, sizeof(node->member_id) - 1);
    node->reservation_date = (res_date > 0) ? res_date : time(NULL);
    node->next = NULL;

    if (q->rear == NULL) {
        q->front = node;
        q->rear = node;
    } else {
        q->rear->next = node;
        q->rear = node;
    }
    q->count++;
    return 1;
}

int reservation_dequeue(const char *book_id, char *out_member_id, time_t *out_date) {
    if (!book_id) return 0;
    for (int i = 0; i < s_queue_count; i++) {
        if (strcmp(s_queues[i].book_id, book_id) == 0) {
            BookReservationQueue *q = &s_queues[i];
            if (!q->front) return 0;

            ReservationNode *node = q->front;
            if (out_member_id) {
                strncpy(out_member_id, node->member_id, ID_MAX_LEN - 1);
                out_member_id[ID_MAX_LEN - 1] = '\0';
            }
            if (out_date) {
                *out_date = node->reservation_date;
            }

            q->front = node->next;
            if (q->front == NULL) {
                q->rear = NULL;
            }
            q->count--;
            free(node);
            return 1;
        }
    }
    return 0;
}

int reservation_peek(const char *book_id, char *out_member_id, time_t *out_date) {
    if (!book_id) return 0;
    for (int i = 0; i < s_queue_count; i++) {
        if (strcmp(s_queues[i].book_id, book_id) == 0) {
            BookReservationQueue *q = &s_queues[i];
            if (!q->front) return 0;
            if (out_member_id) {
                strncpy(out_member_id, q->front->member_id, ID_MAX_LEN - 1);
                out_member_id[ID_MAX_LEN - 1] = '\0';
            }
            if (out_date) {
                *out_date = q->front->reservation_date;
            }
            return 1;
        }
    }
    return 0;
}

int reservation_get_queue_length(const char *book_id) {
    if (!book_id) return 0;
    for (int i = 0; i < s_queue_count; i++) {
        if (strcmp(s_queues[i].book_id, book_id) == 0) {
            return s_queues[i].count;
        }
    }
    return 0;
}

int reservation_is_member_queued(const char *book_id, const char *member_id) {
    if (!book_id || !member_id) return 0;
    for (int i = 0; i < s_queue_count; i++) {
        if (strcmp(s_queues[i].book_id, book_id) == 0) {
            ReservationNode *curr = s_queues[i].front;
            while (curr) {
                if (strcmp(curr->member_id, member_id) == 0) return 1;
                curr = curr->next;
            }
            return 0;
        }
    }
    return 0;
}

void reservation_print_status(const char *book_id) {
    printf("\n=== Book Reservation Queues ===\n");
    int found = 0;
    for (int i = 0; i < s_queue_count; i++) {
        if (!book_id || strcmp(s_queues[i].book_id, book_id) == 0) {
            found = 1;
            Book *b = registry_find_book(s_queues[i].book_id);
            printf("Book [%s] %s (Queue Depth: %d):\n",
                   s_queues[i].book_id,
                   b ? b->title : "Unknown Title",
                   s_queues[i].count);

            ReservationNode *curr = s_queues[i].front;
            int pos = 1;
            while (curr) {
                char date_buf[64] = "N/A";
                struct tm *tm_info = localtime(&curr->reservation_date);
                if (tm_info) strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", tm_info);

                Member *m = registry_find_member(curr->member_id);
                printf("  %d. Member [%s] %s | Reserved At: %s\n",
                       pos++, curr->member_id, m ? m->full_name : "Unknown", date_buf);
                curr = curr->next;
            }
            if (s_queues[i].count == 0) {
                printf("  (Queue is currently empty)\n");
            }
        }
    }
    if (!found) {
        printf("No active reservations for book: %s\n", book_id ? book_id : "All");
    }
    printf("================================\n\n");
}

void reservation_free_all() {
    for (int i = 0; i < s_queue_count; i++) {
        ReservationNode *curr = s_queues[i].front;
        while (curr) {
            ReservationNode *tmp = curr;
            curr = curr->next;
            free(tmp);
        }
        s_queues[i].front = NULL;
        s_queues[i].rear = NULL;
        s_queues[i].count = 0;
    }
    s_queue_count = 0;
}
