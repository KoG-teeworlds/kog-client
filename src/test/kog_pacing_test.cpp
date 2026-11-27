#include <engine/shared/kog_pacing.h>

#include <gtest/gtest.h>

using namespace KogPacing;

namespace
{
constexpr int64_t FREQ = 1000000000;
constexpr int64_t MS = FREQ / 1000;
}

TEST(KogPacing, FirstFrameIsDue)
{
	const CPacing Pacing;
	EXPECT_TRUE(Pacing.RenderDue(0, EWindow::FOCUSED));
}

TEST(KogPacing, NextFrameFollowsTheRate)
{
	CPacing Pacing;
	Pacing.Rendered(0, FREQ, EWindow::FOCUSED, 0);
	EXPECT_FALSE(Pacing.RenderDue(10 * MS, EWindow::FOCUSED));
	EXPECT_EQ(Pacing.WakeTime(10 * MS, FREQ, EWindow::FOCUSED), FREQ / RATE_FOCUSED);
	EXPECT_TRUE(Pacing.RenderDue(FREQ / RATE_FOCUSED, EWindow::FOCUSED));
}

TEST(KogPacing, StaysOnTheGridWhenWakingLate)
{
	CPacing Pacing;
	Pacing.Rendered(0, FREQ, EWindow::FOCUSED, 0);
	const int64_t Interval = FREQ / RATE_FOCUSED;
	Pacing.Rendered(Interval + 3 * MS, FREQ, EWindow::FOCUSED, 0);
	EXPECT_EQ(Pacing.WakeTime(Interval + 4 * MS, FREQ, EWindow::FOCUSED), 2 * Interval);
}

TEST(KogPacing, AMissedIntervalDoesNotCauseABurst)
{
	CPacing Pacing;
	Pacing.Rendered(0, FREQ, EWindow::FOCUSED, 0);
	Pacing.Rendered(5 * FREQ, FREQ, EWindow::FOCUSED, 0);
	Pacing.Rendered(5 * FREQ + MS, FREQ, EWindow::FOCUSED, 0);
	EXPECT_FALSE(Pacing.RenderDue(5 * FREQ + 2 * MS, EWindow::FOCUSED));
}

TEST(KogPacing, UnfocusedIsSlower)
{
	CPacing Pacing;
	Pacing.Rendered(0, FREQ, EWindow::UNFOCUSED, 0);
	EXPECT_EQ(Pacing.WakeTime(MS, FREQ, EWindow::UNFOCUSED), FREQ / RATE_UNFOCUSED);
}

TEST(KogPacing, ThePlayersRateCanOnlyLower)
{
	CPacing Low;
	Low.Rendered(0, FREQ, EWindow::FOCUSED, 20);
	EXPECT_EQ(Low.WakeTime(MS, FREQ, EWindow::FOCUSED), FREQ / 20);

	CPacing High;
	High.Rendered(0, FREQ, EWindow::FOCUSED, 240);
	EXPECT_EQ(High.WakeTime(MS, FREQ, EWindow::FOCUSED), FREQ / RATE_FOCUSED);
}

TEST(KogPacing, HiddenNeverRendersAndWakesForHousekeeping)
{
	const CPacing Pacing;
	EXPECT_FALSE(Pacing.RenderDue(100 * FREQ, EWindow::HIDDEN));
	EXPECT_EQ(Pacing.WakeTime(7 * FREQ, FREQ, EWindow::HIDDEN), 7 * FREQ + HIDDEN_WAKE_MS * MS);
}

TEST(KogPacing, ShowingTheWindowRendersAtOnce)
{
	CPacing Pacing;
	Pacing.Rendered(0, FREQ, EWindow::FOCUSED, 0);
	EXPECT_FALSE(Pacing.RenderDue(MS, EWindow::FOCUSED));
	EXPECT_TRUE(Pacing.RenderDue(MS, EWindow::UNFOCUSED));
}

TEST(KogPacing, RequestRedrawRendersAtOnceOnce)
{
	CPacing Pacing;
	Pacing.Rendered(0, FREQ, EWindow::FOCUSED, 0);
	EXPECT_FALSE(Pacing.RenderDue(MS, EWindow::FOCUSED));
	Pacing.RequestRedraw();
	EXPECT_TRUE(Pacing.RenderDue(MS, EWindow::FOCUSED));
	EXPECT_EQ(Pacing.WakeTime(MS, FREQ, EWindow::FOCUSED), MS);
	Pacing.Rendered(MS, FREQ, EWindow::FOCUSED, 0);
	EXPECT_FALSE(Pacing.RenderDue(2 * MS, EWindow::FOCUSED));
}
