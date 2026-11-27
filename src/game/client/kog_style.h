#ifndef GAME_CLIENT_KOG_STYLE_H
#define GAME_CLIENT_KOG_STYLE_H

#include <base/color.h>

#include <algorithm>
#include <cmath>

// The one place for the look of the KoG client UI, see docs/CLIENT-STYLE.md.
// Units are UI units (the virtual screen is always 600 units high). No configuration is read here, so the
// numbers can be tested; the theme switch is in kog_theme.h.
namespace KogStyle
{
enum class ETheme
{
	CLASSIC = 0, // the look of upstream, nothing of this file is applied
	DARK = 1,
	LIGHT = 2,
	NUM_THEMES
};

struct SPalette
{
	ColorRGBA m_Bg; // base, the darkest surface
	ColorRGBA m_Backdrop; // the menu background behind the panels
	ColorRGBA m_Panel; // cards, bars
	ColorRGBA m_Panel2; // raised cards, slots
	ColorRGBA m_Line; // 1 unit borders and separators
	ColorRGBA m_Fg; // text
	ColorRGBA m_Muted; // secondary text
	ColorRGBA m_Brand; // primary action, focus, active item
	ColorRGBA m_BrandSoft; // icons, small accents
	ColorRGBA m_OnBrand; // text on m_Brand
	ColorRGBA m_Ok;
	ColorRGBA m_Warn;
	ColorRGBA m_Err;
};

inline constexpr SPalette DARK = {
	ColorRGBA(0x0d0d10U),
	ColorRGBA(0x23232bU),
	ColorRGBA(0x0d0d10U).WithAlpha(0.80f),
	ColorRGBA(0x1b1b21U).WithAlpha(0.88f),
	ColorRGBA(0xffffffU).WithAlpha(0.10f),
	ColorRGBA(0xf1f0ecU),
	ColorRGBA(0xb4b4beU),
	ColorRGBA(0xeb5c2bU),
	ColorRGBA(0xed784fU),
	ColorRGBA(0x0d0d10U),
	ColorRGBA(0x8ff0b8U),
	ColorRGBA(0xf2b84bU),
	ColorRGBA(0xff8a8aU)};

inline constexpr SPalette LIGHT = {
	ColorRGBA(0xf6f5f1U),
	ColorRGBA(0xdcdad3U),
	ColorRGBA(0xf6f5f1U).WithAlpha(0.88f),
	ColorRGBA(0xffffffU).WithAlpha(0.92f),
	ColorRGBA(0x000000U).WithAlpha(0.12f),
	ColorRGBA(0x17171bU),
	ColorRGBA(0x4a4a55U),
	ColorRGBA(0xc2410cU),
	ColorRGBA(0xc2410cU),
	ColorRGBA(0xffffffU),
	ColorRGBA(0x166534U),
	ColorRGBA(0xa16207U),
	ColorRGBA(0x7f1d1dU)};

namespace Space
{
inline constexpr float XS = 4.0f;
inline constexpr float S = 8.0f;
inline constexpr float M = 12.0f;
inline constexpr float L = 16.0f;
inline constexpr float XL = 24.0f;
inline constexpr float XXL = 40.0f;
}

namespace Radius
{
inline constexpr float Control = 6.0f; // buttons, inputs, list rows
inline constexpr float Card = 10.0f; // cards, popups, the hero button
}

namespace Font
{
inline constexpr float Caption = 12.0f; // the smallest allowed size
inline constexpr float Body = 14.0f;
inline constexpr float Label = 16.0f;
inline constexpr float Title = 22.0f;
inline constexpr float Hero = 34.0f;
}

namespace Size
{
inline constexpr float Row = 28.0f; // list rows, inputs
inline constexpr float Button = 36.0f;
inline constexpr float Hero = 48.0f; // the Play button
inline constexpr float Touch = 40.0f; // smallest touch target
inline constexpr float Focus = 2.0f; // width of the focus outline
}

namespace Motion
{
inline constexpr float Hover = 0.10f; // seconds, hover and focus colour changes
inline constexpr float Panel = 0.18f; // seconds, panels and popups appearing
}

// Layers over a panel for hover and press, so a state change is a change of alpha and never of hue.
namespace State
{
inline constexpr float HoverOverlay = 0.08f;
inline constexpr float PressOverlay = 0.16f;
inline constexpr float DisabledAlpha = 0.45f;
}

// The HUD (scoreboard, chat, kill feed, notices, tooltips): design C, compact and tabular. Every size of text and every row
// height of the HUD is named here and listed in TEXT_SIZES and ROW_HEIGHTS, and kog_hud_test.cpp fails on anything below the
// minimum, so a later change cannot bring back 11 point text. Units are UI units (600 high); the kill feed draws in a space
// three times larger, KILL_FEED_SCALE converts.
namespace Hud
{
inline constexpr float MIN_TEXT = Font::Caption; // 12, the smallest readable text of the client
inline constexpr float MIN_ROW = 18.0f; // the smallest row that holds 12 unit text with room above and below

inline constexpr float TEXT = MIN_TEXT; // rows of the scoreboard, chat lines, kill feed
inline constexpr float TEXT_HEADER = MIN_TEXT; // column headers and the title strip
inline constexpr float TEXT_TOOLTIP = Font::Body;
inline constexpr float ROW = MIN_ROW;
inline constexpr float ROW_TITLE = MIN_ROW;
inline constexpr float ROW_KILL_FEED = MIN_ROW;
inline constexpr float ROW_CHAT = MIN_ROW;

inline constexpr float RADIUS = 2.0f;
inline constexpr float PAD = Space::S - 2.0f; // 6
inline constexpr float PANEL_ALPHA = 0.92f;
inline constexpr float ZEBRA_ALPHA = 0.06f;
inline constexpr float SELF_ALPHA = 0.35f;
inline constexpr float MIN_COLUMN_WIDTH = 250.0f; // score, name, clan, ping at the text size above
inline constexpr float KILL_FEED_SCALE = 3.0f;

struct SToken
{
	const char *m_pName;
	float m_Value;
};

inline constexpr SToken TEXT_SIZES[] = {{"TEXT", TEXT}, {"TEXT_HEADER", TEXT_HEADER}, {"TEXT_TOOLTIP", TEXT_TOOLTIP}};
inline constexpr SToken ROW_HEIGHTS[] = {{"ROW", ROW}, {"ROW_TITLE", ROW_TITLE}, {"ROW_KILL_FEED", ROW_KILL_FEED}, {"ROW_CHAT", ROW_CHAT}};

// How many columns of rows the scoreboard needs on a screen of this size: at the fixed text size and row height, never smaller.
// What does not fit is not drawn small; Truncated says so and the scoreboard shows how many players are not shown.
struct SScoreboardPlan
{
	int m_Columns;
	int m_RowsPerColumn;
	int m_Shown;
	bool m_Truncated;
	float m_ColumnWidth;
};

inline SScoreboardPlan PlanScoreboard(float Width, float Height, int NumPlayers)
{
	const float Usable = Height - 2.0f * Space::XXL - ROW_TITLE - ROW; // title, headline, margins above and below
	const int MaxRows = std::max(1, (int)(Usable / ROW));
	const int MaxColumns = std::max(1, (int)((Width - 2.0f * Space::L) / MIN_COLUMN_WIDTH));
	SScoreboardPlan Plan;
	Plan.m_Columns = std::clamp((NumPlayers + MaxRows - 1) / MaxRows, 1, MaxColumns);
	Plan.m_Shown = std::min(NumPlayers, Plan.m_Columns * MaxRows);
	Plan.m_RowsPerColumn = std::min(MaxRows, std::max(1, (Plan.m_Shown + Plan.m_Columns - 1) / Plan.m_Columns));
	Plan.m_Truncated = Plan.m_Shown < NumPlayers;
	Plan.m_ColumnWidth = (Width - 2.0f * Space::L) / Plan.m_Columns;
	return Plan;
}
}

// WCAG 2.x, sRGB channels in [0, 1]
inline float ChannelLuminance(float C)
{
	return C <= 0.03928f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
}

inline float RelativeLuminance(const ColorRGBA &C)
{
	return 0.2126f * ChannelLuminance(C.r) + 0.7152f * ChannelLuminance(C.g) + 0.0722f * ChannelLuminance(C.b);
}

inline float ContrastRatio(const ColorRGBA &A, const ColorRGBA &B)
{
	const float La = RelativeLuminance(A);
	const float Lb = RelativeLuminance(B);
	return (std::max(La, Lb) + 0.05f) / (std::min(La, Lb) + 0.05f);
}

// The colour of a (possibly translucent) layer over an opaque background
constexpr ColorRGBA Over(const ColorRGBA &Layer, const ColorRGBA &Background)
{
	return ColorRGBA(
		Layer.r * Layer.a + Background.r * (1.0f - Layer.a),
		Layer.g * Layer.a + Background.g * (1.0f - Layer.a),
		Layer.b * Layer.a + Background.b * (1.0f - Layer.a),
		1.0f);
}
}

#endif
