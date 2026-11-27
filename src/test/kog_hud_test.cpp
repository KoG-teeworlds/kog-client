#include <game/client/kog_style.h>

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <string>

using namespace KogStyle;

TEST(KogHud, NoTokenisedTextIsBelowTheMinimum)
{
	for(const Hud::SToken &Token : Hud::TEXT_SIZES)
		EXPECT_GE(Token.m_Value, Hud::MIN_TEXT) << Token.m_pName;
	EXPECT_GE(Hud::MIN_TEXT, 12.0f);
	// the minimum is the smallest size of the whole client style, not a HUD value of its own
	EXPECT_EQ(Hud::MIN_TEXT, Font::Caption);
}

TEST(KogHud, NoTokenisedRowIsBelowTheMinimum)
{
	for(const Hud::SToken &Token : Hud::ROW_HEIGHTS)
		EXPECT_GE(Token.m_Value, Hud::MIN_ROW) << Token.m_pName;
	// a row holds its text with room: at least 1.5 times the text size
	EXPECT_GE(Hud::MIN_ROW, 1.5f * Hud::MIN_TEXT);
	// the kill feed draws three times larger, the converted values must hold the same minimum
	EXPECT_GE(Hud::ROW_KILL_FEED * Hud::KILL_FEED_SCALE, 3.0f * Hud::MIN_ROW);
	EXPECT_GE(Hud::TEXT * Hud::KILL_FEED_SCALE, 3.0f * Hud::MIN_TEXT);
}

TEST(KogHud, ASettingThatIsNotATheme)
{
	EXPECT_EQ(ThemeFromSetting(0), ETheme::CLASSIC);
	EXPECT_EQ(ThemeFromSetting(1), ETheme::DARK);
	EXPECT_EQ(ThemeFromSetting(2), ETheme::LIGHT);
	EXPECT_EQ(ThemeFromSetting(3), ETheme::GLASS);
	for(int Value : {-1, 4, 100, -2147483647 - 1, 2147483647})
		EXPECT_EQ(ThemeFromSetting(Value), ETheme::CLASSIC) << Value;
}

namespace
{
	struct SScreen
	{
		const char *m_pName;
		float m_Width; // UI units: 600 high, so the width is 600 times the aspect ratio
		float m_Height;
	};
	const SScreen SCREENS[] = {
		{"4:3", 800.0f, 600.0f},
		{"16:9", 1066.7f, 600.0f},
		{"16:10", 960.0f, 600.0f},
		{"ultrawide", 1400.0f, 600.0f},
		{"narrow window", 600.0f, 600.0f},
		{"tall window", 450.0f, 600.0f},
	};

	// The invariants of every plan, for any input at all
	void ExpectSafe(const Hud::SScoreboardPlan &Plan, float Width, float Height, int Players, const std::string &Where)
	{
		SCOPED_TRACE(Where);
		for(float Value : {Plan.m_ColumnWidth, Plan.m_X, Plan.m_Y, Plan.m_Width, Plan.m_Height, Plan.m_ChatReserve})
			ASSERT_TRUE(std::isfinite(Value));
		ASSERT_GE(Plan.m_Columns, 1);
		ASSERT_GE(Plan.m_RowsPerColumn, 1);
		ASSERT_GE(Plan.m_Shown, 0);
		ASSERT_LE(Plan.m_Shown, std::max(Players, 0));
		ASSERT_EQ(Plan.m_Truncated, Plan.m_Shown < std::max(Players, 0));
		ASSERT_GT(Plan.m_Width, 0.0f);
		ASSERT_GT(Plan.m_Height, 0.0f);
		// inside the screen (a screen of one unit gets a plan that is one unit off at most)
		const float W = std::max(Width, 1.0f);
		const float H = std::max(Height, 1.0f);
		ASSERT_GE(Plan.m_X, 0.0f);
		ASSERT_GE(Plan.m_Y, 0.0f);
		ASSERT_LE(Plan.m_X + Plan.m_Width, W + 1.0f);
		ASSERT_LE(Plan.m_Y + Plan.m_Height, H + 0.01f);
		ASSERT_LE(Plan.m_Columns, 100000);
	}
}

