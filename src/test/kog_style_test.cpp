#include <game/client/kog_style.h>

#include <gtest/gtest.h>

using namespace KogStyle;

// A bright map behind a translucent panel is the worst case for text on it
static const ColorRGBA WORST_BACKING(1.0f, 1.0f, 1.0f);

TEST(KogStyle, ContrastOfKnownPairs)
{
	EXPECT_NEAR(ContrastRatio(ColorRGBA(0x000000U), ColorRGBA(0xffffffU)), 21.0f, 0.01f);
	EXPECT_NEAR(ContrastRatio(ColorRGBA(0x777777U), ColorRGBA(0xffffffU)), 4.48f, 0.05f);
}

TEST(KogStyle, TextIsReadableOnTheBase)
{
	EXPECT_GE(ContrastRatio(DARK.m_Fg, DARK.m_Bg), 7.0f);
	EXPECT_GE(ContrastRatio(DARK.m_Muted, DARK.m_Bg), 7.0f);
}

TEST(KogStyle, TextIsReadableOnPanelsOverAnyMap)
{
	const ColorRGBA Dark = Over(DARK.m_Panel, WORST_BACKING);
	const ColorRGBA Raised = Over(DARK.m_Panel2, WORST_BACKING);
	EXPECT_GE(ContrastRatio(DARK.m_Fg, Dark), 7.0f);
	EXPECT_GE(ContrastRatio(DARK.m_Muted, Dark), 4.5f);
	EXPECT_GE(ContrastRatio(DARK.m_Fg, Raised), 7.0f);
	EXPECT_GE(ContrastRatio(DARK.m_Muted, Raised), 4.5f);
}

TEST(KogStyle, TextOnBrandIsReadable)
{
	EXPECT_GE(ContrastRatio(DARK.m_OnBrand, DARK.m_Brand), 4.5f);
}

TEST(KogStyle, StatusColoursAreReadableAndAreNotOnlyHues)
{
	const ColorRGBA Dark = Over(DARK.m_Panel, WORST_BACKING);
	for(const ColorRGBA &Status : {DARK.m_Ok, DARK.m_Warn, DARK.m_Err})
		EXPECT_GE(ContrastRatio(Status, Dark), 4.5f);
	// Colour is never the only carrier (a word or icon always goes with it), and the three also differ in lightness
	// so that a viewer who cannot tell red from green still sees a difference
	EXPECT_GT(std::abs(RelativeLuminance(DARK.m_Ok) - RelativeLuminance(DARK.m_Err)), 0.1f);
	EXPECT_GT(std::abs(RelativeLuminance(DARK.m_Warn) - RelativeLuminance(DARK.m_Ok)), 0.1f);
	EXPECT_GT(std::abs(RelativeLuminance(DARK.m_Warn) - RelativeLuminance(DARK.m_Err)), 0.1f);
}

TEST(KogStyle, BrandIsVisibleAsAShapeAgainstThePanel)
{
	// WCAG 1.4.11: a control boundary or focus outline needs 3:1
	EXPECT_GE(ContrastRatio(DARK.m_Brand, Over(DARK.m_Panel, DARK.m_Bg)), 3.0f);
}

TEST(KogStyle, SelectedRowKeepsTextReadable)
{
	const ColorRGBA Row = Over(DARK.m_Brand.WithAlpha(0.45f), Over(DARK.m_Panel, WORST_BACKING));
	EXPECT_GE(ContrastRatio(DARK.m_Fg, Row), 4.5f);
}

TEST(KogStyle, ScalesAreOrderedAndNothingIsBelowTheMinimum)
{
	EXPECT_LT(Space::XS, Space::S);
	EXPECT_LT(Space::S, Space::M);
	EXPECT_LT(Space::M, Space::L);
	EXPECT_LT(Space::L, Space::XL);
	EXPECT_LT(Space::XL, Space::XXL);
	EXPECT_LT(Font::Caption, Font::Body);
	EXPECT_LT(Font::Body, Font::Label);
	EXPECT_LT(Font::Label, Font::Title);
	EXPECT_LT(Font::Title, Font::Hero);
	EXPECT_GE(Font::Caption, 12.0f);
	EXPECT_GE(Size::Row, 28.0f);
	EXPECT_GE(Size::Touch, 40.0f);
	EXPECT_GE(Size::Hero, Size::Touch);
}

