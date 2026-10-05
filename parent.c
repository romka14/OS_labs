#include <windows.h>
#include <string.h>

static HANDLE std_in, std_out, std_err;

void put_str(HANDLE h, const char *s) {
    DWORD written;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
}

void put_ulong(HANDLE h, unsigned long n) {
    char buf[24];
    int i = sizeof(buf) - 1;

    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n != 0);
    put_str(h, buf + i);
}

void error_exit(const char *message) {
    DWORD code = GetLastError();

    put_str(std_err, message);
    put_str(std_err, " failed, error code ");
    put_ulong(std_err, code);
    put_str(std_err, "\n");
    ExitProcess(1);
}

void send_data(HANDLE pipe, const char *data, DWORD size) {
    DWORD written;

    if (!WriteFile(pipe, data, size, &written, NULL) || written != size)
        error_exit("WriteFile");
}

int read_stdin_line(char *buffer, int size, int *eof) {
    int len = 0;
    DWORD read_count;
    char c;

    while (len < size - 1) {
        if (!ReadFile(std_in, &c, 1, &read_count, NULL) || read_count == 0 ||
            c == 0x1A) {
            *eof = 1;
            break;
        }
        buffer[len++] = c;
        if (c == '\n')
            break;
    }

    buffer[len] = '\0';
    return len;
}

int read_reply(HANDLE pipe, char *buffer, int size) {
    int len = 0;
    DWORD read_count;
    char c;

    while (len < size - 1) {
        if (!ReadFile(pipe, &c, 1, &read_count, NULL) || read_count == 0)
            return 0;
        if (c == '\n')
            break;
        if (c != '\r')
            buffer[len++] = c;
    }

    buffer[len] = '\0';
    return 1;
}

int main(void) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE pipe1_read, pipe1_write, pipe2_read, pipe2_write;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char file_name[MAX_PATH];
    char command[MAX_PATH + 16];
    char line[4096];
    char reply[64];
    DWORD exit_code;
    int eof = 0;
    int len;

    std_in = GetStdHandle(STD_INPUT_HANDLE);
    std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    std_err = GetStdHandle(STD_ERROR_HANDLE);

    put_str(std_out, "Enter file name: ");
    len = read_stdin_line(file_name, sizeof(file_name), &eof);
    if (len == 0) {
        put_str(std_err, "File name was not entered\n");
        return 1;
    }
    file_name[strcspn(file_name, "\r\n")] = '\0';
    if (file_name[0] == '\0') {
        put_str(std_err, "File name is empty\n");
        return 1;
    }

    if (!CreatePipe(&pipe1_read, &pipe1_write, &sa, 0))
        error_exit("CreatePipe (pipe1)");
    if (!CreatePipe(&pipe2_read, &pipe2_write, &sa, 0))
        error_exit("CreatePipe (pipe2)");
    if (!SetHandleInformation(pipe1_write, HANDLE_FLAG_INHERIT, 0))
        error_exit("SetHandleInformation (pipe1)");
    if (!SetHandleInformation(pipe2_read, HANDLE_FLAG_INHERIT, 0))
        error_exit("SetHandleInformation (pipe2)");

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = pipe1_read;
    si.hStdOutput = pipe2_write;
    si.hStdError = std_err;
    ZeroMemory(&pi, sizeof(pi));

    lstrcpyA(command, "child.exe \"");
    lstrcatA(command, file_name);
    lstrcatA(command, "\"");
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        error_exit("CreateProcess");

    CloseHandle(pi.hThread);
    CloseHandle(pipe1_read);
    CloseHandle(pipe2_write);

    eof = 0;
    put_str(std_out, "Enter numbers (Ctrl+Z to finish):\n");
    while ((len = read_stdin_line(line, sizeof(line), &eof)) > 0) {
        int line_ended = line[len - 1] == '\n';

        send_data(pipe1_write, line, (DWORD)len);
        if (!line_ended && !eof)
            continue;
        if (!line_ended)
            send_data(pipe1_write, "\n", 1);

        if (!read_reply(pipe2_read, reply, sizeof(reply))) {
            put_str(std_err, "Child process terminated unexpectedly\n");
            break;
        }
        if (strcmp(reply, "div0") == 0) {
            put_str(std_out, "Division by zero. Terminating.\n");
            break;
        }
        if (strcmp(reply, "error") == 0)
            put_str(std_out, "Invalid input, expected integers\n");
    }

    CloseHandle(pipe1_write);
    CloseHandle(pipe2_read);

    if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED)
        error_exit("WaitForSingleObject");
    if (!GetExitCodeProcess(pi.hProcess, &exit_code))
        error_exit("GetExitCodeProcess");
    CloseHandle(pi.hProcess);

    if (exit_code != 0) {
        put_str(std_err, "Child process exited with code ");
        put_ulong(std_err, exit_code);
        put_str(std_err, "\n");
        return 1;
    }
    return 0;
}
