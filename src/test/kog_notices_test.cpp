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

static KogNotices::SToast Toast(EKind Kind, const char *pKey, const char *pText, int Value = 0, int Limit = 0)
{
	KogNotices::SToast Result;
	Result.m_Kind = Kind;
	Result.m_Key = pKey;
	Result.m_Text = pText;
	Result.m_Value = Value;
	Result.m_Limit = Limit;
	return Result;
}

TEST(KogNotices, ANoticeWithTheKeyOfAShownOneReplacesIt)
{
	CToasts Toasts;
	EXPECT_TRUE(Toasts.Show(Toast(EKind::QUEST_PROGRESS, "quest:team8", "Team of 8", 1, 8), 1.0f));
	EXPECT_TRUE(Toasts.Show(Toast(EKind::QUEST_PROGRESS, "quest:team8", "Team of 8", 2, 8), 1.5f));
	ASSERT_EQ(Toasts.All().size(), 1u);
	EXPECT_EQ(Toasts.All().front().m_Value, 2);
	EXPECT_TRUE(Toasts.All().front().HasProgress());
	EXPECT_TRUE(Toasts.Show(Toast(EKind::ACHIEVEMENT, "ach:1", "x"), 2.0f));
	EXPECT_EQ(Toasts.All().size(), 2u);
}

TEST(KogNotices, ReplacingDoesNotCountAgainstTheRateLimit)
{
	CToasts Toasts;
	for(int i = 0; i < 20; i++)
		EXPECT_TRUE(Toasts.Show(Toast(EKind::MAINTENANCE, "maintenance", "restart", 300 - i), 1.0f + i * 0.1f));
	EXPECT_EQ(Toasts.All().size(), 1u);
	// a noticeless flood stops at the limit, the rest is for the chat log
	CToasts Flood;
	int Shown = 0;
	for(int i = 0; i < 10; i++)
		Shown += Flood.Show(Toast(EKind::INFO, "", "line"), 1.0f + i * 0.1f);
	EXPECT_EQ(Shown, MAX_TOASTS_PER_WINDOW);
}

TEST(KogNotices, ACountdownLastsUntilItEnds)
{
	CToasts Toasts;
	ASSERT_TRUE(Toasts.Show(Toast(EKind::MAINTENANCE, "maintenance", "restart in 5 minutes", 300), 10.0f));
	const SToast &Shown = Toasts.All().front();
	EXPECT_TRUE(Shown.HasCountdown());
	EXPECT_EQ(Shown.SecondsLeft(10.0f), 300);
	EXPECT_EQ(Shown.SecondsLeft(70.0f), 240);
	EXPECT_EQ(Shown.SecondsLeft(400.0f), 0);
	Toasts.Expire(100.0f);
	EXPECT_FALSE(Toasts.Empty());
	Toasts.Expire(311.0f);
	EXPECT_TRUE(Toasts.Empty());
}

TEST(KogNotices, ToastsAreShortAndACountdownIsBounded)
{
	EXPECT_EQ(DurationOf(EKind::ACHIEVEMENT, 0), TOAST_SECONDS);
	EXPECT_EQ(DurationOf(EKind::MAINTENANCE, 0), TOAST_SECONDS) << "no deadline: an ordinary toast";
	EXPECT_EQ(DurationOf(EKind::ACHIEVEMENT, 300), TOAST_SECONDS) << "value means something only for some kinds";
	EXPECT_EQ(DurationOf(EKind::MAINTENANCE, 2000000000), (float)MAX_COUNTDOWN_SECONDS);
	CToasts Toasts;
	ASSERT_TRUE(Toasts.Show(Toast(EKind::INFO, "", "line"), 1.0f));
	Toasts.Expire(5.5f);
	EXPECT_FALSE(Toasts.Empty());
	Toasts.Expire(6.5f);
	EXPECT_TRUE(Toasts.Empty());
}
