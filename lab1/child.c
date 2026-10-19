#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

enum { STATUS_OK, 
    STATUS_ERROR, 
    STATUS_DIV0 };

static HANDLE std_in, std_out, std_err;

void put_str(HANDLE h, const char *s) {
    DWORD written;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
}

void fail(const char *message) {
    DWORD code = GetLastError();
    char buf[24];
    int i = sizeof(buf) - 1;

    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + code % 10);
        code /= 10;
    } while (code != 0);

    put_str(std_err, message);
    put_str(std_err, " failed, error code ");
    put_str(std_err, buf + i);
    put_str(std_err, "\n");
}

int write_int_line(HANDLE h, int value) {
    char buf[16];
    int i = sizeof(buf) - 1;
    unsigned int n = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
    DWORD written, size;

    buf[i] = '\n';
    do {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n != 0);
    if (value < 0)
        buf[--i] = '-';

    size = (DWORD)(sizeof(buf) - i);
    return WriteFile(h, buf + i, size, &written, NULL) && written == size;
}

int read_char(void) {
    char c;
    DWORD read_count;

    if (!ReadFile(std_in, &c, 1, &read_count, NULL) || read_count == 0)
        return -1;
    return (unsigned char)c;
}

char *read_line(void) {
    size_t size = 64, len = 0;
    char *line = malloc(size);
    int c;

    if (line == NULL) {
        put_str(std_err, "malloc failed\n");
        exit(1);
    }

    while ((c = read_char()) != -1 && c != '\n') {
        if (len + 1 == size) {
            char *tmp;
            size *= 2;
            tmp = realloc(line, size);
            if (tmp == NULL) {
                put_str(std_err, "realloc failed\n");
                free(line);
                exit(1);
            }
            line = tmp;
        }
        line[len++] = (char)c;
    }

    if (c == -1 && len == 0) {
        free(line);
        return NULL;
    }

    line[len] = '\0';
    return line;
}

int parse_int(const char *token, int *value) {
    char *end;
    long number;

    errno = 0;
    number = strtol(token, &end, 10);
    if (errno != 0 || *end != '\0' || number < INT_MIN || number > INT_MAX)
        return 0;

    *value = (int)number;
    return 1;
}

int divide_line(char *line, int *result) {
    const char *delims = " \t\r";
    char *token = strtok(line, delims);
    int divisor;

    if (token == NULL || !parse_int(token, result))
        return STATUS_ERROR;

    while ((token = strtok(NULL, delims)) != NULL) {
        if (!parse_int(token, &divisor))
            return STATUS_ERROR;
        if (divisor == 0)
            return STATUS_DIV0;
        if (*result == INT_MIN && divisor == -1)
            return STATUS_ERROR;
        *result /= divisor;
    }

    return STATUS_OK;
}

int main(int argc, char *argv[]) {
    HANDLE out;
    char *line;

    std_in = GetStdHandle(STD_INPUT_HANDLE);
    std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    std_err = GetStdHandle(STD_ERROR_HANDLE);

    if (argc != 2) {
        put_str(std_err, "Usage: ");
        put_str(std_err, argv[0]);
        put_str(std_err, " <file>\n");
        return 1;
    }

    out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        fail("CreateFile");
        return 1;
    }

    while ((line = read_line()) != NULL) {
        int result;
        int status = divide_line(line, &result);
        free(line);

        if (status == STATUS_DIV0) {
            put_str(std_out, "div0\n");
            CloseHandle(out);
            return 0;
        }

        if (status == STATUS_OK) {
            if (!write_int_line(out, result)) {
                fail("write to file");
                CloseHandle(out);
                return 1;
            }
            put_str(std_out, "ok\n");
        } else {
            put_str(std_out, "error\n");
        }
    }

    if (!CloseHandle(out)) {
        fail("CloseHandle");
        return 1;
    }
    return 0;
}
