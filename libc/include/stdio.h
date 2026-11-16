#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

#define EOF (-1)

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#define BUFSIZ 512

typedef struct FILE {
    int   fd;
    int   flags;
    int   eof;
    int   error;
    char  buf[BUFSIZ];
    char *write_buf;
    size_t write_buf_capacity;
    int   buf_pos;
    int   buf_len;
    int   mode; /* 0 = read, 1 = write */
} FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* pathname, const char* mode);
FILE* freopen(const char* pathname, const char* mode, FILE* stream);
int   fclose(FILE* stream);
int   fflush(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int   fseek(FILE* stream, long offset, int whence);
long  ftell(FILE* stream);
int   feof(FILE* stream);
int   ferror(FILE* stream);
int   fileno(FILE* stream);
int   remove(const char* pathname);
int   rename(const char* old_path, const char* new_path);

int   fgetc(FILE* stream);
int   getc(FILE* stream);
char* fgets(char* s, int size, FILE* stream);
int   fputc(int c, FILE* stream);
int   fputs(const char* s, FILE* stream);
int   getchar(void);
int   putchar(int c);
int   puts(const char* s);
int   sscanf(const char* input, const char* format, ...);

int   printf(const char* format, ...);
int   fprintf(FILE* stream, const char* format, ...);
int   sprintf(char* str, const char* format, ...);
int   snprintf(char* str, size_t size, const char* format, ...);
int   vprintf(const char* format, va_list ap);
int   vfprintf(FILE* stream, const char* format, va_list ap);
int   vsprintf(char* str, const char* format, va_list ap);
int   vsnprintf(char* str, size_t size, const char* format, va_list ap);

#endif