TEST(KogStyle, OverBlendsLikeTheGpu)
{
	const ColorRGBA C = Over(ColorRGBA(1.0f, 0.0f, 0.0f, 0.5f), ColorRGBA(0.0f, 0.0f, 1.0f));
	EXPECT_FLOAT_EQ(C.r, 0.5f);
	EXPECT_FLOAT_EQ(C.b, 0.5f);
}

// The light theme: the worst case behind a translucent panel is a dark map
static const ColorRGBA DARK_BACKING(0.0f, 0.0f, 0.0f);

TEST(KogStyle, LightTextIsReadable)
{
	EXPECT_GE(ContrastRatio(LIGHT.m_Fg, LIGHT.m_Bg), 7.0f);
	EXPECT_GE(ContrastRatio(LIGHT.m_Muted, LIGHT.m_Bg), 7.0f);
	const ColorRGBA Panel = Over(LIGHT.m_Panel, DARK_BACKING);
	const ColorRGBA Raised = Over(LIGHT.m_Panel2, DARK_BACKING);
	EXPECT_GE(ContrastRatio(LIGHT.m_Fg, Panel), 7.0f);
	EXPECT_GE(ContrastRatio(LIGHT.m_Muted, Panel), 4.5f);
	EXPECT_GE(ContrastRatio(LIGHT.m_Fg, Raised), 7.0f);
	EXPECT_GE(ContrastRatio(LIGHT.m_Muted, Raised), 4.5f);
	EXPECT_GE(ContrastRatio(LIGHT.m_Fg, Over(LIGHT.m_Backdrop, DARK_BACKING)), 7.0f);
}

TEST(KogStyle, LightBrandAndStatusMarks)
{
	EXPECT_GE(ContrastRatio(LIGHT.m_OnBrand, LIGHT.m_Brand), 4.5f);
	const ColorRGBA Panel = Over(LIGHT.m_Panel, DARK_BACKING);
	// the brand colour and the status marks are shapes and words next to words: 3:1 is the rule for shapes
	EXPECT_GE(ContrastRatio(LIGHT.m_Brand, Panel), 3.0f);
	for(const ColorRGBA &Status : {LIGHT.m_Ok, LIGHT.m_Warn, LIGHT.m_Err})
		EXPECT_GE(ContrastRatio(Status, Panel), 3.0f);
	EXPECT_GT(std::abs(RelativeLuminance(LIGHT.m_Ok) - RelativeLuminance(LIGHT.m_Err)), 0.04f);
	EXPECT_GT(std::abs(RelativeLuminance(LIGHT.m_Warn) - RelativeLuminance(LIGHT.m_Ok)), 0.04f);
	EXPECT_GT(std::abs(RelativeLuminance(LIGHT.m_Warn) - RelativeLuminance(LIGHT.m_Err)), 0.04f);
}

TEST(KogStyle, LightSelectedRowKeepsTextReadable)
{
	const ColorRGBA Row = Over(LIGHT.m_Brand.WithAlpha(0.45f), Over(LIGHT.m_Panel, DARK_BACKING));
	EXPECT_GE(ContrastRatio(LIGHT.m_Fg, Row), 4.5f);
}

TEST(KogStyle, GlassTextIsReadableOverAnyMap)
{
	const ColorRGBA Panel = Over(GLASS.m_Panel, WORST_BACKING);
	const ColorRGBA Raised = Over(GLASS.m_Panel2, WORST_BACKING);
	EXPECT_GE(ContrastRatio(GLASS.m_Fg, Panel), 7.0f);
	EXPECT_GE(ContrastRatio(GLASS.m_Muted, Panel), 4.5f);
	EXPECT_GE(ContrastRatio(GLASS.m_Fg, Raised), 7.0f);
	EXPECT_GE(ContrastRatio(GLASS.m_Muted, Raised), 4.5f);
	EXPECT_GE(ContrastRatio(GLASS.m_OnBrand, GLASS.m_Brand), 4.5f);
	EXPECT_GE(ContrastRatio(GLASS.m_Brand, Over(GLASS.m_Panel, GLASS.m_Bg)), 3.0f);
	for(const ColorRGBA &Status : {GLASS.m_Ok, GLASS.m_Warn, GLASS.m_Err})
		EXPECT_GE(ContrastRatio(Status, Panel), 4.5f);
}

