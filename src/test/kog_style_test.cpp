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