TEST(KogHud, ScoreboardNeverShrinksTheTextToFit)
{
	for(const SScreen &Screen : SCREENS)
	{
		for(int Players : {1, 8, 16, 24, 32, 48, 64, 96, 128})
		{
			const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players);
			const std::string Where = std::string(Screen.m_pName) + " with " + std::to_string(Players) + " players";
			ExpectSafe(Plan, Screen.m_Width, Screen.m_Height, Players, Where);
			SCOPED_TRACE(Where);
			// the plan has no size of its own: rows and text come from the tokens
			EXPECT_GE(Plan.m_ColumnWidth, Hud::MIN_COLUMN_WIDTH);
			EXPECT_LE(Plan.m_RowsPerColumn * Hud::ROW + Hud::TITLE_BAR + 2.0f * Hud::TEXT_HEADER + 10.0f, Plan.m_Height + 0.01f);
		}
	}
}

TEST(KogHud, SixtyFourPlayersFitOnEveryCommonScreen)
{
	for(const SScreen &Screen : SCREENS)
	{
		if(Screen.m_Width < 700.0f)
			continue; // narrower than 4:3: fewer columns, the rest is counted, not shrunk
		EXPECT_FALSE(Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, 64).m_Truncated) << Screen.m_pName;
	}
}

TEST(KogHud, A128PlayerServerOnA43ScreenIsCountedNotShrunk)
{
	const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(800.0f, 600.0f, 128);
	EXPECT_TRUE(Plan.m_Truncated);
	EXPECT_GE(Plan.m_Shown, 64);
	// on 16:9 and wider the chat strip stays free and 128 still does not all fit; a wider screen shows them all
	EXPECT_FALSE(Hud::PlanScoreboard(2400.0f, 600.0f, 128).m_Truncated);
}

TEST(KogHud, TheChatStripIsFreeOnAWideScreenAndTheChatWaitsOnOthers)
{
	for(const SScreen &Screen : SCREENS)
	{
		for(int Players : {0, 1, 16, 64, 128, 512})
		{
			const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players);
			SCOPED_TRACE(std::string(Screen.m_pName) + " " + std::to_string(Players));
			if(Screen.m_Width / Screen.m_Height > Hud::WIDE_ASPECT)
			{
				EXPECT_FALSE(Plan.m_ChatHidden);
				EXPECT_GE(Plan.m_X, Hud::CHAT_RESERVE);
			}
			else
			{
				EXPECT_TRUE(Plan.m_ChatHidden);
			}
		}
	}
}

TEST(KogHud, TheSpectatorsPanelStaysOnTheScreenOrIsLeftOut)
{
	for(const SScreen &Screen : SCREENS)
	{
		for(int Players : {0, 8, 16, 32, 64, 128})
		{
			const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players);
			const float Height = Hud::PlanSpectatorsHeight(Screen.m_Height, Plan.m_Y + Plan.m_Height);
			EXPECT_GE(Height, 0.0f);
			EXPECT_LE(Height, 100.0f);
			if(Height > 0.0f)
			{
				EXPECT_GE(Height, Hud::SPECTATORS_MIN);
				EXPECT_LE(Plan.m_Y + Plan.m_Height + 5.0f + Height, Screen.m_Height);
			}
		}
	}
}

TEST(KogHud, TheKillFeedKeepsAboveTheScoreboard)
{
	// the scoreboard starts at SCOREBOARD_TOP when it fits, and four rows fit above it
	EXPECT_GE(Hud::PlanKillFeedRows(true, Hud::SCOREBOARD_TOP, 10), 4);
	EXPECT_EQ(Hud::PlanKillFeedRows(false, 0.0f, 10), 10);
	for(float Top : {-100.0f, 0.0f, 5.0f, 20.0f, 50.0f, 100.0f, 400.0f, 1.0e9f})
	{
		const int Rows = Hud::PlanKillFeedRows(true, Top, 10);
		EXPECT_GE(Rows, 0);
		EXPECT_LE(Rows, 10);
		EXPECT_LE(Hud::SCREEN_MARGIN + Rows * Hud::ROW_KILL_FEED, std::max(Top, Hud::SCREEN_MARGIN + 0.0f) + 0.01f);
	}
	EXPECT_EQ(Hud::PlanKillFeedRows(true, 100.0f, -5), 0);
	// values that are not rows of anything
	EXPECT_EQ(Hud::PlanKillFeedRows(true, std::nanf(""), 10), 0);
	EXPECT_EQ(Hud::PlanKillFeedRows(true, -INFINITY, 10), 0);
	EXPECT_EQ(Hud::PlanKillFeedRows(true, INFINITY, 10), 10);
	EXPECT_EQ(Hud::PlanKillFeedRows(true, 3.4e38f, 10), 10);
}

