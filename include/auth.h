#ifndef INVENTIX_AUTH_H
#define INVENTIX_AUTH_H

#include "storage.h"

typedef struct {
    char username[32];
    int is_admin;
} User;

// Initialize Auth System (Create default admin if needed)
void auth_init(KVStore *store);

// User Management
int auth_create_user(KVStore *store, const char *username, const char *password);
int auth_verify_user(KVStore *store, const char *username, const char *password, User *user_out);

#endif
