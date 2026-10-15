#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Registry g_registry;

void trim_whitespace(char *str) {
    if (!str) return;
    char *start = str;
    while (*start && (isspace((unsigned char)*start) || *start == '\r' || *start == '\n')) {
        start++;
    }
    char *end = start + strlen(start);
    while (end > start && (isspace((unsigned char)*(end - 1)) || *(end - 1) == '\r' || *(end - 1) == '\n')) {
        end--;
    }
    *end = '\0';
    if (start != str) {
        memmove(str, start, end - start + 1);
    }
}

const char* condition_to_string(CopyCondition cond) {
    switch (cond) {
        case COND_NEW:  return "NEW";
        case COND_GOOD: return "GOOD";
        case COND_WORN: return "WORN";
        case COND_LOST: return "LOST";
        default:        return "UNKNOWN";
    }
}

CopyCondition string_to_condition(const char *str) {
    if (!str) return COND_GOOD;
    if (strcmp(str, "NEW") == 0) return COND_NEW;
    if (strcmp(str, "GOOD") == 0) return COND_GOOD;
    if (strcmp(str, "WORN") == 0) return COND_WORN;
    if (strcmp(str, "LOST") == 0) return COND_LOST;
    return COND_GOOD;
}

const char* copy_state_to_string(CopyState state) {
    switch (state) {
        case STATE_AVAILABLE: return "AVAILABLE";
        case STATE_BORROWED:  return "BORROWED";
        case STATE_RESERVED:  return "RESERVED";
        case STATE_LOST:      return "LOST";
        default:              return "UNKNOWN";
    }
}

CopyState string_to_copy_state(const char *str) {
    if (!str) return STATE_AVAILABLE;
    if (strcmp(str, "AVAILABLE") == 0) return STATE_AVAILABLE;
    if (strcmp(str, "BORROWED") == 0)  return STATE_BORROWED;
    if (strcmp(str, "RESERVED") == 0)  return STATE_RESERVED;
    if (strcmp(str, "LOST") == 0)      return STATE_LOST;
    return STATE_AVAILABLE;
}

