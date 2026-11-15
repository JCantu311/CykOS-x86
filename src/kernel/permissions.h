#ifndef PERMISSIONS_H
#define PERMISSIONS_H

#define CYK_PERMISSION_READ  4
#define CYK_PERMISSION_WRITE 2
#define CYK_PERMISSION_EXEC  1

void permissions_init(void);
unsigned int permissions_uid(void);
unsigned int permissions_gid(void);
int permissions_set_uid(unsigned int uid);
int permissions_set_gid(unsigned int gid);
int permissions_check(const char* path, unsigned int requested);
int permissions_get(const char* path, unsigned int* mode, unsigned int* uid, unsigned int* gid);
int permissions_chmod(const char* path, unsigned int mode);
int permissions_chown(const char* path, unsigned int uid, unsigned int gid);
int permissions_create(const char* path, unsigned int mode, int is_dir);

#endif
