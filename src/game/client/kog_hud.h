#ifndef GAME_CLIENT_KOG_HUD_H
#define GAME_CLIENT_KOG_HUD_H

#include "kog_draw.h"
#include "kog_theme.h"

#include <game/client/ui_rect.h>

#include <algorithm>

// What the HUD components ask while the KoG look is on (design C, tokens in kog_style.h). With the Classic theme every function
// returns the classic value it is given, so the component draws exactly what upstream draws.
namespace KogHud
{
	inline bool On() { return KogTheme::On(); }

	// Text is never smaller than Hud::MIN_TEXT in the KoG look
	inline float Text(float Classic) { return On() ? std::max(Classic, KogStyle::Hud::MIN_TEXT) : Classic; }
	// Rows are never lower than Hud::MIN_ROW
	// For code that draws in the 300 high HUD space (chat): half the size in UI units
	inline float TextIn300(float Classic) { return On() ? std::max(Classic, KogStyle::Hud::MIN_TEXT / 2.0f) : Classic; }
	inline float Row(float Classic) { return On() ? std::max(Classic, KogStyle::Hud::MIN_ROW) : Classic; }
	inline float Radius(float Classic) { return On() ? KogStyle::Hud::RADIUS : Classic; }

	inline ColorRGBA Panel(const ColorRGBA &Classic)
	{
		if(!On())
			return Classic;
		return KogTheme::IsGlass() ? KogTheme::Palette().m_Panel : KogTheme::Palette().m_Panel.WithAlpha(KogStyle::Hud::PANEL_ALPHA);
	}

	// A HUD panel: the classic flat rectangle, or in the KoG look a glass panel with a shadow
	inline void DrawPanel(const CUIRect &Rect, const ColorRGBA &Classic, int Corners, float ClassicRadius, const KogStyle::Elevation::SLevel &Level = KogStyle::Elevation::RAISED)
	{
		if(!On())
		{
			Rect.Draw(Classic, Corners, ClassicRadius);
			return;
		}
		KogDraw::Panel(Rect, {Panel(Classic), &Level, KogStyle::Hud::PANEL_RADIUS, Corners, KogStyle::Elevation::GLASS_TOP_LIGHTEN});
	}
	inline ColorRGBA Headline(const ColorRGBA &Classic) { return On() ? KogTheme::Palette().m_Muted : Classic; }
	inline ColorRGBA Self(const ColorRGBA &Classic) { return On() ? KogTheme::Palette().m_Brand.WithAlpha(KogStyle::Hud::SELF_ALPHA) : Classic; }
	// Ping: the three status colours of the palette (they differ in lightness, the number is always shown too)
	inline ColorRGBA Ping(int Latency)
	{
		const KogStyle::SPalette &P = KogTheme::Palette();
		return Latency <= 60 ? P.m_Ok : (Latency <= 120 ? P.m_Warn : P.m_Err);
	}
	inline ColorRGBA Accent() { return KogTheme::Palette().m_Brand; }
	inline ColorRGBA Zebra() { return KogTheme::Layer(KogStyle::Hud::ZEBRA_ALPHA); }
	inline ColorRGBA Header() { return KogTheme::Palette().m_Brand; }
	inline ColorRGBA OnHeader() { return KogTheme::Palette().m_OnBrand; }

	// Row measurements of the scoreboard in the KoG look: one size for every player count
	inline void ScoreboardMeasure(float &LineHeight, float &TeeSizeMod, float &Spacing, float &RoundRadius, float &FontSize)
	{
		LineHeight = KogStyle::Hud::ROW;
		TeeSizeMod = 0.3f;
		Spacing = 0.0f;
		RoundRadius = KogStyle::Hud::RADIUS;
		FontSize = KogStyle::Hud::TEXT;
	}
}

#endif
