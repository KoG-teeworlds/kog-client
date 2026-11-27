// Counts the allocations of a process by call site (macOS interposer).
// cc -dynamiclib -undefined dynamic_lookup -o libmalloc_stacks.dylib malloc_stacks.c ; MALLOC_STACKS_FILE=out DYLD_INSERT_LIBRARIES=./libmalloc_stacks.dylib program
// Every 2 seconds the table is written to <file>.<n> ("count frame frame ..." per line, addresses in hex, a line F gives the frames swapped so far, lines starting with I list the
// images (load address, path)), see allocs_by_site.py for the evaluation.
#include <dlfcn.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEPTH 24
#define SKIP 2
#define SLOTS (1 << 16)

typedef struct
{
	_Atomic uint64_t m_Hash;
	_Atomic unsigned long m_Count;
	void *m_apFrames[DEPTH];
} Slot;

static Slot g_aSlots[SLOTS];
static _Atomic unsigned long g_Frames;

extern void SDL_GL_SwapWindow(void *pWindow);
static void CountedSwap(void *pWindow)
{
	atomic_fetch_add(&g_Frames, 1);
	SDL_GL_SwapWindow(pWindow);
}
static _Thread_local int t_Busy;

static void Record(void)
{
	if(t_Busy)
		return;
	t_Busy = 1;
	void *apFrames[DEPTH + SKIP];
	int Num = backtrace(apFrames, DEPTH + SKIP);
	uint64_t Hash = 1469598103934665603ULL;
	for(int i = SKIP; i < Num; i++)
		Hash = (Hash ^ (uint64_t)(uintptr_t)apFrames[i]) * 1099511628211ULL;
	Hash |= 1;
	for(unsigned i = Hash % SLOTS, n = 0; n < SLOTS; i = (i + 1) % SLOTS, n++)
	{
		uint64_t Expected = 0;
		if(atomic_compare_exchange_strong(&g_aSlots[i].m_Hash, &Expected, Hash))
		{
			memset(g_aSlots[i].m_apFrames, 0, sizeof(g_aSlots[i].m_apFrames));
			for(int k = SKIP; k < Num; k++)
				g_aSlots[i].m_apFrames[k - SKIP] = apFrames[k];
			atomic_fetch_add(&g_aSlots[i].m_Count, 1);
			break;
		}
		if(Expected == Hash)
		{
			atomic_fetch_add(&g_aSlots[i].m_Count, 1);
			break;
		}
	}
	t_Busy = 0;
}

static void *CountedMalloc(size_t Size)
{
	Record();
	return malloc(Size);
}
static void *CountedCalloc(size_t Count, size_t Size)
{
	Record();
	return calloc(Count, Size);
}
static void *CountedRealloc(void *p, size_t Size)
{
	Record();
	return realloc(p, Size);
}

#define INTERPOSE(Replacement, Original) \
	__attribute__((used)) static struct { const void *m_pReplacement; const void *m_pOriginal; } Interpose_##Original \
		__attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&Replacement, (const void *)(unsigned long)&Original}

INTERPOSE(CountedMalloc, malloc);
INTERPOSE(CountedCalloc, calloc);
INTERPOSE(CountedRealloc, realloc);
INTERPOSE(CountedSwap, SDL_GL_SwapWindow);

static struct
{
	void *m_pAddress;
	const char *m_pName;
} g_aNames[1 << 15];

static const char *NameOf(void *pAddress)
{
	unsigned Index = (unsigned)(((uintptr_t)pAddress >> 2) * 2654435761u) >> 17;
	if(g_aNames[Index].m_pAddress != pAddress)
	{
		Dl_info Info;
		g_aNames[Index].m_pAddress = pAddress;
		const char *pName = dladdr(pAddress, &Info) && Info.dli_sname ? Info.dli_sname : "-";
		static char s_aStore[1 << 15][64];
		char *pStored = s_aStore[Index];
		for(int i = 0; i < 63 && pName[i]; i++)
			pStored[i] = pName[i] == ' ' ? '_' : pName[i];
		g_aNames[Index].m_pName = pStored;
	}
	return g_aNames[Index].m_pName;
}

static void *Writer(void *pArg)
{
	const char *pPath = getenv("MALLOC_STACKS_FILE");
	if(!pPath)
		return NULL;
	for(int n = 1;; n++)
	{
		sleep(2);
		t_Busy = 1;
		char aName[1024];
		snprintf(aName, sizeof(aName), "%s.%d", pPath, n);
		FILE *pFile = fopen(aName, "w");
		if(!pFile)
			continue;
		fprintf(pFile, "F %lu\n", atomic_load(&g_Frames));
		for(uint32_t i = 0; i < _dyld_image_count(); i++)
			fprintf(pFile, "I %p %s\n", (void *)_dyld_get_image_header(i), _dyld_get_image_name(i));
		for(int i = 0; i < SLOTS; i++)
		{
			if(!atomic_load(&g_aSlots[i].m_Hash))
				continue;
			fprintf(pFile, "%lu", atomic_load(&g_aSlots[i].m_Count));
			for(int k = 0; k < DEPTH && g_aSlots[i].m_apFrames[k]; k++)
			{
				fprintf(pFile, " %p=%s", g_aSlots[i].m_apFrames[k], NameOf(g_aSlots[i].m_apFrames[k]));
			}
			fprintf(pFile, "\n");
		}
		fclose(pFile);
		t_Busy = 0;
	}
	return NULL;
}

__attribute__((constructor)) static void Start(void)
{
	void *apFrames[4];
	backtrace(apFrames, 4);
	pthread_t Thread;
	pthread_create(&Thread, NULL, Writer, NULL);
	pthread_detach(Thread);
}
