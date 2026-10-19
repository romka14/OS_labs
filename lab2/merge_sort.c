#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MIN_PART 1000

typedef struct {
    int *a;
    int *tmp;
    int l;
    int r;
} Task;

static volatile LONG running = 1;
static volatile LONG peak = 1;
static LONG max_threads = 1;

void merge(int *a, int *tmp, int l, int m, int r) {
    int i = l, j = m, k = l;
    while (i < m && j < r) {
        if (a[i] <= a[j]) tmp[k++] = a[i++];
        else tmp[k++] = a[j++];
    }
    while (i < m) tmp[k++] = a[i++];
    while (j < r) tmp[k++] = a[j++];
    for (k = l; k < r; k++) a[k] = tmp[k];
}

void sort_seq(int *a, int *tmp, int l, int r) {
    if (r - l < 2) return;
    int m = (l + r) / 2;
    sort_seq(a, tmp, l, m);
    sort_seq(a, tmp, m, r);
    merge(a, tmp, l, m, r);
}

void sort_par(int *a, int *tmp, int l, int r);

DWORD WINAPI thread_func(LPVOID p) {
    Task *t = (Task *)p;
    sort_par(t->a, t->tmp, t->l, t->r);
    InterlockedDecrement(&running);
    return 0;
}

void sort_par(int *a, int *tmp, int l, int r) {
    if (r - l < 2) return;
    int m = (l + r) / 2;
    HANDLE h = NULL;
    Task t = {a, tmp, l, m};
    if (r - l >= MIN_PART) {
        LONG now = InterlockedIncrement(&running);
        if (now <= max_threads) {
            LONG p = peak;
            while (now > p && InterlockedCompareExchange(&peak, now, p) != p) p = peak;
            h = CreateThread(NULL, 0, thread_func, &t, 0, NULL);
            if (h == NULL) InterlockedDecrement(&running);
        } else {
            InterlockedDecrement(&running);
        }
    }
    if (h != NULL) {
        sort_par(a, tmp, m, r);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    } else {
        sort_par(a, tmp, l, m);
        sort_par(a, tmp, m, r);
    }
    merge(a, tmp, l, m, r);
}

double now_ms() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

int is_sorted(int *a, int n) {
    for (int i = 1; i < n; i++)
        if (a[i - 1] > a[i]) return 0;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s <array size> <max threads> [pause]\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    max_threads = atoi(argv[2]);
    if (n <= 0 || max_threads <= 0) return 1;

    int *orig = malloc(n * sizeof(int));
    int *a = malloc(n * sizeof(int));
    int *tmp = malloc(n * sizeof(int));
    srand(12345);
    for (int i = 0; i < n; i++) orig[i] = (rand() << 15) ^ rand();

    for (int i = 0; i < n; i++) a[i] = orig[i];
    double t0 = now_ms();
    sort_seq(a, tmp, 0, n);
    double seq = now_ms() - t0;
    printf("sequential: %.3f ms, sorted=%d\n", seq, is_sorted(a, n));

    if (argc > 3) {
        printf("pid %lu, press Enter to start parallel sort\n", GetCurrentProcessId());
        getchar();
    }

    for (int i = 0; i < n; i++) a[i] = orig[i];
    t0 = now_ms();
    sort_par(a, tmp, 0, n);
    double par = now_ms() - t0;
    double s = seq / par;
    printf("parallel: threads=%ld time=%.3f ms speedup=%.3f efficiency=%.3f peak=%ld sorted=%d\n",
           max_threads, par, s, s / max_threads, peak, is_sorted(a, n));

    free(orig);
    free(a);
    free(tmp);
    return 0;
}