int registry_init(const char *books_file, const char *members_file, const char *copies_file) {
    memset(&g_registry, 0, sizeof(Registry));
    strncpy(g_registry.books_filepath, books_file, sizeof(g_registry.books_filepath) - 1);
    strncpy(g_registry.members_filepath, members_file, sizeof(g_registry.members_filepath) - 1);
    strncpy(g_registry.copies_filepath, copies_file, sizeof(g_registry.copies_filepath) - 1);

    /* 1. Load Books */
    FILE *fb = fopen(books_file, "r");
    if (!fb) {
        fprintf(stderr, "[ERROR] Cannot open books registry file: %s\n", books_file);
        return 0;
    }
    char line[512];
    while (fgets(line, sizeof(line), fb)) {
        trim_whitespace(line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        char *id = strtok(line, "|");
        char *title = strtok(NULL, "|");
        char *author = strtok(NULL, "|");
        if (id && title && author && g_registry.book_count < MAX_BOOKS) {
            Book *b = &g_registry.books[g_registry.book_count++];
            strncpy(b->book_id, id, sizeof(b->book_id) - 1);
            strncpy(b->title, title, sizeof(b->title) - 1);
            strncpy(b->author, author, sizeof(b->author) - 1);
        }
    }
    fclose(fb);

    /* 2. Load Members */
    FILE *fm = fopen(members_file, "r");
    if (!fm) {
        fprintf(stderr, "[ERROR] Cannot open members registry file: %s\n", members_file);
        return 0;
    }
    while (fgets(line, sizeof(line), fm)) {
        trim_whitespace(line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        char *id = strtok(line, "|");
        char *name = strtok(NULL, "|");
        char *course = strtok(NULL, "|");
        if (id && name && course && g_registry.member_count < MAX_MEMBERS) {
            Member *m = &g_registry.members[g_registry.member_count++];
            strncpy(m->member_id, id, sizeof(m->member_id) - 1);
            strncpy(m->full_name, name, sizeof(m->full_name) - 1);
            strncpy(m->course_code, course, sizeof(m->course_code) - 1);
            m->nonce = 0;
            m->token_balance = 0.0;
            m->fines_due = 0.0;
            m->active_borrows = 0;

            /* Load persistent keypair if available, otherwise generate and save */
            if (!crypto_load_keypair(m->member_id, m->privkey_pem, sizeof(m->privkey_pem),
                                    m->pubkey_pem, sizeof(m->pubkey_pem))) {
                crypto_generate_keypair(m->privkey_pem, sizeof(m->privkey_pem),
                                        m->pubkey_pem, sizeof(m->pubkey_pem));
                crypto_save_keypair(m->member_id, m->privkey_pem, m->pubkey_pem);
            }
        }
    }
    fclose(fm);

    /* 3. Load Copies */
    FILE *fc = fopen(copies_file, "r");
    if (!fc) {
        fprintf(stderr, "[ERROR] Cannot open copies inventory file: %s\n", copies_file);
        return 0;
    }
    while (fgets(line, sizeof(line), fc)) {
        trim_whitespace(line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        char *cid = strtok(line, "|");
        char *bid = strtok(NULL, "|");
        char *cond_str = strtok(NULL, "|");
        if (cid && bid && cond_str && g_registry.copy_count < MAX_COPIES) {
            BookCopy *c = &g_registry.copies[g_registry.copy_count++];
            strncpy(c->copy_id, cid, sizeof(c->copy_id) - 1);
            strncpy(c->book_id, bid, sizeof(c->book_id) - 1);
            c->condition = string_to_condition(cond_str);
            c->last_condition = c->condition;
            c->state = (c->condition == COND_LOST) ? STATE_LOST : STATE_AVAILABLE;
            c->current_holder_id[0] = '\0';
            c->borrow_timestamp = 0;
            c->due_date = 0;
            c->renewal_count = 0;
            c->last_returned_time = 0;
            c->last_returned_member[0] = '\0';
        }
    }
    fclose(fc);

    printf("[INFO] Registries initialized: %d Books, %d Members, %d Physical Copies loaded.\n",
           g_registry.book_count, g_registry.member_count, g_registry.copy_count);
    return 1;
}

Book* registry_find_book(const char *book_id) {
    if (!book_id) return NULL;
    for (int i = 0; i < g_registry.book_count; i++) {
        if (strcmp(g_registry.books[i].book_id, book_id) == 0) {
            return &g_registry.books[i];
        }
    }
    return NULL;
}

Member* registry_find_member(const char *member_id) {
    if (!member_id) return NULL;
    for (int i = 0; i < g_registry.member_count; i++) {
        if (strcmp(g_registry.members[i].member_id, member_id) == 0) {
            return &g_registry.members[i];
        }
    }
    return NULL;
}

BookCopy* registry_find_copy(const char *copy_id) {
    if (!copy_id) return NULL;
    for (int i = 0; i < g_registry.copy_count; i++) {
        if (strcmp(g_registry.copies[i].copy_id, copy_id) == 0) {
            return &g_registry.copies[i];
        }
    }
    return NULL;
}

BookCopy* registry_find_available_copy(const char *book_id) {
    if (!book_id) return NULL;
    for (int i = 0; i < g_registry.copy_count; i++) {
        if (strcmp(g_registry.copies[i].book_id, book_id) == 0 &&
            g_registry.copies[i].state == STATE_AVAILABLE) {
            return &g_registry.copies[i];
        }
    }
    return NULL;
}

int registry_add_book(const char *book_id, const char *title, const char *author) {
    if (!book_id || !title || !author) return 0;
    if (registry_find_book(book_id)) return 0; // Already exists
    if (g_registry.book_count >= MAX_BOOKS) return 0;

    Book *b = &g_registry.books[g_registry.book_count++];
    strncpy(b->book_id, book_id, sizeof(b->book_id) - 1);
    strncpy(b->title, title, sizeof(b->title) - 1);
    strncpy(b->author, author, sizeof(b->author) - 1);

    FILE *f = fopen(g_registry.books_filepath, "a");
    if (f) {
        fprintf(f, "%s|%s|%s\n", book_id, title, author);
        fclose(f);
    }
    return 1;
}

int registry_add_member(const char *member_id, const char *full_name, const char *course_code) {
    if (!member_id || !full_name || !course_code) return 0;
    if (registry_find_member(member_id)) return 0; // Already exists
    if (g_registry.member_count >= MAX_MEMBERS) return 0;

    Member *m = &g_registry.members[g_registry.member_count++];
    strncpy(m->member_id, member_id, sizeof(m->member_id) - 1);
    strncpy(m->full_name, full_name, sizeof(m->full_name) - 1);
    strncpy(m->course_code, course_code, sizeof(m->course_code) - 1);
    m->nonce = 0;
    m->token_balance = 0.0;
    m->fines_due = 0.0;
    m->active_borrows = 0;

    crypto_generate_keypair(m->privkey_pem, sizeof(m->privkey_pem),
                            m->pubkey_pem, sizeof(m->pubkey_pem));
    crypto_save_keypair(m->member_id, m->privkey_pem, m->pubkey_pem);

    FILE *f = fopen(g_registry.members_filepath, "a");
    if (f) {
        fprintf(f, "%s|%s|%s\n", member_id, full_name, course_code);
        fclose(f);
    }
    return 1;
}

int registry_add_copy(const char *copy_id, const char *book_id, CopyCondition cond) {
    if (!copy_id || !book_id) return 0;
    if (!registry_find_book(book_id)) return 0; // Book must exist
    if (registry_find_copy(copy_id)) return 0;  // Copy ID must be unique
    if (g_registry.copy_count >= MAX_COPIES) return 0;

    BookCopy *c = &g_registry.copies[g_registry.copy_count++];
    strncpy(c->copy_id, copy_id, sizeof(c->copy_id) - 1);
    strncpy(c->book_id, book_id, sizeof(c->book_id) - 1);
    c->condition = cond;
    c->last_condition = cond;
    c->state = (cond == COND_LOST) ? STATE_LOST : STATE_AVAILABLE;
    c->current_holder_id[0] = '\0';
    c->borrow_timestamp = 0;
    c->due_date = 0;
    c->renewal_count = 0;
    c->last_returned_time = 0;
    c->last_returned_member[0] = '\0';

    FILE *f = fopen(g_registry.copies_filepath, "a");
    if (f) {
        fprintf(f, "%s|%s|%s\n", copy_id, book_id, condition_to_string(cond));
        fclose(f);
    }
    return 1;
}

int registry_mark_lost(const char *copy_id, const char *reason_code) {
    if (!copy_id || !reason_code || strlen(reason_code) == 0) return 0;
    BookCopy *c = registry_find_copy(copy_id);
    if (!c) return 0;

    c->condition = COND_LOST;
    c->state = STATE_LOST;
    if (strlen(c->current_holder_id) > 0) {
        Member *m = registry_find_member(c->current_holder_id);
        if (m && m->active_borrows > 0) {
            m->active_borrows--;
        }
        c->current_holder_id[0] = '\0';
    }
    registry_save_copies();
    return 1;
}

int registry_save_copies() {
    FILE *f = fopen(g_registry.copies_filepath, "w");
    if (!f) return 0;

    for (int i = 0; i < g_registry.copy_count; i++) {
        BookCopy *c = &g_registry.copies[i];
        fprintf(f, "%s|%s|%s\n", c->copy_id, c->book_id, condition_to_string(c->condition));
    }
    fclose(f);
    return 1;
}
