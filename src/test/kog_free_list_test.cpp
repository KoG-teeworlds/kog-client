#include <engine/shared/kog_free_list.h>

#include <gtest/gtest.h>

#include <vector>

TEST(KogFreeList, EmptyListHasNoFreeEntries)
{
	const std::vector<int> vNext = {-1, -1, -1};
	EXPECT_EQ(KogFreeList::CountFree(vNext, -1), 0u);
}

TEST(KogFreeList, CountsTheChain)
{
	// 2 -> 0 -> end, 1 is in use
	const std::vector<int> vNext = {-1, -1, 0};
	EXPECT_EQ(KogFreeList::CountFree(vNext, 2), 2u);
}

TEST(KogFreeList, ALoopMeansADoubleFree)
{
	const std::vector<int> vNext = {1, 0};
	EXPECT_FALSE(KogFreeList::CountFree(vNext, 0).has_value());
}

TEST(KogFreeList, AnIndexOutsideTheVectorIsReported)
{
	const std::vector<int> vNext = {7};
	EXPECT_FALSE(KogFreeList::CountFree(vNext, 0).has_value());
	EXPECT_FALSE(KogFreeList::CountFree(vNext, 5).has_value());
}