TEST(KogStyle, GlassStatusDiffersInLightness)
{
	EXPECT_GT(std::abs(RelativeLuminance(GLASS.m_Ok) - RelativeLuminance(GLASS.m_Err)), 0.1f);
	EXPECT_GT(std::abs(RelativeLuminance(GLASS.m_Warn) - RelativeLuminance(GLASS.m_Ok)), 0.04f);
}

TEST(KogStyle, GlassSelfRowAndTopOfThePanelKeepTextReadable)
{
	const ColorRGBA Row = Over(GLASS.m_Brand.WithAlpha(Hud::SELF_ALPHA), Over(GLASS.m_Panel, WORST_BACKING));
	EXPECT_GE(ContrastRatio(GLASS.m_Fg, Row), 4.5f);
	// the lighter top of a glass panel is the worst place for the text on it
	ColorRGBA Lighter = GLASS.m_Panel;
	Lighter.r += (1.0f - Lighter.r) * Elevation::GLASS_TOP_LIGHTEN;
	Lighter.g += (1.0f - Lighter.g) * Elevation::GLASS_TOP_LIGHTEN;
	Lighter.b += (1.0f - Lighter.b) * Elevation::GLASS_TOP_LIGHTEN;
	EXPECT_GE(ContrastRatio(GLASS.m_Fg, Over(Lighter, WORST_BACKING)), 7.0f);
}

TEST(KogStyle, EveryThemeSettingIsValid)
{
	EXPECT_EQ(ThemeFromSetting(3), ETheme::GLASS);
	EXPECT_EQ(ThemeFromSetting(4), ETheme::CLASSIC);
	EXPECT_EQ(ThemeFromSetting(-1), ETheme::CLASSIC);
}

TEST(KogStyle, EasingIsMonotonicBoundedAndJumpsWithReducedMotion)
{
	float Previous = 0.0f;
	for(int i = 0; i <= 100; ++i)
	{
		const float V = EaseOutCubic(i / 100.0f);
		EXPECT_GE(V, Previous);
		EXPECT_LE(V, 1.0f);
		Previous = V;
	}
	EXPECT_FLOAT_EQ(EaseOutCubic(-3.0f), 0.0f);
	EXPECT_FLOAT_EQ(EaseOutCubic(7.0f), 1.0f);
	EXPECT_FLOAT_EQ(EaseOutCubic(std::nanf("")), 1.0f);
	EXPECT_FLOAT_EQ(Progress(0.0f, Motion::Toast, true), 1.0f);
	EXPECT_FLOAT_EQ(Progress(0.0f, 0.0f, false), 1.0f);
	EXPECT_LT(Progress(0.01f, Motion::Toast, false), 1.0f);
	EXPECT_LE(Motion::Toast, 0.2f);
	EXPECT_GE(Motion::Hover, 0.1f);
}

TEST(KogStyle, ElevationLevelsGrow)
{
	EXPECT_LT(Elevation::SMALL.m_Blur, Elevation::RAISED.m_Blur);
	EXPECT_LT(Elevation::RAISED.m_Blur, Elevation::FLOATING.m_Blur);
	EXPECT_EQ(Elevation::FLAT.m_ShadowAlpha, 0.0f);
}

TEST(KogStyle, UpstreamColoursKeepTheirMeaning)
{
	EXPECT_EQ(Classify(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)), ESemantic::NEUTRAL);
	EXPECT_EQ(Classify(ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f)), ESemantic::NEUTRAL);
	EXPECT_EQ(Classify(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f)), ESemantic::NEUTRAL);
	EXPECT_EQ(Classify(ColorRGBA(0.5f, 1.0f, 0.5f, 0.5f)), ESemantic::GOOD); // connect button
	EXPECT_EQ(Classify(ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f)), ESemantic::GOOD);
	EXPECT_EQ(Classify(ColorRGBA(0.9f, 0.2f, 0.2f, 0.85f)), ESemantic::BAD);
	EXPECT_EQ(Classify(ColorRGBA(1.0f, 0.98f, 0.5f, 0.55f)), ESemantic::WARN); // sorted header
	EXPECT_EQ(Classify(ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f)), ESemantic::OTHER);
}

TEST(KogStyle, ClassifyNeverFailsOnOddValues)
{
	const float Nan = std::nanf("");
	for(const float V : {0.0f, 1.0f, -1.0f, 7.0f, Nan})
		for(const float W : {0.0f, 0.5f, 1.0f, Nan})
			(void)Classify(ColorRGBA(V, W, V, 1.0f));
}
