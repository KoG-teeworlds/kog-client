// Counts the calls of malloc, calloc and realloc of a process and prints them at exit (macOS interposer).
// cc -dynamiclib -o libmalloc_count.dylib malloc_count.c ; DYLD_INSERT_LIBRARIES=./libmalloc_count.dylib program
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic unsigned long g_Malloc, g_Calloc, g_Realloc;

static void *CountedMalloc(size_t Size)
{
	atomic_fetch_add_explicit(&g_Malloc, 1, memory_order_relaxed);
	return malloc(Size);
}
static void *CountedCalloc(size_t Count, size_t Size)
{
	atomic_fetch_add_explicit(&g_Calloc, 1, memory_order_relaxed);
	return calloc(Count, Size);
}
static void *CountedRealloc(void *p, size_t Size)
{
	atomic_fetch_add_explicit(&g_Realloc, 1, memory_order_relaxed);
	return realloc(p, Size);
}

#define INTERPOSE(Replacement, Original) \
	__attribute__((used)) static struct { const void *m_pReplacement; const void *m_pOriginal; } Interpose_##Original \
		__attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&Replacement, (const void *)(unsigned long)&Original}

INTERPOSE(CountedMalloc, malloc);
INTERPOSE(CountedCalloc, calloc);
INTERPOSE(CountedRealloc, realloc);

// The process may leave without running destructors, so a thread writes the counters to the file named by MALLOC_COUNT_FILE
// (one line "seconds malloc calloc realloc" every 500 ms)
#include <pthread.h>
#include <time.h>
#include <unistd.h>

static void *Writer(void *pArg)
{
	const char *pPath = getenv("MALLOC_COUNT_FILE");
	FILE *pFile = pPath ? fopen(pPath, "w") : NULL;
	if(!pFile)
		return NULL;
	struct timespec Start;
	clock_gettime(CLOCK_MONOTONIC, &Start);
	for(;;)
	{
		struct timespec Now;
		clock_gettime(CLOCK_MONOTONIC, &Now);
		fprintf(pFile, "%.2f %lu %lu %lu\n", (Now.tv_sec - Start.tv_sec) + (Now.tv_nsec - Start.tv_nsec) / 1e9, g_Malloc, g_Calloc, g_Realloc);
		fflush(pFile);
		usleep(500000);
	}
	return NULL;
}

__attribute__((constructor)) static void Start(void)
{
	pthread_t Thread;
	pthread_create(&Thread, NULL, Writer, NULL);
	pthread_detach(Thread);
}
