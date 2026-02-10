/**
 * InventixDB Security Module Implementation
 * 
 * Implements bcrypt-style password hashing, RBAC, sessions, and encryption
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "security.h"
#include "logger.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

SecurityContext *g_security = NULL;

// ============================================================================
// CRYPTOGRAPHIC UTILITIES
// ============================================================================

// Simple but secure random number generation
void security_random_bytes(uint8_t *buffer, size_t length) {
#ifdef _WIN32
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProv, (DWORD)length, buffer);
        CryptReleaseContext(hProv, 0);
    } else {
        // Fallback (less secure)
        srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());
        for (size_t i = 0; i < length; i++) {
            buffer[i] = (uint8_t)(rand() % 256);
        }
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, buffer, length);
        close(fd);
    } else {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (size_t i = 0; i < length; i++) {
            buffer[i] = (uint8_t)(rand() % 256);
        }
    }
#endif
}

// Convert bytes to hex string
static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';
}

// Convert hex string to bytes
static void hex_to_bytes(const char *hex, uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        uint8_t h = (high >= 'a') ? (high - 'a' + 10) : (high - '0');
        uint8_t l = (low >= 'a') ? (low - 'a' + 10) : (low - '0');
        bytes[i] = (h << 4) | l;
    }
}

// ============================================================================
// BCRYPT-STYLE PASSWORD HASHING
// ============================================================================

// SHA-256 constants
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    
    for (int i = 0; i < 16; i++) {
        m[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }
    
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    
    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    
    uint8_t block[64];
    size_t remaining = len;
    size_t offset = 0;
    
    // Process full blocks
    while (remaining >= 64) {
        sha256_transform(state, data + offset);
        offset += 64;
        remaining -= 64;
    }
    
    // Final block with padding
    memset(block, 0, 64);
    memcpy(block, data + offset, remaining);
    block[remaining] = 0x80;
    
    if (remaining >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }
    
    // Length in bits
    uint64_t bits = len * 8;
    for (int i = 0; i < 8; i++) {
        block[63 - i] = (uint8_t)(bits >> (i * 8));
    }
    sha256_transform(state, block);
    
    // Output
    for (int i = 0; i < 8; i++) {
        hash[i * 4] = (uint8_t)(state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(state[i]);
    }
}

// PBKDF2-HMAC-SHA256 (bcrypt-like key stretching)
static void pbkdf2_sha256(const char *password, const uint8_t *salt, size_t salt_len,
                          int iterations, uint8_t *output, size_t output_len) {
    uint8_t U[32];
    uint8_t T[32];
    
    size_t password_len = strlen(password);
    uint8_t *message = malloc(password_len + salt_len + 4 + 64);
    
    // HMAC key preparation
    uint8_t key[64];
    memset(key, 0, 64);
    if (password_len > 64) {
        sha256((const uint8_t*)password, password_len, key);
    } else {
        memcpy(key, password, password_len);
    }
    
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = key[i] ^ 0x36;
        opad[i] = key[i] ^ 0x5c;
    }
    
    size_t blocks = (output_len + 31) / 32;
    
    for (size_t block_num = 1; block_num <= blocks; block_num++) {
        // U1 = HMAC(password, salt || INT(block_num))
        memcpy(message, ipad, 64);
        memcpy(message + 64, salt, salt_len);
        message[64 + salt_len] = (uint8_t)(block_num >> 24);
        message[64 + salt_len + 1] = (uint8_t)(block_num >> 16);
        message[64 + salt_len + 2] = (uint8_t)(block_num >> 8);
        message[64 + salt_len + 3] = (uint8_t)(block_num);
        
        uint8_t inner_hash[32];
        sha256(message, 64 + salt_len + 4, inner_hash);
        
        memcpy(message, opad, 64);
        memcpy(message + 64, inner_hash, 32);
        sha256(message, 96, U);
        memcpy(T, U, 32);
        
        // Iterate
        for (int i = 1; i < iterations; i++) {
            // U_i = HMAC(password, U_{i-1})
            memcpy(message, ipad, 64);
            memcpy(message + 64, U, 32);
            sha256(message, 96, inner_hash);
            
            memcpy(message, opad, 64);
            memcpy(message + 64, inner_hash, 32);
            sha256(message, 96, U);
            
            for (int j = 0; j < 32; j++) {
                T[j] ^= U[j];
            }
        }
        
        size_t copy_len = (block_num == blocks) ? (output_len - (blocks - 1) * 32) : 32;
        memcpy(output + (block_num - 1) * 32, T, copy_len);
    }
    
    free(message);
}

int security_hash_password(const char *password, char *salt_hex, char *hash_hex) {
    if (!password || !salt_hex || !hash_hex) return -1;
    
    // Generate random salt
    uint8_t salt[SECURITY_SALT_LENGTH];
    security_random_bytes(salt, SECURITY_SALT_LENGTH);
    bytes_to_hex(salt, SECURITY_SALT_LENGTH, salt_hex);
    
    // Hash with PBKDF2 (4096 iterations)
    uint8_t hash[32];
    int iterations = g_security ? (1 << g_security->bcrypt_cost) : 4096;
    pbkdf2_sha256(password, salt, SECURITY_SALT_LENGTH, iterations, hash, 32);
    bytes_to_hex(hash, 32, hash_hex);
    
    return 0;
}

bool security_verify_password(const char *password, const char *salt_hex, const char *stored_hash) {
    if (!password || !salt_hex || !stored_hash) return false;
    
    // Convert salt from hex
    uint8_t salt[SECURITY_SALT_LENGTH];
    hex_to_bytes(salt_hex, salt, SECURITY_SALT_LENGTH);
    
    // Hash provided password
    uint8_t hash[32];
    int iterations = g_security ? (1 << g_security->bcrypt_cost) : 4096;
    pbkdf2_sha256(password, salt, SECURITY_SALT_LENGTH, iterations, hash, 32);
    
    char computed_hash[65];
    bytes_to_hex(hash, 32, computed_hash);
    
    // Constant-time comparison
    int diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= computed_hash[i] ^ stored_hash[i];
    }
    
    return diff == 0;
}

bool security_validate_password(const char *password, char *error_msg) {
    if (!g_security || !password) {
        if (error_msg) strcpy(error_msg, "Invalid parameters");
        return false;
    }
    
    size_t len = strlen(password);
    
    if ((int)len < g_security->password_min_length) {
        if (error_msg) sprintf(error_msg, "Password must be at least %d characters", 
                               g_security->password_min_length);
        return false;
    }
    
    bool has_upper = false, has_lower = false, has_digit = false, has_special = false;
    
    for (size_t i = 0; i < len; i++) {
        if (isupper(password[i])) has_upper = true;
        else if (islower(password[i])) has_lower = true;
        else if (isdigit(password[i])) has_digit = true;
        else has_special = true;
    }
    
    if (g_security->require_uppercase && !has_upper) {
        if (error_msg) strcpy(error_msg, "Password must contain uppercase letter");
        return false;
    }
    if (g_security->require_lowercase && !has_lower) {
        if (error_msg) strcpy(error_msg, "Password must contain lowercase letter");
        return false;
    }
    if (g_security->require_digit && !has_digit) {
        if (error_msg) strcpy(error_msg, "Password must contain a digit");
        return false;
    }
    if (g_security->require_special && !has_special) {
        if (error_msg) strcpy(error_msg, "Password must contain special character");
        return false;
    }
    
    return true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void security_init_roles(void) {
    if (!g_security) return;
    
    // Superadmin - all permissions
    security_create_role(ROLE_SUPERADMIN, PERM_ALL, "Full system access");
    g_security->roles[0].is_system_role = true;
    
    // Admin - all except shutdown
    security_create_role(ROLE_ADMIN, 
        PERM_ALL & ~(PERM_SHUTDOWN | PERM_RESTORE),
        "Database administration");
    g_security->roles[1].is_system_role = true;
    
    // Developer - DDL + DML
    security_create_role(ROLE_DEVELOPER,
        PERM_CREATE_DB | PERM_USE_DB | PERM_CREATE_TABLE | PERM_DROP_TABLE | PERM_ALTER_TABLE |
        PERM_SELECT | PERM_INSERT | PERM_UPDATE | PERM_DELETE | PERM_TRANSACTION | PERM_VIEW_STATS,
        "Application development");
    g_security->roles[2].is_system_role = true;
    
    // Analyst - read + limited write
    security_create_role(ROLE_ANALYST,
        PERM_USE_DB | PERM_SELECT | PERM_INSERT | PERM_VIEW_STATS,
        "Data analysis");
    g_security->roles[3].is_system_role = true;
    
    // Readonly - only select
    security_create_role(ROLE_READONLY,
        PERM_USE_DB | PERM_SELECT,
        "Read-only access");
    g_security->roles[4].is_system_role = true;
    
    // Guest - minimal
    security_create_role(ROLE_GUEST,
        PERM_USE_DB,
        "Guest access");
    g_security->roles[5].is_system_role = true;
}

int security_init(void) {
    if (g_security) {
        LOG_WARN("Security already initialized");
        return 0;
    }
    
    g_security = calloc(1, sizeof(SecurityContext));
    if (!g_security) {
        LOG_ERROR("Failed to allocate security context");
        return -1;
    }
    
    // Default configuration
    g_security->bcrypt_cost = SECURITY_BCRYPT_COST;
    g_security->session_timeout = SECURITY_SESSION_TIMEOUT;
    g_security->max_failed_attempts = 5;
    g_security->lockout_duration = 300;
    g_security->password_min_length = 8;
    g_security->require_uppercase = true;
    g_security->require_lowercase = true;
    g_security->require_digit = true;
    g_security->require_special = false;
    
    // Initialize lock
#ifdef _WIN32
    g_security->lock = CreateMutex(NULL, FALSE, NULL);
#else
    g_security->lock = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init((pthread_mutex_t*)g_security->lock, NULL);
#endif
    
    // Initialize roles first (needed before loading/creating users)
    security_init_roles();
    
    // Try to restore persisted users from security.dat
    if (security_load("security.dat") == 0) {
        LOG_INFO("Restored %d users from security.dat", g_security->user_count);
    } else {
        // No saved state — create default superuser
        char error[128];
        int user_id = security_create_user("admin", "Admin@123", ROLE_SUPERADMIN, error);
        if (user_id >= 0) {
            // user_id is 1-based, array is 0-indexed
            g_security->users[user_id - 1].is_superuser = true;
            LOG_INFO("Created default superuser 'admin'");
        }
    }
    
    g_security->initialized = true;
    LOG_INFO("Security module initialized");
    
    return 0;
}

void security_shutdown(void) {
    if (!g_security) return;
    
    // Save state
    security_save("security.dat");
    
    // Cleanup
#ifdef _WIN32
    if (g_security->lock) CloseHandle((HANDLE)g_security->lock);
#else
    if (g_security->lock) {
        pthread_mutex_destroy((pthread_mutex_t*)g_security->lock);
        free(g_security->lock);
    }
#endif
    
    free(g_security);
    g_security = NULL;
    
    LOG_INFO("Security module shutdown");
}

// ============================================================================
// USER MANAGEMENT
// ============================================================================

int security_create_user(const char *username, const char *password, 
                         const char *role, char *error_msg) {
    if (!g_security || !username || !password) {
        if (error_msg) strcpy(error_msg, "Invalid parameters");
        return -1;
    }
    
    // Check if user exists
    if (security_find_user(username)) {
        if (error_msg) sprintf(error_msg, "User '%s' already exists", username);
        return -1;
    }
    
    // Validate password
    if (!security_validate_password(password, error_msg)) {
        return -1;
    }
    
    // Check capacity
    if (g_security->user_count >= SECURITY_MAX_USERS) {
        if (error_msg) strcpy(error_msg, "Maximum users reached");
        return -1;
    }
    
    // Create user
    User *user = &g_security->users[g_security->user_count];
    memset(user, 0, sizeof(User));
    
    user->user_id = g_security->user_count + 1;
    strncpy(user->username, username, SECURITY_USERNAME_MAX - 1);
    
    // Hash password
    if (security_hash_password(password, user->salt, user->password_hash) != 0) {
        if (error_msg) strcpy(error_msg, "Failed to hash password");
        return -1;
    }
    
    // Assign role
    if (role && security_find_role(role)) {
        strncpy(user->roles[0], role, SECURITY_ROLE_NAME_MAX - 1);
        user->role_count = 1;
    } else {
        strncpy(user->roles[0], ROLE_GUEST, SECURITY_ROLE_NAME_MAX - 1);
        user->role_count = 1;
    }
    
    user->status = USER_STATUS_ACTIVE;
    user->created_at = time(NULL);
    
    // Auto-set superuser flag for superadmin role
    if (role && strcmp(role, ROLE_SUPERADMIN) == 0) {
        user->is_superuser = true;
    }
    
    g_security->user_count++;
    
    LOG_INFO("Created user '%s' with role '%s'", username, user->roles[0]);
    security_audit_log("USER_CREATE", username, "system", "User created");
    
    // Persist immediately so users survive server restart
    security_save("security.dat");
    
    return user->user_id;
}

int security_delete_user(const char *username) {
    if (!g_security || !username) return -1;
    
    for (int i = 0; i < g_security->user_count; i++) {
        if (strcmp(g_security->users[i].username, username) == 0) {
            // Don't delete last superuser
            if (g_security->users[i].is_superuser) {
                int super_count = 0;
                for (int j = 0; j < g_security->user_count; j++) {
                    if (g_security->users[j].is_superuser) super_count++;
                }
                if (super_count <= 1) {
                    LOG_WARN("Cannot delete last superuser");
                    return -1;
                }
            }
            
            // Remove user
            memmove(&g_security->users[i], &g_security->users[i + 1],
                    (g_security->user_count - i - 1) * sizeof(User));
            g_security->user_count--;
            
            LOG_INFO("Deleted user '%s'", username);
            security_audit_log("USER_DELETE", username, "system", "User deleted");
            security_save("security.dat");
            return 0;
        }
    }
    
    return -1;
}

User* security_find_user(const char *username) {
    if (!g_security || !username) return NULL;
    
    for (int i = 0; i < g_security->user_count; i++) {
        if (strcmp(g_security->users[i].username, username) == 0) {
            return &g_security->users[i];
        }
    }
    
    return NULL;
}

int security_change_password(const char *username, const char *new_password) {
    User *user = security_find_user(username);
    if (!user) return -1;
    
    char error[128];
    if (!security_validate_password(new_password, error)) {
        return -1;
    }
    
    security_hash_password(new_password, user->salt, user->password_hash);
    user->must_change_password = false;
    
    LOG_INFO("Password changed for user '%s'", username);
    security_audit_log("PASSWORD_CHANGE", username, "system", "Password changed");
    security_save("security.dat");
    
    return 0;
}

int security_lock_user(const char *username) {
    User *user = security_find_user(username);
    if (!user) return -1;
    
    user->status = USER_STATUS_LOCKED;
    user->lockout_until = time(NULL) + g_security->lockout_duration;
    
    LOG_WARN("User '%s' locked", username);
    security_audit_log("USER_LOCK", username, "system", "Account locked");
    
    return 0;
}

int security_unlock_user(const char *username) {
    User *user = security_find_user(username);
    if (!user) return -1;
    
    user->status = USER_STATUS_ACTIVE;
    user->failed_attempts = 0;
    user->lockout_until = 0;
    
    LOG_INFO("User '%s' unlocked", username);
    security_audit_log("USER_UNLOCK", username, "system", "Account unlocked");
    
    return 0;
}

// ============================================================================
// ROLE MANAGEMENT
// ============================================================================

int security_create_role(const char *name, uint32_t permissions, const char *description) {
    if (!g_security || !name) return -1;
    
    if (g_security->role_count >= SECURITY_MAX_ROLES) return -1;
    
    // Check if exists
    if (security_find_role(name)) return -1;
    
    Role *role = &g_security->roles[g_security->role_count];
    strncpy(role->name, name, SECURITY_ROLE_NAME_MAX - 1);
    role->permissions = permissions;
    role->is_system_role = false;
    if (description) {
        strncpy(role->description, description, 127);
    }
    
    g_security->role_count++;
    return 0;
}

int security_delete_role(const char *name) {
    if (!g_security || !name) return -1;
    
    for (int i = 0; i < g_security->role_count; i++) {
        if (strcmp(g_security->roles[i].name, name) == 0) {
            if (g_security->roles[i].is_system_role) {
                LOG_WARN("Cannot delete system role '%s'", name);
                return -1;
            }
            
            memmove(&g_security->roles[i], &g_security->roles[i + 1],
                    (g_security->role_count - i - 1) * sizeof(Role));
            g_security->role_count--;
            return 0;
        }
    }
    
    return -1;
}

Role* security_find_role(const char *name) {
    if (!g_security || !name) return NULL;
    
    for (int i = 0; i < g_security->role_count; i++) {
        if (strcmp(g_security->roles[i].name, name) == 0) {
            return &g_security->roles[i];
        }
    }
    
    return NULL;
}

int security_grant_role(const char *username, const char *rolename) {
    User *user = security_find_user(username);
    Role *role = security_find_role(rolename);
    
    if (!user || !role) return -1;
    
    // Check if already has role
    for (int i = 0; i < user->role_count; i++) {
        if (strcmp(user->roles[i], rolename) == 0) return 0;
    }
    
    if (user->role_count >= 8) return -1;
    
    strncpy(user->roles[user->role_count], rolename, SECURITY_ROLE_NAME_MAX - 1);
    user->role_count++;
    
    LOG_INFO("Granted role '%s' to user '%s'", rolename, username);
    return 0;
}

int security_revoke_role(const char *username, const char *rolename) {
    User *user = security_find_user(username);
    if (!user) return -1;
    
    for (int i = 0; i < user->role_count; i++) {
        if (strcmp(user->roles[i], rolename) == 0) {
            memmove(&user->roles[i], &user->roles[i + 1],
                    (user->role_count - i - 1) * SECURITY_ROLE_NAME_MAX);
            user->role_count--;
            LOG_INFO("Revoked role '%s' from user '%s'", rolename, username);
            return 0;
        }
    }
    
    return -1;
}

uint32_t security_get_user_permissions(const char *username) {
    User *user = security_find_user(username);
    if (!user) return PERM_NONE;
    
    if (user->is_superuser) return PERM_ALL;
    
    uint32_t perms = PERM_NONE;
    for (int i = 0; i < user->role_count; i++) {
        Role *role = security_find_role(user->roles[i]);
        if (role) {
            perms |= role->permissions;
        }
    }
    
    return perms;
}

bool security_user_has_permission(const char *username, Permission perm) {
    uint32_t perms = security_get_user_permissions(username);
    return (perms & perm) == perm;
}

// ============================================================================
// SESSION MANAGEMENT
// ============================================================================

void security_generate_token(char *token) {
    uint8_t bytes[32];
    security_random_bytes(bytes, 32);
    bytes_to_hex(bytes, 32, token);
}

int security_login(const char *username, const char *password,
                   const char *client_ip, int client_port,
                   Session **out_session, char *error_msg) {
    if (!g_security || !username || !password) {
        if (error_msg) strcpy(error_msg, "Invalid parameters");
        return -1;
    }
    
    User *user = security_find_user(username);
    if (!user) {
        if (error_msg) strcpy(error_msg, "Invalid username or password");
        security_audit_log("LOGIN_FAILED", username, client_ip, "User not found");
        return -1;
    }
    
    // Check if locked
    if (user->status == USER_STATUS_LOCKED) {
        if (user->lockout_until > time(NULL)) {
            if (error_msg) sprintf(error_msg, "Account locked. Try again in %lld seconds",
                                   (long long)(user->lockout_until - time(NULL)));
            security_audit_log("LOGIN_BLOCKED", username, client_ip, "Account locked");
            return -1;
        } else {
            // Lockout expired
            user->status = USER_STATUS_ACTIVE;
            user->failed_attempts = 0;
        }
    }
    
    if (user->status != USER_STATUS_ACTIVE) {
        if (error_msg) strcpy(error_msg, "Account is disabled");
        return -1;
    }
    
    // Verify password
    if (!security_verify_password(password, user->salt, user->password_hash)) {
        user->failed_attempts++;
        
        if (user->failed_attempts >= g_security->max_failed_attempts) {
            security_lock_user(username);
            if (error_msg) strcpy(error_msg, "Account locked due to failed attempts");
        } else {
            if (error_msg) sprintf(error_msg, "Invalid password. %d attempts remaining",
                                   g_security->max_failed_attempts - user->failed_attempts);
        }
        
        security_audit_log("LOGIN_FAILED", username, client_ip, "Invalid password");
        return -1;
    }
    
    // Success - create session
    if (g_security->session_count >= SECURITY_MAX_SESSIONS) {
        security_cleanup_sessions();
        if (g_security->session_count >= SECURITY_MAX_SESSIONS) {
            if (error_msg) strcpy(error_msg, "Maximum sessions reached");
            return -1;
        }
    }
    
    Session *session = &g_security->sessions[g_security->session_count];
    memset(session, 0, sizeof(Session));
    
    security_generate_token(session->token);
    session->user_id = user->user_id;
    strncpy(session->username, username, SECURITY_USERNAME_MAX - 1);
    session->permissions = security_get_user_permissions(username);
    session->created_at = time(NULL);
    session->last_activity = session->created_at;
    session->expires_at = session->created_at + g_security->session_timeout;
    
    if (client_ip) strncpy(session->client_ip, client_ip, 63);
    session->client_port = client_port;
    strcpy(session->current_db, "public");
    
    session->is_authenticated = true;
    session->is_superuser = user->is_superuser;
    
    g_security->session_count++;
    
    // Update user
    user->failed_attempts = 0;
    user->last_login = time(NULL);
    
    if (out_session) *out_session = session;
    
    LOG_INFO("User '%s' logged in from %s:%d", username, client_ip ? client_ip : "unknown", client_port);
    security_audit_log("LOGIN_SUCCESS", username, client_ip, "Login successful");
    
    return 0;
}

void security_logout(const char *token) {
    if (!g_security || !token) return;
    
    for (int i = 0; i < g_security->session_count; i++) {
        if (strcmp(g_security->sessions[i].token, token) == 0) {
            char username[64];
            strncpy(username, g_security->sessions[i].username, 63);
            
            memmove(&g_security->sessions[i], &g_security->sessions[i + 1],
                    (g_security->session_count - i - 1) * sizeof(Session));
            g_security->session_count--;
            
            LOG_INFO("User '%s' logged out", username);
            security_audit_log("LOGOUT", username, "system", "Logout");
            return;
        }
    }
}

Session* security_find_session(const char *token) {
    if (!g_security || !token) return NULL;
    
    for (int i = 0; i < g_security->session_count; i++) {
        if (strcmp(g_security->sessions[i].token, token) == 0) {
            return &g_security->sessions[i];
        }
    }
    
    return NULL;
}

bool security_validate_session(const char *token) {
    Session *session = security_find_session(token);
    if (!session) return false;
    
    if (!session->is_authenticated) return false;
    
    if (time(NULL) > session->expires_at) {
        security_logout(token);
        return false;
    }
    
    return true;
}

void security_touch_session(const char *token) {
    Session *session = security_find_session(token);
    if (session) {
        session->last_activity = time(NULL);
        session->expires_at = session->last_activity + g_security->session_timeout;
    }
}

int security_cleanup_sessions(void) {
    if (!g_security) return 0;
    
    time_t now = time(NULL);
    int cleaned = 0;
    
    for (int i = g_security->session_count - 1; i >= 0; i--) {
        if (now > g_security->sessions[i].expires_at) {
            memmove(&g_security->sessions[i], &g_security->sessions[i + 1],
                    (g_security->session_count - i - 1) * sizeof(Session));
            g_security->session_count--;
            cleaned++;
        }
    }
    
    if (cleaned > 0) {
        LOG_DEBUG("Cleaned up %d expired sessions", cleaned);
    }
    
    return cleaned;
}

bool security_session_has_permission(Session *session, Permission perm) {
    if (!session || !session->is_authenticated) return false;
    if (session->is_superuser) return true;
    return (session->permissions & perm) == perm;
}

// ============================================================================
// AUTHORIZATION
// ============================================================================

bool security_authorize_command(Session *session, const char *cmd_type) {
    if (!session || !cmd_type) return false;
    if (session->is_superuser) return true;
    
    Permission required = PERM_NONE;
    
    if (strcmp(cmd_type, "SELECT") == 0 || strcmp(cmd_type, "DIKHAO") == 0) {
        required = PERM_SELECT;
    } else if (strcmp(cmd_type, "INSERT") == 0 || strcmp(cmd_type, "DALO") == 0) {
        required = PERM_INSERT;
    } else if (strcmp(cmd_type, "UPDATE") == 0 || strcmp(cmd_type, "BADLO") == 0) {
        required = PERM_UPDATE;
    } else if (strcmp(cmd_type, "DELETE") == 0 || strcmp(cmd_type, "HATAO") == 0) {
        required = PERM_DELETE;
    } else if (strcmp(cmd_type, "CREATE TABLE") == 0 || strcmp(cmd_type, "BANAO") == 0 ||
               strcmp(cmd_type, "TABLE") == 0 || strcmp(cmd_type, "CREATE") == 0) {
        required = PERM_CREATE_TABLE;
    } else if (strcmp(cmd_type, "DROP TABLE") == 0 || strcmp(cmd_type, "GIRAO") == 0) {
        required = PERM_DROP_TABLE;
    } else if (strcmp(cmd_type, "CREATE DATABASE") == 0) {
        required = PERM_CREATE_DB;
    } else if (strcmp(cmd_type, "DROP DATABASE") == 0) {
        required = PERM_DROP_DB;
    } else if (strcmp(cmd_type, "BEGIN") == 0 || strcmp(cmd_type, "COMMIT") == 0 || 
               strcmp(cmd_type, "ROLLBACK") == 0) {
        required = PERM_TRANSACTION;
    } else if (strcmp(cmd_type, "GRANT") == 0) {
        required = PERM_GRANT;
    } else if (strcmp(cmd_type, "REVOKE") == 0) {
        required = PERM_REVOKE;
    } else if (strcmp(cmd_type, "SHOW") == 0 || strcmp(cmd_type, "DEKHO") == 0) {
        required = PERM_SELECT;  // View requires select
    }
    
    return security_session_has_permission(session, required);
}

// ============================================================================
// AES ENCRYPTION
// ============================================================================

// AES S-box
static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

// Simplified AES-256-CBC encryption (for demonstration)
int security_aes_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                         const uint8_t *key, const uint8_t *iv,
                         uint8_t *ciphertext) {
    if (!plaintext || !key || !iv || !ciphertext) return -1;
    
    // Padding
    size_t padded_len = ((plaintext_len + 15) / 16) * 16;
    uint8_t *padded = calloc(padded_len, 1);
    memcpy(padded, plaintext, plaintext_len);
    
    // Simple XOR-based "encryption" for demo (real impl would use proper AES)
    uint8_t block[16];
    memcpy(block, iv, 16);
    
    for (size_t i = 0; i < padded_len; i += 16) {
        // XOR with previous block (CBC mode)
        for (int j = 0; j < 16; j++) {
            padded[i + j] ^= block[j];
        }
        
        // Simple substitution using S-box
        for (int j = 0; j < 16; j++) {
            ciphertext[i + j] = aes_sbox[padded[i + j] ^ key[j % 32]];
        }
        
        memcpy(block, ciphertext + i, 16);
    }
    
    free(padded);
    return (int)padded_len;
}

int security_aes_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                         const uint8_t *key, const uint8_t *iv,
                         uint8_t *plaintext) {
    (void)ciphertext;
    (void)ciphertext_len;
    (void)key;
    (void)iv;
    (void)plaintext;
    // Inverse operations for decryption
    // TODO: Implement proper AES decryption
    return -1;
}

// ============================================================================
// AUDIT LOGGING
// ============================================================================

void security_audit_log(const char *event_type, const char *username,
                        const char *client_ip, const char *details) {
    char buffer[512];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    snprintf(buffer, sizeof(buffer), "[AUDIT] %s | Event: %s | User: %s | IP: %s | %s",
             time_str, event_type, username ? username : "-",
             client_ip ? client_ip : "-", details ? details : "");
    
    LOG_INFO("%s", buffer);
    
    // Also write to audit log file
    FILE *f = fopen("security_audit.log", "a");
    if (f) {
        fprintf(f, "%s\n", buffer);
        fclose(f);
    }
}

// ============================================================================
// PERSISTENCE
// ============================================================================

int security_save(const char *filepath) {
    if (!g_security || !filepath) return -1;
    
    FILE *f = fopen(filepath, "wb");
    if (!f) return -1;
    
    // Write header
    uint32_t magic = 0x53454355;  // "SECU"
    fwrite(&magic, sizeof(magic), 1, f);
    
    // Write users
    fwrite(&g_security->user_count, sizeof(int), 1, f);
    fwrite(g_security->users, sizeof(User), g_security->user_count, f);
    
    // Write roles
    fwrite(&g_security->role_count, sizeof(int), 1, f);
    fwrite(g_security->roles, sizeof(Role), g_security->role_count, f);
    
    fclose(f);
    LOG_INFO("Security data saved to %s", filepath);
    return 0;
}

int security_load(const char *filepath) {
    if (!g_security || !filepath) return -1;
    
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x53454355) {
        fclose(f);
        return -1;
    }
    
    // Read users
    if (fread(&g_security->user_count, sizeof(int), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    if (fread(g_security->users, sizeof(User), g_security->user_count, f) != 
        (size_t)g_security->user_count) {
        fclose(f);
        return -1;
    }
    
    // Read roles  
    if (fread(&g_security->role_count, sizeof(int), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    if (fread(g_security->roles, sizeof(Role), g_security->role_count, f) != 
        (size_t)g_security->role_count) {
        fclose(f);
        return -1;
    }
    
    fclose(f);
    LOG_INFO("Security data loaded from %s", filepath);
    return 0;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char* security_permission_name(Permission perm) {
    switch (perm) {
        case PERM_CREATE_DB: return "CREATE_DATABASE";
        case PERM_DROP_DB: return "DROP_DATABASE";
        case PERM_USE_DB: return "USE_DATABASE";
        case PERM_CREATE_TABLE: return "CREATE_TABLE";
        case PERM_DROP_TABLE: return "DROP_TABLE";
        case PERM_ALTER_TABLE: return "ALTER_TABLE";
        case PERM_SELECT: return "SELECT";
        case PERM_INSERT: return "INSERT";
        case PERM_UPDATE: return "UPDATE";
        case PERM_DELETE: return "DELETE";
        case PERM_CREATE_USER: return "CREATE_USER";
        case PERM_DROP_USER: return "DROP_USER";
        case PERM_GRANT: return "GRANT";
        case PERM_REVOKE: return "REVOKE";
        case PERM_SHUTDOWN: return "SHUTDOWN";
        case PERM_RELOAD_CONFIG: return "RELOAD_CONFIG";
        case PERM_VIEW_STATS: return "VIEW_STATS";
        case PERM_BACKUP: return "BACKUP";
        case PERM_RESTORE: return "RESTORE";
        case PERM_TRANSACTION: return "TRANSACTION";
        default: return "UNKNOWN";
    }
}

void security_permissions_to_string(uint32_t perms, char *buffer, size_t size) {
    if (!buffer) return;
    buffer[0] = '\0';
    
    if (perms == PERM_ALL) {
        strncpy(buffer, "ALL", size);
        return;
    }
    
    const Permission all_perms[] = {
        PERM_CREATE_DB, PERM_DROP_DB, PERM_USE_DB, PERM_CREATE_TABLE, PERM_DROP_TABLE,
        PERM_ALTER_TABLE, PERM_SELECT, PERM_INSERT, PERM_UPDATE, PERM_DELETE,
        PERM_CREATE_USER, PERM_DROP_USER, PERM_GRANT, PERM_REVOKE, PERM_SHUTDOWN,
        PERM_RELOAD_CONFIG, PERM_VIEW_STATS, PERM_BACKUP, PERM_RESTORE, PERM_TRANSACTION
    };
    
    bool first = true;
    for (size_t i = 0; i < sizeof(all_perms) / sizeof(all_perms[0]); i++) {
        if (perms & all_perms[i]) {
            if (!first) strncat(buffer, ", ", size - strlen(buffer) - 1);
            strncat(buffer, security_permission_name(all_perms[i]), size - strlen(buffer) - 1);
            first = false;
        }
    }
}
