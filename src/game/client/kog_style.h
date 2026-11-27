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
