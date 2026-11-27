#ifndef ENGINE_SHARED_KOG_FREE_LIST_H
#define ENGINE_SHARED_KOG_FREE_LIST_H

#include <cstddef>
#include <optional>
#include <vector>

// The graphics code hands out indices of textures, buffers and quad containers from free lists that live in the vectors
// themselves: an entry that is free holds the index of the next free one, -1 ends the list. These functions read such a list
// without changing it, for the report of leaked handles at shutdown.
namespace KogFreeList
{
// The number of free entries, or nothing when the list leaves the vector or loops: both mean an index was freed twice.
// `Next(Index)` is the entry that follows the free entry `Index`.
template<typename TNext>
[[nodiscard]] std::optional<size_t> CountFree(size_t Size, long long First, TNext Next)
{
	size_t Count = 0;
	for(long long Index = First; Index != -1; Index = Next(static_cast<size_t>(Index)))
	{
		if(Index < 0 || static_cast<size_t>(Index) >= Size || Count >= Size)
			return std::nullopt;
		Count++;
	}
	return Count;
}

[[nodiscard]] inline std::optional<size_t> CountFree(const std::vector<int> &vNext, long long First)
{
	return CountFree(vNext.size(), First, [&](size_t Index) { return static_cast<long long>(vNext[Index]); });
}
} // namespace KogFreeList

#endif
