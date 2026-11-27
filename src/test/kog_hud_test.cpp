#include <game/client/kog_style.h>

#include <gtest/gtest.h>

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
}

TEST(KogHud, ScoreboardNeverShrinksTheTextToFit)
{
	for(const SScreen &Screen : SCREENS)
	{
		for(int Players : {1, 8, 16, 24, 32, 48, 64, 96, 128})
		{
			const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players);
			SCOPED_TRACE(std::string(Screen.m_pName) + " with " + std::to_string(Players) + " players");
			// the plan has no size of its own: rows and text come from the tokens
			ASSERT_GE(Plan.m_Columns, 1);
			EXPECT_GE(Plan.m_ColumnWidth, Hud::MIN_COLUMN_WIDTH);
			EXPECT_LE(Plan.m_RowsPerColumn * Hud::ROW + Hud::ROW_TITLE + Hud::ROW, Screen.m_Height);
			EXPECT_LE(Plan.m_Shown, Players);
			EXPECT_EQ(Plan.m_Truncated, Plan.m_Shown < Players);
		}
	}
}

TEST(KogHud, SixtyFourPlayersFitOnEveryCommonScreen)
{
	for(const SScreen &Screen : SCREENS)
	{
		if(Screen.m_Width < 700.0f)
			continue; // narrower than 4:3: fewer columns, the rest is counted, not shrunk
		const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, 64);
		EXPECT_FALSE(Plan.m_Truncated) << Screen.m_pName;
	}
}

TEST(KogHud, A128PlayerServerOnA43ScreenIsCountedNotShrunk)
{
	const Hud::SScoreboardPlan Plan = Hud::PlanScoreboard(800.0f, 600.0f, 128);
	EXPECT_TRUE(Plan.m_Truncated);
	EXPECT_LT(Plan.m_Shown, 128);
	EXPECT_GE(Plan.m_Shown, 64);
	// on 16:9 and wider all 128 fit
	EXPECT_FALSE(Hud::PlanScoreboard(1800.0f, 600.0f, 128).m_Truncated);
}

TEST(KogHud, ThePlanIsMonotonic)
{
	for(const SScreen &Screen : SCREENS)
	{
		int Previous = 0;
		for(int Players = 1; Players <= 128; Players++)
		{
			const int Shown = Hud::PlanScoreboard(Screen.m_Width, Screen.m_Height, Players).m_Shown;
			EXPECT_GE(Shown, Previous) << Screen.m_pName << " " << Players;
			Previous = Shown;
		}
	}
}
