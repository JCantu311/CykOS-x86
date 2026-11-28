#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#define FILE_WRITE_BUFFER_CAPACITY (256u * 1024u)

static FILE _stdin  = { .fd = STDIN_FILENO,  .flags = 0, .eof = 0, .error = 0, .buf_pos = 0, .buf_len = 0, .mode = 0, .ungetc_char = -1 };
static FILE _stdout = { .fd = STDOUT_FILENO, .flags = 0, .eof = 0, .error = 0, .buf_pos = 0, .buf_len = 0, .mode = 1, .ungetc_char = -1 };
static FILE _stderr = { .fd = STDERR_FILENO, .flags = 0, .eof = 0, .error = 0, .buf_pos = 0, .buf_len = 0, .mode = 1, .ungetc_char = -1 };

FILE* stdin  = &_stdin;
FILE* stdout = &_stdout;
FILE* stderr = &_stderr;

int fileno(FILE* stream) {
    return stream ? stream->fd : -1;
}

int remove(const char* pathname) {
    return unlink(pathname);
}

int rename(const char* old_path, const char* new_path) {
    int source = open(old_path, O_RDONLY);
    if (source < 0) return -1;
    int destination = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (destination < 0) {
        close(source);
        return -1;
    }

    char buffer[512];
    int result = 0;
    ssize_t count;
    while ((count = read(source, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(destination, buffer + offset, (size_t)(count - offset));
            if (written <= 0) {
                result = -1;
                break;
            }
            offset += written;
        }
        if (result != 0) break;
    }
    if (count < 0) result = -1;
    if (close(source) != 0) result = -1;
    if (close(destination) != 0) result = -1;
    if (result == 0 && unlink(old_path) != 0) result = -1;
    return result;
}

FILE* fopen(const char* pathname, const char* mode) {
    int flags = 0;
    int plus = (strchr(mode, '+') != NULL);
    if (*mode == 'r') {
        flags = plus ? O_RDWR : O_RDONLY;
    } else if (*mode == 'w') {
        flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    } else if (*mode == 'a') {
        flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
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
    fp->write_buf = 0;
    fp->write_buf_capacity = 0;
    if (*mode != 'r') {
        fp->write_buf = (char*)malloc(FILE_WRITE_BUFFER_CAPACITY);
        if (fp->write_buf) fp->write_buf_capacity = FILE_WRITE_BUFFER_CAPACITY;
    }
    fp->buf_pos = 0;
    fp->buf_len = 0;
    fp->mode = (*mode == 'r') ? 0 : 1;
    fp->ungetc_char = -1;
    return fp;
}

FILE* fdopen(int fd, const char* mode) {
    if (fd < 0 || !mode) return NULL;
    FILE* fp = (FILE*)malloc(sizeof(FILE));
    if (!fp) return NULL;
    fp->fd = fd;
    fp->flags = (*mode == 'r') ? O_RDONLY : O_WRONLY;
    fp->eof = 0;
    fp->error = 0;
    fp->write_buf = 0;
    fp->write_buf_capacity = 0;
    fp->buf_pos = 0;
    fp->buf_len = 0;
    fp->mode = (*mode == 'r') ? 0 : 1;
    fp->ungetc_char = -1;
    return fp;
}

FILE* freopen(const char* pathname, const char* mode, FILE* stream) {
    if (!pathname || !mode || !stream) return NULL;
    if (fclose(stream) != 0) return NULL;
    return fopen(pathname, mode);
}

int fflush(FILE* stream) {
    if (!stream) return 0;
    if (stream->mode == 1 && stream->buf_pos > 0) {
        const char* data = stream->write_buf ? stream->write_buf : stream->buf;
        size_t remaining = (size_t)stream->buf_pos;
        size_t offset = 0;
        while (remaining > 0) {
            ssize_t written = write(stream->fd, data + offset, remaining);
            if (written <= 0) {
                stream->error = 1;
                return EOF;
            }
            offset += (size_t)written;
            remaining -= (size_t)written;
        }
        stream->buf_pos = 0;
    }
    return 0;
}

int fclose(FILE* stream) {
    if (!stream) return EOF;
    fflush(stream);
    int ret = close(stream->fd);
    if (stream->write_buf) free(stream->write_buf);
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

    if (stream->ungetc_char >= 0 && total_bytes > 0) {
        dest[bytes_read++] = (char)stream->ungetc_char;
        stream->ungetc_char = -1;
    }

    while (bytes_read < total_bytes) {
        if (stream->buf_pos >= stream->buf_len) {
            size_t remaining = total_bytes - bytes_read;
            if (remaining >= BUFSIZ) {
                size_t direct_chunk = remaining > (512u * 1024u) ? (512u * 1024u) : remaining;
                ssize_t r = read(stream->fd, dest + bytes_read, direct_chunk);
                if (r <= 0) {
                    if (r == 0) stream->eof = 1;
                    else stream->error = 1;
                    break;
                }
                bytes_read += (size_t)r;
                continue;
            }
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

    if (stream->write_buf && stream->write_buf_capacity) {
        size_t written_total = 0;
        while (written_total < total_bytes) {
            size_t room = stream->write_buf_capacity - (size_t)stream->buf_pos;
            size_t chunk = total_bytes - written_total;
            if (chunk > room) chunk = room;
            memcpy(stream->write_buf + stream->buf_pos, src + written_total, chunk);
            stream->buf_pos += (int)chunk;
            written_total += chunk;
            if ((size_t)stream->buf_pos == stream->write_buf_capacity &&
                fflush(stream) == EOF) {
                return written_total / size;
            }
        }
        return nmemb;
    }

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
    stream->ungetc_char = -1;
    off_t ret = lseek(stream->fd, (off_t)offset, whence);
    return (ret == (off_t)-1) ? -1 : 0;
}

long ftell(FILE* stream) {
    if (!stream) return -1;
    off_t cur = lseek(stream->fd, 0, SEEK_CUR);
    if (cur == (off_t)-1) return -1;
    long adj = (long)(stream->buf_len - stream->buf_pos) + (stream->ungetc_char >= 0 ? 1 : 0);
    return (long)cur - adj;
}

void rewind(FILE* stream) {
    if (!stream) return;
    fseek(stream, 0, SEEK_SET);
    stream->error = 0;
}

int feof(FILE* stream) {
    return stream ? stream->eof : 0;
}

int ferror(FILE* stream) {
    return stream ? stream->error : 0;
}

void clearerr(FILE* stream) {
    if (!stream) return;
    stream->eof = 0;
    stream->error = 0;
}

void perror(const char* s) {
    const char* msg = strerror(errno);
    if (s && s[0]) {
        fprintf(stderr, "%s: %s\n", s, msg);
    } else {
        fprintf(stderr, "%s\n", msg);
    }
}

int setvbuf(FILE* stream, char* buf, int mode, size_t size) {
    (void)stream; (void)buf; (void)mode; (void)size;
    return 0;
}

void setbuf(FILE* stream, char* buf) {
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

int ungetc(int c, FILE* stream) {
    if (!stream || c == EOF || stream->ungetc_char >= 0) return EOF;
    stream->ungetc_char = (unsigned char)c;
    stream->eof = 0;
    return stream->ungetc_char;
}

int fgetc(FILE* stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) {
        return (int)c;
    }
    return EOF;
}

int getc(FILE* stream) {
    return fgetc(stream);
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

/* ----------- tmpfile / tmpnam ----------- */

static unsigned int g_tmpnam_seq = 0;

FILE* tmpfile(void) {
    char name[32];
    snprintf(name, sizeof(name), "/TMP/TF%06u", g_tmpnam_seq++);
    int fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return NULL;
    unlink(name); /* delete-on-close semantics (best effort) */
    FILE* fp = fdopen(fd, "w+");
    return fp;
}

char* tmpnam(char* s) {
    static char g_tmpnam_buf[32];
    char* buf = s ? s : g_tmpnam_buf;
    snprintf(buf, 32, "/TMP/TN%06u", g_tmpnam_seq++);
    return buf;
}

/* ----------- popen / pclose ----------- */

FILE* popen(const char* command, const char* type) {
    if (!command || !type) return NULL;
    int pfd[2];
    if (pipe(pfd) != 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }

    if (pid == 0) {
        /* child */
        if (*type == 'r') {
            close(pfd[0]);
            dup2(pfd[1], STDOUT_FILENO);
            close(pfd[1]);
        } else {
            close(pfd[1]);
            dup2(pfd[0], STDIN_FILENO);
            close(pfd[0]);
        }
        char* argv[4];
        argv[0] = "/USR/BIN/SH.BIN";
        argv[1] = "-c";
        argv[2] = (char*)command;
        argv[3] = NULL;
        execv(argv[0], argv);
        _exit(127);
    }

    /* parent */
    FILE* fp;
    if (*type == 'r') {
        close(pfd[1]);
        fp = fdopen(pfd[0], "r");
    } else {
        close(pfd[0]);
        fp = fdopen(pfd[1], "w");
    }
    return fp;
}

int pclose(FILE* stream) {
    if (!stream) return -1;
    int fd = stream->fd;
    fclose(stream);
    int status = 0;
    waitpid(-1, &status, 0);
    (void)fd;
    return WEXITSTATUS(status);
}
