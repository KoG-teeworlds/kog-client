#ifndef GAME_CLIENT_KOG_THEME_H
#define GAME_CLIENT_KOG_THEME_H

#include "kog_style.h"

#include <engine/shared/config.h>

// What the shared UI code asks: is the KoG look on, and which colours does it use. Classic returns exactly the
// values the code used before, so a file that asks stays identical to upstream when the theme is Classic.
namespace KogTheme
{
inline KogStyle::ETheme Current()
{
	return g_Config.m_ClKogTheme == (int)KogStyle::ETheme::DARK ? KogStyle::ETheme::DARK : KogStyle::ETheme::CLASSIC;
}

inline bool On() { return Current() != KogStyle::ETheme::CLASSIC; }

inline const KogStyle::SPalette &Palette() { return KogStyle::DARK; }

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
	return On() && Neutral ? Classic.WithMultipliedAlpha(0.22f) : Classic;
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
	return Checked ? Palette().m_Brand.WithAlpha(std::min(1.0f, 0.55f * HoverMul + 0.2f)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * HoverMul);
}
}

#endif
