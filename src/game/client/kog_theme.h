#ifndef GAME_CLIENT_KOG_THEME_H
#define GAME_CLIENT_KOG_THEME_H

#include "kog_draw.h"
#include "kog_style.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/ui_rect.h>

// What the shared UI code asks: is the KoG look on, and which colours does it use. Classic returns exactly the
// values the code used before, so a file that asks stays identical to upstream when the theme is Classic.
namespace KogTheme
{
	inline KogStyle::ETheme Current() { return KogStyle::ThemeFromSetting(g_Config.m_ClKogTheme); }

	inline bool On() { return Current() != KogStyle::ETheme::CLASSIC; }
	inline bool IsLight() { return Current() == KogStyle::ETheme::LIGHT; }

	inline bool IsGlass() { return Current() == KogStyle::ETheme::GLASS; }

	inline const KogStyle::SPalette &Palette()
	{
		return IsLight() ? KogStyle::LIGHT : (IsGlass() ? KogStyle::GLASS : KogStyle::DARK);
	}

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
		if(!On())
			return Classic;
		const KogStyle::SPalette &P = Palette();
		// a meaningful colour keeps its meaning with the colour of the palette, dark enough that the text on it stays readable
		const auto Tinted = [&](const ColorRGBA &Base) {
			const float Mix = IsLight() ? 1.0f : 0.45f;
			return ColorRGBA(Base.r * Mix, Base.g * Mix, Base.b * Mix, std::min(1.0f, Classic.a * 1.4f + 0.15f));
		};
		switch(KogStyle::Classify(Classic))
		{
		case KogStyle::ESemantic::GOOD: return Tinted(P.m_Ok);
		case KogStyle::ESemantic::BAD: return Tinted(P.m_Err);
		case KogStyle::ESemantic::WARN: return Tinted(P.m_Warn);
		case KogStyle::ESemantic::OTHER: return Tinted(P.m_Brand);
		case KogStyle::ESemantic::NEUTRAL: break;
		}
		if(Classic.r < 0.1f) // black layers (disabled, shadows) stay
			return Classic;
		return IsLight() ? Layer(Classic.a * 0.22f) : Classic.WithMultipliedAlpha(0.22f * Classic.r);
	}

	// A button, input or box: the control fill with the corners of the glass look, a lighter top and a rim light
	inline void DrawControl(const CUIRect &Rect, const ColorRGBA &Classic, int Corners, float ClassicRounding)
	{
		if(!On())
		{
			Rect.Draw(Classic, Corners, ClassicRounding);
			return;
		}
		const ColorRGBA Fill = Control(Classic);
		const float Radius = std::min(std::max(ClassicRounding, KogStyle::Radius::Control), std::min(Rect.w, Rect.h) / 2.0f);
		const ColorRGBA Top = KogDraw::Lightened(Fill, IsLight() ? 0.0f : 0.10f);
		Rect.Draw4(Top, Top, Fill, Fill, Corners, Radius);
		if((Corners & IGraphics::CORNER_T) == IGraphics::CORNER_T && Rect.w > 2.0f * Radius)
		{
			const CUIRect Rim = {Rect.x + Radius, Rect.y, Rect.w - 2.0f * Radius, 1.0f};
			Rim.Draw(Layer(IsLight() ? 0.10f : 0.12f), IGraphics::CORNER_NONE, 0.0f);
		}
	}

	// Header cells of lists (column titles): a quiet layer, the sorted column in the brand colour
	inline ColorRGBA GridHeader(const ColorRGBA &Classic, int Checked)
	{
		if(!On())
			return Classic;
		return Checked == 2 ? Palette().m_Brand.WithAlpha(0.55f) : Layer(0.16f);
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
	// The view of a menu page: the classic flat rectangle, or a glass panel with a shadow
	inline void DrawView(const CUIRect &Rect, const ColorRGBA &Classic, int Corners, float ClassicRadius)
	{
		if(!On())
		{
			Rect.Draw(Classic, Corners, ClassicRadius);
			return;
		}
		KogDraw::Panel(Rect, {Classic, &KogStyle::Elevation::RAISED, KogStyle::Radius::Panel, Corners, KogStyle::Elevation::GLASS_TOP_LIGHTEN});
	}

	// While it lives, the menus draw their text with the colours of the theme; the destructor puts the defaults back
	// so the game view, the HUD and the console keep their white text.
	class CMenuTextScope
	{
		ITextRender *m_pTextRender;
		ColorRGBA m_PreviousColor;
		ColorRGBA m_PreviousOutline;

		void Set(ColorRGBA Color, ColorRGBA Outline)
		{
			m_pTextRender->SetDefaultTextColors(Color, Outline);
			m_pTextRender->TextColor(Color);
			m_pTextRender->TextOutlineColor(Outline);
		}

	public:
		explicit CMenuTextScope(ITextRender *pTextRender) :
			m_pTextRender(pTextRender),
			m_PreviousColor(pTextRender->DefaultTextColor()),
			m_PreviousOutline(pTextRender->DefaultTextOutlineColor())
		{
			Set(TextColor(), TextOutlineColor());
		}
		~CMenuTextScope() { Set(m_PreviousColor, m_PreviousOutline); }
		CMenuTextScope(const CMenuTextScope &) = delete;
		CMenuTextScope &operator=(const CMenuTextScope &) = delete;
	};
	// Text with a colour of its own in the menus. A colour in SLabelProperties is multiplied with the default text colour, which
	// is dark in the light theme, so a bright colour cannot be reached that way. This sets the default for its lifetime instead.
	class CTextColor
	{
		ITextRender *m_pTextRender;
		ColorRGBA m_Color;
		ColorRGBA m_Outline;

	public:
		CTextColor(ITextRender *pTextRender, ColorRGBA Color) :
			m_pTextRender(pTextRender),
			m_Color(pTextRender->DefaultTextColor()),
			m_Outline(pTextRender->DefaultTextOutlineColor())
		{
			const float Luminance = 0.2126f * Color.r + 0.7152f * Color.g + 0.0722f * Color.b;
			Set(Color, Luminance > 0.5f ? ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f * Color.a) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f * Color.a));
		}
		~CTextColor() { Set(m_Color, m_Outline); }
		CTextColor(const CTextColor &) = delete;
		CTextColor &operator=(const CTextColor &) = delete;

	private:
		void Set(ColorRGBA Color, ColorRGBA Outline)
		{
			m_pTextRender->SetDefaultTextColors(Color, Outline);
			m_pTextRender->TextColor(Color);
			m_pTextRender->TextOutlineColor(Outline);
		}
	};
}

#endif
