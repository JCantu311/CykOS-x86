#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#define ACCOUNT_NAME_MAX 24
#define ACCOUNT_PASSWORD_MAX 64
#define ACCOUNT_FLAG_SUDO 1u

void accounts_init(void);
unsigned int accounts_count(void);
int accounts_get(unsigned int index, char name[ACCOUNT_NAME_MAX], unsigned int* uid,
                 unsigned int* gid, unsigned int* flags);
int accounts_lookup(const char* name, unsigned int* uid, unsigned int* gid,
                    unsigned int* flags);
int accounts_create(const char* name, unsigned int uid, unsigned int gid, unsigned int flags);
int accounts_delete(const char* name);
int accounts_set_flags(const char* name, unsigned int flags);
int accounts_set_password(const char* name, const char* password);
int accounts_authenticate(const char* name, const char* password);

#endif
