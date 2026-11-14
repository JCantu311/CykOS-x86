#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static FILE _stdin  = { .fd = STDIN_FILENO,  .flags = 0, .eof = 0, .error = 0, .buf_pos = 0, .buf_len = 0, .mode = 0 };
static FILE _stdout = { .fd = STDOUT_FILENO, .flags = 0, .eof = 0, .error = 0, .buf_pos = 0, .buf_len = 0, .mode = 1 };
static FILE _stderr = { .fd = STDERR_FILENO, .flags = 0, .eof = 0, .error = 0, .buf_pos = 0, .buf_len = 0, .mode = 1 };

FILE* stdin  = &_stdin;
FILE* stdout = &_stdout;
FILE* stderr = &_stderr;

FILE* fopen(const char* pathname, const char* mode) {
    int flags = 0;
    if (strchr(mode, '+')) {
        flags = O_RDWR;
    } else if (*mode == 'r') {
        flags = O_RDONLY;
    } else if (*mode == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (*mode == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }

    int fd = open(pathname, flags, 0666);
    if (fd < 0) return NULL;

    FILE* fp = (FILE*)malloc(sizeof(FILE));
    if (!fp) {
        close(fd);
        return NULL;
    }

    fp->fd = fd;
    fp->flags = flags;
    fp->eof = 0;
    fp->error = 0;
    fp->buf_pos = 0;
    fp->buf_len = 0;
    fp->mode = (*mode == 'r') ? 0 : 1;
    return fp;
}

int fflush(FILE* stream) {
    if (!stream) return 0;
    if (stream->mode == 1 && stream->buf_pos > 0) {
        ssize_t written = write(stream->fd, stream->buf, stream->buf_pos);
        if (written < 0) {
            stream->error = 1;
            return EOF;
        }
        stream->buf_pos = 0;
    }
    return 0;
}

int fclose(FILE* stream) {
    if (!stream) return EOF;
    fflush(stream);
    int ret = close(stream->fd);
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return (ret == 0) ? 0 : EOF;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    size_t bytes_read = 0;
    char* dest = (char*)ptr;

    while (bytes_read < total_bytes) {
        if (stream->buf_pos >= stream->buf_len) {
            ssize_t r = read(stream->fd, stream->buf, BUFSIZ);
            if (r <= 0) {
                if (r == 0) stream->eof = 1;
                else stream->error = 1;
                break;
            }
            stream->buf_pos = 0;
            stream->buf_len = (int)r;
        }

        size_t available = stream->buf_len - stream->buf_pos;
        size_t needed = total_bytes - bytes_read;
        size_t to_copy = (needed < available) ? needed : available;

        memcpy(dest + bytes_read, stream->buf + stream->buf_pos, to_copy);
        stream->buf_pos += to_copy;
        bytes_read += to_copy;
    }

    return bytes_read / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    const char* src = (const char*)ptr;

    for (size_t i = 0; i < total_bytes; i++) {
        stream->buf[stream->buf_pos++] = src[i];
        if (stream->buf_pos >= BUFSIZ || (stream == stdout && src[i] == '\n')) {
            if (fflush(stream) == EOF) {
                return i / size;
            }
        }
    }
    return nmemb;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    fflush(stream);
    stream->buf_pos = 0;
    stream->buf_len = 0;
    stream->eof = 0;
    off_t ret = lseek(stream->fd, (off_t)offset, whence);
    return (ret == (off_t)-1) ? -1 : 0;
}

long ftell(FILE* stream) {
    if (!stream) return -1;
    off_t cur = lseek(stream->fd, 0, SEEK_CUR);
    if (cur == (off_t)-1) return -1;
    return (long)(cur - (stream->buf_len - stream->buf_pos));
}

int feof(FILE* stream) {
    return stream ? stream->eof : 0;
}

int ferror(FILE* stream) {
    return stream ? stream->error : 0;
}

int fgetc(FILE* stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) {
        return (int)c;
    }
    return EOF;
}

char* fgets(char* s, int size, FILE* stream) {
    if (!s || size <= 0 || !stream) return NULL;
    int idx = 0;
    while (idx < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (idx == 0) return NULL;
            break;
        }
        s[idx++] = (char)c;
        if (c == '\n') break;
    }
    s[idx] = '\0';
    return s;
}

int fputc(int c, FILE* stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1) {
        return ch;
    }
    return EOF;
}

int fputs(const char* s, FILE* stream) {
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) == len) {
        return 0;
    }
    return EOF;
}

int getchar(void) {
    return fgetc(stdin);
}

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char* s) {
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 0;
}
