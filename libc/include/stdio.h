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

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

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
    int   ungetc_char; /* -1 if empty */
} FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* pathname, const char* mode);
FILE* fdopen(int fd, const char* mode);
FILE* freopen(const char* pathname, const char* mode, FILE* stream);
int   fclose(FILE* stream);
int   fflush(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int   fseek(FILE* stream, long offset, int whence);
long  ftell(FILE* stream);
void  rewind(FILE* stream);
int   feof(FILE* stream);
int   ferror(FILE* stream);
void  clearerr(FILE* stream);
void  perror(const char* s);
int   setvbuf(FILE* stream, char* buf, int mode, size_t size);
void  setbuf(FILE* stream, char* buf);
int   fileno(FILE* stream);
int   remove(const char* pathname);
int   rename(const char* old_path, const char* new_path);
FILE* tmpfile(void);
char* tmpnam(char* s);
FILE* popen(const char* command, const char* type);
int   pclose(FILE* stream);

int   fgetc(FILE* stream);
int   getc(FILE* stream);
int   ungetc(int c, FILE* stream);
char* fgets(char* s, int size, FILE* stream);
int   fputc(int c, FILE* stream);
int   putc(int c, FILE* stream);
int   fputs(const char* s, FILE* stream);
int   getchar(void);
int   putchar(int c);
int   puts(const char* s);
#define getc_unlocked(s)    getc(s)
#define putc_unlocked(c, s) putc(c, s)
#define getchar_unlocked()  getchar()
#define putchar_unlocked(c) putchar(c)
int   sscanf(const char* input, const char* format, ...);
int   vsscanf(const char* input, const char* format, va_list ap);
int   fscanf(FILE* stream, const char* format, ...);
int   vfscanf(FILE* stream, const char* format, va_list ap);
int   scanf(const char* format, ...);

int   printf(const char* format, ...);
int   fprintf(FILE* stream, const char* format, ...);
int   sprintf(char* str, const char* format, ...);
int   snprintf(char* str, size_t size, const char* format, ...);
int   vprintf(const char* format, va_list ap);
int   vfprintf(FILE* stream, const char* format, va_list ap);
int   vsprintf(char* str, const char* format, va_list ap);
int   vsnprintf(char* str, size_t size, const char* format, va_list ap);

#endif
