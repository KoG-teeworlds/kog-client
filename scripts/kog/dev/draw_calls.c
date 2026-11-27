// Counts the OpenGL draw calls and texture binds per frame of a process and writes "frames draws binds" to the file named by
// DRAW_CALLS_FILE once a second (macOS interposer; the frame is a call of SDL_GL_SwapWindow).
// cc -dynamiclib -undefined dynamic_lookup -framework OpenGL -o libdraw_calls.dylib draw_calls.c
#include <OpenGL/gl3.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static _Atomic unsigned long g_Frames, g_Draws, g_Binds;

extern void SDL_GL_SwapWindow(void *pWindow);
static void CountedSwap(void *pWindow)
{
	atomic_fetch_add_explicit(&g_Frames, 1, memory_order_relaxed);
	SDL_GL_SwapWindow(pWindow);
}
static void CountedDrawElements(GLenum Mode, GLsizei Count, GLenum Type, const void *pIndices)
{
	atomic_fetch_add_explicit(&g_Draws, 1, memory_order_relaxed);
	glDrawElements(Mode, Count, Type, pIndices);
}
static void CountedDrawArrays(GLenum Mode, GLint First, GLsizei Count)
{
	atomic_fetch_add_explicit(&g_Draws, 1, memory_order_relaxed);
	glDrawArrays(Mode, First, Count);
}
static void CountedDrawElementsInstanced(GLenum Mode, GLsizei Count, GLenum Type, const void *pIndices, GLsizei Instances)
{
	atomic_fetch_add_explicit(&g_Draws, 1, memory_order_relaxed);
	glDrawElementsInstanced(Mode, Count, Type, pIndices, Instances);
}
static void CountedBindTexture(GLenum Target, GLuint Texture)
{
	atomic_fetch_add_explicit(&g_Binds, 1, memory_order_relaxed);
	glBindTexture(Target, Texture);
}

#define INTERPOSE(Replacement, Original) \
	__attribute__((used)) static struct { const void *m_pReplacement; const void *m_pOriginal; } Interpose_##Original \
		__attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&Replacement, (const void *)(unsigned long)&Original}

INTERPOSE(CountedSwap, SDL_GL_SwapWindow);
INTERPOSE(CountedDrawElements, glDrawElements);
INTERPOSE(CountedDrawArrays, glDrawArrays);
INTERPOSE(CountedDrawElementsInstanced, glDrawElementsInstanced);
INTERPOSE(CountedBindTexture, glBindTexture);

static void *Writer(void *pArg)
{
	const char *pPath = getenv("DRAW_CALLS_FILE");
	if(!pPath)
		return NULL;
	for(;;)
	{
		FILE *pFile = fopen(pPath, "w");
		if(pFile)
		{
			fprintf(pFile, "%lu %lu %lu\n", atomic_load(&g_Frames), atomic_load(&g_Draws), atomic_load(&g_Binds));
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
