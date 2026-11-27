#ifndef GAME_CLIENT_KOG_THEME_H
#define GAME_CLIENT_KOG_THEME_H

#include "kog_style.h"

#include <engine/shared/config.h>
#include <engine/textrender.h>

// What the shared UI code asks: is the KoG look on, and which colours does it use. Classic returns exactly the
// values the code used before, so a file that asks stays identical to upstream when the theme is Classic.
namespace KogTheme
{
inline KogStyle::ETheme Current()
{
	switch(g_Config.m_ClKogTheme)
	{
	case (int)KogStyle::ETheme::DARK:
		return KogStyle::ETheme::DARK;
	case (int)KogStyle::ETheme::LIGHT:
		return KogStyle::ETheme::LIGHT;
	}
	return KogStyle::ETheme::CLASSIC;
}

inline bool On() { return Current() != KogStyle::ETheme::CLASSIC; }
inline bool IsLight() { return Current() == KogStyle::ETheme::LIGHT; }

inline const KogStyle::SPalette &Palette() { return IsLight() ? KogStyle::LIGHT : KogStyle::DARK; }

// The neutral layer of the look: white over dark panels, black over light ones
inline ColorRGBA Layer(float Alpha) { return IsLight() ? ColorRGBA(0.0f, 0.0f, 0.0f, Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, Alpha); }

// A neutral layer in a place that was white in the classic look (rails, hover, headers)
inline ColorRGBA Neutral(float Alpha) { return On() ? Layer(Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, Alpha); }

// Colours of text in the menus. Classic and dark keep the engine default (white with a dark outline).
inline ColorRGBA TextColor() { return IsLight() ? KogStyle::LIGHT.m_Fg : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f); }
inline ColorRGBA TextOutlineColor() { return IsLight() ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f); }

// Fill of a selected or filled control: neutral white in Classic, the brand colour in the KoG look
inline ColorRGBA Fill(float ClassicAlpha, float BrandAlpha)
{
	return On() ? Palette().m_Brand.WithAlpha(BrandAlpha) : ColorRGBA(1.0f, 1.0f, 1.0f, ClassicAlpha);
}

// A button, input or box fill. Classic: unchanged. KoG: the neutral white fills become quiet layers (a state is a
// change of alpha), colours that carry a meaning (green, red) are kept.
inline ColorRGBA Control(const ColorRGBA &Classic)
{
	const bool Neutral = Classic.r == 1.0f && Classic.g == 1.0f && Classic.b == 1.0f;
	if(!On() || !Neutral)
		return Classic;
	return IsLight() ? Layer(Classic.a * 0.22f) : Classic.WithMultipliedAlpha(0.22f);
}

// Handle of a scrollbar or slider: the legacy grey 0.8 to 1.0 becomes the muted text colour so it stays visible
inline ColorRGBA Handle(const ColorRGBA &Classic)
{
	return On() ? Palette().m_Muted.WithAlpha(Classic.r * 0.85f) : Classic;
}

// Box of a check box
inline ColorRGBA CheckBox(bool Checked, float HoverMul)
{
	if(!On())
		return ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * HoverMul);
	return Checked ? Palette().m_Brand.WithAlpha(std::min(1.0f, 0.55f * HoverMul + 0.2f)) : Layer(0.12f * HoverMul);
}
// While it lives, the menus draw their text with the colours of the theme; the destructor puts the defaults back
// so the game view, the HUD and the console keep their white text.
class CMenuTextScope
{
	ITextRender *m_pTextRender;

	void Set(ColorRGBA Color, ColorRGBA Outline)
	{
		m_pTextRender->SetDefaultTextColors(Color, Outline);
		m_pTextRender->TextColor(Color);
		m_pTextRender->TextOutlineColor(Outline);
	}

public:
	explicit CMenuTextScope(ITextRender *pTextRender) :
		m_pTextRender(pTextRender)
	{
		Set(TextColor(), TextOutlineColor());
	}
	~CMenuTextScope() { Set(ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 0.3f)); }
};
}

#endif
