#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "auth.h"
#include "storage.h"

// Simple hash (djb2) for password storage (In production use bcrypt/argon2)
static unsigned long hash_password(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

void auth_init(KVStore *store) {
    // Check if admin exists
    char key[] = "SYS:USER:admin";
    if (!kv_get(store, key)) {
        printf("[Auth] Initializing default admin user...\n");
        auth_create_user(store, "admin", "admin"); // Default pass
    }
}

int auth_create_user(KVStore *store, const char *username, const char *password) {
    char key[64];
    sprintf(key, "SYS:USER:%s", username);
    
    if (kv_get(store, key)) {
        return 0; // Already exists
    }
    
    unsigned long phash = hash_password(password);
    kv_put(store, key, &phash, sizeof(unsigned long), VAL_TYPE_ROW);
    return 1;
}

int auth_verify_user(KVStore *store, const char *username, const char *password, User *user_out) {
    char key[64];
    sprintf(key, "SYS:USER:%s", username);
    
    Value *val = kv_get(store, key);
    if (!val) return 0; // User not found
    
    unsigned long stored_hash = *(unsigned long*)val->data;
    if (stored_hash == hash_password(password)) {
        if (user_out) {
            strncpy(user_out->username, username, 31);
            user_out->username[31] = '\0';
            user_out->is_admin = (strcmp(username, "admin") == 0);
        }
        return 1;
    }
    return 0;
}
