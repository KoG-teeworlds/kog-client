#include "kog_draw.h"

#include <engine/graphics.h>
#include <engine/image.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

static IGraphics *gs_pGraphics = nullptr;
static IGraphics::CTextureHandle gs_ShadowTexture;

KogDraw::SNineSlice KogDraw::PlanShadow(float X, float Y, float Width, float Height, float Blur, float OffsetY)
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

void KogDraw::Init(IGraphics *pGraphics)
{
	gs_pGraphics = pGraphics;
	CImageInfo Image;
	Image.m_Width = TEXTURE_SIZE;
	Image.m_Height = TEXTURE_SIZE;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();
	const float Half = TEXTURE_SIZE / 2.0f;
	const float BoxHalf = Half - EDGE; // half the side of the box
	for(int y = 0; y < TEXTURE_SIZE; ++y)
	{
		for(int x = 0; x < TEXTURE_SIZE; ++x)
		{
			// signed distance to the rounded box, negative inside
			const float Qx = std::fabs(x + 0.5f - Half) - (BoxHalf - CORNER_RADIUS);
			const float Qy = std::fabs(y + 0.5f - Half) - (BoxHalf - CORNER_RADIUS);
			const float Outside = std::hypot(std::max(Qx, 0.0f), std::max(Qy, 0.0f));
			const float Distance = Outside + std::min(std::max(Qx, Qy), 0.0f) - CORNER_RADIUS;
			const float T = KogStyle::Clamp01(0.5f - Distance / (2.0f * EDGE));
			const float Alpha = T * T * (3.0f - 2.0f * T);
			// the very border is zero so that clamped sampling never smears
			const bool Border = x == 0 || y == 0 || x == TEXTURE_SIZE - 1 || y == TEXTURE_SIZE - 1;
			uint8_t *pPixel = Image.m_pData + ((size_t)y * TEXTURE_SIZE + x) * 4;
			pPixel[0] = pPixel[1] = pPixel[2] = 255;
			pPixel[3] = Border ? 0 : (uint8_t)std::lround(Alpha * 255.0f);
		}
	}
	gs_ShadowTexture = gs_pGraphics->LoadTextureRawMove(Image, 0, "kog_shadow");
}

ColorRGBA KogDraw::Lightened(const ColorRGBA &Base, float Amount)
{
	return ColorRGBA(
		Base.r + (1.0f - Base.r) * Amount,
		Base.g + (1.0f - Base.g) * Amount,
		Base.b + (1.0f - Base.b) * Amount,
		Base.a);
}

void KogDraw::Shadow(const CUIRect &Rect, const KogStyle::Elevation::SLevel &Level)
{
	if(gs_pGraphics == nullptr || !gs_ShadowTexture.IsValid() || Level.m_Blur <= 0.0f || Level.m_ShadowAlpha <= 0.0f)
		return;
	const SNineSlice Plan = PlanShadow(Rect.x, Rect.y, Rect.w, Rect.h, Level.m_Blur, Level.m_OffsetY);
	const float Slice = (float)SLICE / TEXTURE_SIZE;
	const float aU[4] = {0.0f, Slice, 1.0f - Slice, 1.0f};
	gs_pGraphics->TextureSet(gs_ShadowTexture);
	gs_pGraphics->QuadsBegin();
	gs_pGraphics->SetColor(0.0f, 0.0f, 0.0f, Level.m_ShadowAlpha);
	for(int Row = 0; Row < 3; ++Row)
	{
		for(int Column = 0; Column < 3; ++Column)
		{
			gs_pGraphics->QuadsSetSubset(aU[Column], aU[Row], aU[Column + 1], aU[Row + 1]);
			const IGraphics::CQuadItem Quad(Plan.m_aX[Column], Plan.m_aY[Row], Plan.m_aX[Column + 1] - Plan.m_aX[Column], Plan.m_aY[Row + 1] - Plan.m_aY[Row]);
			gs_pGraphics->QuadsDrawTL(&Quad, 1);
		}
	}
	gs_pGraphics->QuadsEnd();
	gs_pGraphics->TextureClear();
}

void KogDraw::Panel(const CUIRect &Rect, const SGlass &Glass)
{
	if(gs_pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	const float Radius = std::min(Glass.m_Radius, std::min(Rect.w, Rect.h) / 2.0f);
	if(Glass.m_pLevel != nullptr)
		Shadow(Rect, *Glass.m_pLevel);
	const ColorRGBA Top = Lightened(Glass.m_Base, Glass.m_Lighten);
	gs_pGraphics->TextureClear();
	gs_pGraphics->DrawRect4(Rect.x, Rect.y, Rect.w, Rect.h, Top, Top, Glass.m_Base, Glass.m_Base, Glass.m_Corners, Radius);
	if(Glass.m_pLevel != nullptr && Glass.m_pLevel->m_RimAlpha > 0.0f && (Glass.m_Corners & IGraphics::CORNER_T) == IGraphics::CORNER_T)
	{
		gs_pGraphics->DrawRect(Rect.x + Radius, Rect.y, Rect.w - 2.0f * Radius, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, Glass.m_pLevel->m_RimAlpha), IGraphics::CORNER_NONE, 0.0f);
	}
}

void KogDraw::Pill(const CUIRect &Rect, ColorRGBA Color, const KogStyle::Elevation::SLevel &Level)
{
	if(gs_pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	const float Radius = Rect.h / 2.0f;
	Shadow(Rect, Level);
	gs_pGraphics->TextureClear();
	gs_pGraphics->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Color, IGraphics::CORNER_ALL, Radius);
}
