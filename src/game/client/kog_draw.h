#ifndef GAME_CLIENT_KOG_DRAW_H
#define GAME_CLIENT_KOG_DRAW_H

#include "kog_style.h"

#include <base/color.h>

#include <algorithm>
#include <cmath>

class IGraphics;
class CUIRect;

// Drawing primitives of the KoG look, on the engine's quads only (no shader, no render target): a soft shadow from one generated
// 9-slice texture, translucent panels with a vertical gradient and a rim light, pills. See docs/CLIENT-UI-DIRECTION.md.
namespace KogDraw
{
	// The 9-slice of a shadow: the texture is a blurred rounded box of TEXTURE_SIZE texels, its box starts EDGE texels in and has
	// corners of CORNER_RADIUS texels, so the corner slices are EDGE + CORNER_RADIUS texels, which keeps the arc out of the stretched middle.
	inline constexpr int TEXTURE_SIZE = 64;
	inline constexpr int EDGE = 24;
	inline constexpr int CORNER_RADIUS = 10;
	inline constexpr int SLICE = EDGE + CORNER_RADIUS;

	// Where the three columns and rows of the shadow begin: index 0..3 are the edges. Pure.
	struct SNineSlice
	{
		float m_aX[4];
		float m_aY[4];
	};
	// A panel of Width x Height at (X, Y), shadow reaching Blur units beyond it and moved down by OffsetY. Every edge is finite,
	// non-decreasing, and the corner slices never exceed half of the shadow's size.
	inline SNineSlice PlanShadow(float X, float Y, float Width, float Height, float Blur, float OffsetY)
	{
		const auto Finite = [](float V) { return std::isfinite(V) ? V : 0.0f; };
		X = Finite(X);
		Y = Finite(Y);
		Width = std::max(Finite(Width), 0.0f);
		Height = std::max(Finite(Height), 0.0f);
		Blur = std::max(Finite(Blur), 0.0f);
		OffsetY = Finite(OffsetY);
	
		const float Left = X - Blur;
		const float Top = Y + OffsetY - Blur;
		const float OuterWidth = Width + 2.0f * Blur;
		const float OuterHeight = Height + 2.0f * Blur;
		const float Scale = Blur / (float)EDGE;
		const float CornerX = std::min(SLICE * Scale, OuterWidth / 2.0f);
		const float CornerY = std::min(SLICE * Scale, OuterHeight / 2.0f);
	
		SNineSlice Plan;
		Plan.m_aX[0] = Left;
		Plan.m_aX[1] = Left + CornerX;
		Plan.m_aX[2] = Left + OuterWidth - CornerX;
		Plan.m_aX[3] = Left + OuterWidth;
		Plan.m_aY[0] = Top;
		Plan.m_aY[1] = Top + CornerY;
		Plan.m_aY[2] = Top + OuterHeight - CornerY;
		Plan.m_aY[3] = Top + OuterHeight;
		return Plan;
	}
	


	// Make the shadow texture. Called once with the graphics of the client; every draw function does nothing before that.
	void Init(IGraphics *pGraphics);

	void Shadow(const CUIRect &Rect, const KogStyle::Elevation::SLevel &Level);

	struct SGlass
	{
		ColorRGBA m_Base; // colour (with alpha) of the bottom of the panel
		const KogStyle::Elevation::SLevel *m_pLevel;
		float m_Radius;
		int m_Corners;
		float m_Lighten; // the top is this much lighter (white mixed in), 0 for a flat fill
	};

	// Shadow, gradient fill and rim light, in this order
	void Panel(const CUIRect &Rect, const SGlass &Glass);

	// A shadow-less fill with the radius clamped to half the shorter side: pills, chips, kill feed rows
	void Pill(const CUIRect &Rect, ColorRGBA Color, const KogStyle::Elevation::SLevel &Level);

	// The gradient of a glass panel: the colour of the top
	ColorRGBA Lightened(const ColorRGBA &Base, float Amount);
}

#endif
