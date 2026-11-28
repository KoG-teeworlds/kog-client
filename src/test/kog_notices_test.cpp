#include <engine/shared/kog_notices.h>

#include <gtest/gtest.h>

using namespace KogNotices;

TEST(KogNotices, TheLinesTheBackendWrites)
{
	const char *pBody = nullptr;
	EXPECT_EQ(Classify("Achievement unlocked: Finisher, silver (+150 coins)", &pBody), EKind::ACHIEVEMENT);
	EXPECT_STREQ(pBody, "Finisher, silver (+150 coins)");
	EXPECT_EQ(Classify("Quest completed: No hammer", &pBody), EKind::QUEST);
	EXPECT_STREQ(pBody, "No hammer");
}

TEST(KogNotices, OtherLinesAreNot)
{
	const char *pBody = nullptr;
	EXPECT_EQ(Classify("", &pBody), EKind::NONE);
	EXPECT_EQ(Classify("achievement unlocked: x", &pBody), EKind::NONE);
	EXPECT_EQ(Classify("Achievement unlocked:", &pBody), EKind::NONE);
	EXPECT_EQ(Classify("Achievement unlocked: ", &pBody), EKind::NONE);
	EXPECT_EQ(Classify("He said Achievement unlocked: x", &pBody), EKind::NONE);
	EXPECT_EQ(Classify("Quest completed:no space", &pBody), EKind::NONE);
}

TEST(KogNotices, AtMostFourToastsInTenSeconds)
{
	CRateLimit Limit;
	for(int i = 0; i < MAX_TOASTS_PER_WINDOW; i++)
		EXPECT_TRUE(Limit.Allow(1.0f + i));
	EXPECT_FALSE(Limit.Allow(5.0f));
	EXPECT_FALSE(Limit.Allow(10.9f));
	EXPECT_TRUE(Limit.Allow(11.5f)); // the first one is more than 10 seconds old
	Limit.Reset();
	EXPECT_TRUE(Limit.Allow(11.6f));
}