TEST(KogHud, ThePlanIsMonotonicAndDeterministic)
{
	for(const SScreen &Screen : SCREENS)
	{
		int Previous = 0;
		for(int Players = 0; Players <= 512; Players++)
		{
			const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players);
			const Hud::SScoreboardPlan Again = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players);
			EXPECT_EQ(Plan.m_Shown, Again.m_Shown);
			EXPECT_EQ(Plan.m_X, Again.m_X);
			EXPECT_GE(Plan.m_Shown, Previous) << Screen.m_pName << " " << Players;
			Previous = Plan.m_Shown;
		}
	}
}

// Property test: random screens (from a few units to 8K, every aspect ratio, UI scale 50 to 200 percent), random player counts
TEST(KogHud, RandomScreensAndPlayerCountsKeepEveryInvariant)
{
	std::mt19937 Random(20261127);
	std::uniform_real_distribution<float> Aspect(0.2f, 8.0f);
	std::uniform_real_distribution<float> Scale(0.5f, 2.0f);
	std::uniform_int_distribution<int> Pixels(1, 4320);
	std::uniform_int_distribution<int> Players(0, 512);
	for(int Iteration = 0; Iteration < 20000; Iteration++)
	{
		// the UI is always 600 units high at scale 1; a UI scale divides that
		const float Height = Iteration % 2 == 0 ? 600.0f / Scale(Random) : (float)Pixels(Random);
		const float Width = Height * Aspect(Random);
		const int N = Iteration % 50 == 0 ? (Iteration % 100 == 0 ? 0 : 512) : Players(Random);
		const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Width, Height, N);
		const std::string Where = "iteration " + std::to_string(Iteration) + ": " + std::to_string(Width) + "x" + std::to_string(Height) + ", " + std::to_string(N) + " players";
		ExpectSafe(Plan, Width, Height, N, Where);
		if(::testing::Test::HasFatalFailure())
			return;
		if(Height >= 200.0f && Width >= 480.0f)
		{
			// where the minimum sizes can be kept, they are kept
			SCOPED_TRACE(Where);
			ASSERT_GE(Plan.m_ColumnWidth, Hud::MIN_COLUMN_WIDTH);
			ASSERT_LE(Plan.m_RowsPerColumn * Hud::ROW + Hud::TITLE_BAR + 2.0f * Hud::TEXT_HEADER + 10.0f, Plan.m_Height + 0.01f);
			if(Width / Height > Hud::WIDE_ASPECT)
				ASSERT_GE(Plan.m_X, Hud::CHAT_RESERVE);
			const float Spectators = Hud::PlanSpectatorsHeight(Height, Plan.m_Y + Plan.m_Height);
			ASSERT_GE(Spectators, 0.0f);
			ASSERT_LE(Plan.m_Y + Plan.m_Height + (Spectators > 0.0f ? 5.0f + Spectators : 0.0f), Height);
		}
	}
}

TEST(KogHud, InputsThatAreNotScreensGiveAValidPlan)
{
	const float Nan = std::nanf("");
	const float Inf = INFINITY;
	for(float Width : {0.0f, -5.0f, Nan, Inf, 1.0f, 1.0e9f})
	{
		for(float Height : {0.0f, -5.0f, Nan, Inf, 1.0f, 1.0e9f})
		{
			for(int N : {-1, 0, 1, 128, 2147483647, -2147483647 - 1})
			{
				const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Width, Height, N);
				for(float Value : {Plan.m_ColumnWidth, Plan.m_X, Plan.m_Y, Plan.m_Width, Plan.m_Height})
					ASSERT_TRUE(std::isfinite(Value)) << Width << " " << Height << " " << N;
				ASSERT_GE(Plan.m_Columns, 1);
				ASSERT_GE(Plan.m_Shown, 0);
			}
		}
	}
}
