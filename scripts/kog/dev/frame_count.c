// Counts the buffer swaps (frames) of a process and writes the total to the file named by FRAME_COUNT_FILE once a second
// (macOS interposer, no allocation is touched).
// cc -dynamiclib -undefined dynamic_lookup -o libframe_count.dylib frame_count.c
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static _Atomic unsigned long g_Frames;

extern void SDL_GL_SwapWindow(void *pWindow);
static void CountedSwap(void *pWindow)
{
	atomic_fetch_add_explicit(&g_Frames, 1, memory_order_relaxed);
	SDL_GL_SwapWindow(pWindow);
}

__attribute__((used)) static struct { const void *m_pReplacement; const void *m_pOriginal; } Interpose_Swap
	__attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&CountedSwap, (const void *)(unsigned long)&SDL_GL_SwapWindow};

static void *Writer(void *pArg)
{
	const char *pPath = getenv("FRAME_COUNT_FILE");
	if(!pPath)
		return NULL;
	for(;;)
	{
		FILE *pFile = fopen(pPath, "w");
		if(pFile)
		{
			fprintf(pFile, "%lu\n", atomic_load(&g_Frames));
			fclose(pFile);
		}
		sleep(1);
	}
	return NULL;
}

__attribute__((constructor)) static void Start(void)
{
	pthread_t Thread;
	pthread_create(&Thread, NULL, Writer, NULL);
	pthread_detach(Thread);
}
