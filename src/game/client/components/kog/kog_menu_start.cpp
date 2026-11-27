#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/menus_start.h>
#include <game/client/gameclient.h>
#include <game/client/kog_theme.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

using namespace KogStyle;

static const SPalette &Pal() { return KogTheme::Palette(); }

namespace
{
enum class EButtonKind
{
	PRIMARY, // the brand colour, one per page
	SECONDARY,
	QUIET,
};

struct SKogButton
{
	const char *m_pIcon;
	const char *m_pText;
	EButtonKind m_Kind;
	ColorRGBA m_Dot = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f); // a state mark (running server); the text says the same
};

// Fades towards the target in Motion::Hover seconds, a frame time of zero or a disabled smooth scroll jumps
float Approach(float Value, float Target, float FrameTime)
{
	if(g_Config.m_UiSmoothScrollTime <= 0)
		return Target;
	const float Step = FrameTime / Motion::Hover;
	return Value < Target ? std::min(Value + Step, Target) : std::max(Value - Step, Target);
}

void DrawCard(const CUIRect &Rect, const ColorRGBA &Fill, float Radius)
{
	CUIRect Inner;
	Rect.Draw(Pal().m_Line, IGraphics::CORNER_ALL, Radius);
	Rect.Margin(1.0f, &Inner);
	Inner.Draw(Fill, IGraphics::CORNER_ALL, Radius - 1.0f);
}
}

void CMenusStart::DrawKogScrim(const CUIRect &Screen)
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	const ColorRGBA Top = Pal().m_Bg.WithAlpha(0.55f);
	const ColorRGBA Bottom = Pal().m_Bg.WithAlpha(0.80f);
	Graphics()->SetColor4(Top, Top, Bottom, Bottom);
	const IGraphics::CQuadItem Quad(Screen.x, Screen.y, Screen.w, Screen.h);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();
}

bool CMenusStart::DoKogButton(const void *pId, float *pHover, const CUIRect &Rect, const char *pIcon, const char *pText, int Kind, const ColorRGBA &Dot)
{
	const bool Primary = Kind == (int)EButtonKind::PRIMARY;
	const bool Quiet = Kind == (int)EButtonKind::QUIET;
	const bool Hot = Ui()->HotItem() == pId;
	*pHover = Approach(*pHover, Hot ? 1.0f : 0.0f, Client()->RenderFrameTime());
	const float Overlay = Ui()->CheckActiveItem(pId) ? State::PressOverlay : *pHover * State::HoverOverlay;

	ColorRGBA Fill = Primary ? Pal().m_Brand : (Quiet ? Pal().m_Panel : Pal().m_Panel2);
	const ColorRGBA TextColor = Primary ? Pal().m_OnBrand : (Quiet ? Pal().m_Muted : Pal().m_Fg);
	const float Radius = Primary ? Radius::Card : Radius::Control;
	// the overlay is a lighter layer on dark fills and a darker one on the brand fill
	const ColorRGBA OverlayColor = Primary ? ColorRGBA(0.0f, 0.0f, 0.0f, Overlay) : KogTheme::Layer(Overlay);
	Fill = Over(OverlayColor, Fill.WithAlpha(Fill.a));
	Fill.a = Primary ? 1.0f : (Quiet ? Pal().m_Panel.a : Pal().m_Panel2.a);

	if(Primary)
		Rect.Draw(Fill, IGraphics::CORNER_ALL, Radius);
	else
		DrawCard(Rect, Fill, Radius);

	CUIRect Content;
	Rect.VMargin(Space::M, &Content);
	const float FontSize = Primary ? Font::Title : Font::Body;

	if(pIcon != nullptr)
	{
		CUIRect Icon;
		Content.VSplitLeft(FontSize + Space::XS, &Icon, &Content);
		Content.VSplitLeft(Space::S, nullptr, &Content);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		SLabelProperties IconProps;
		IconProps.SetColor(Primary ? Pal().m_OnBrand : Pal().m_BrandSoft);
		Ui()->DoLabel(&Icon, pIcon, FontSize * 0.9f, TEXTALIGN_MC, IconProps);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	if(Dot.a > 0.0f)
	{
		CUIRect DotRect;
		Content.VSplitRight(Space::S, &Content, &DotRect);
		DotRect.HMargin((DotRect.h - Space::S) / 2.0f, &DotRect);
		DotRect.Draw(Dot, IGraphics::CORNER_ALL, Space::XS);
		Content.VSplitRight(Space::XS, &Content, nullptr);
	}

	SLabelProperties Props;
	Props.SetColor(TextColor);
	Props.m_MaxWidth = Content.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Content, pText, FontSize, pIcon != nullptr ? TEXTALIGN_ML : TEXTALIGN_MC, Props);

	return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
}

void CMenusStart::RenderKogStartMenu(CUIRect MainView, int &NewPage)
{
	DrawKogScrim(*Ui()->Screen());

	// logo, never stretched; smaller in a low window so that the buttons keep their place
	const float Scale = std::min(1.0f, MainView.h / 560.0f);
	const float LogoHeight = 96.0f * Scale;
	const float LogoWidth = LogoHeight * 603.3f / 351.9f;
	Graphics()->TextureSet(g_pData->m_aImages[KogTheme::IsLight() ? IMAGE_BANNER_LIGHT : IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	const IGraphics::CQuadItem Logo(MainView.x + MainView.w / 2.0f - LogoWidth / 2.0f, MainView.y + Space::XXL * Scale, LogoWidth, LogoHeight);
	Graphics()->QuadsDrawTL(&Logo, 1);
	Graphics()->QuadsEnd();

	CUIRect Column = MainView;
	Column.HSplitTop(Space::XXL * Scale + LogoHeight + Space::XL, nullptr, &Column);
	Column.VMargin(std::max(Space::L, (MainView.w - 380.0f) / 2.0f), &Column);

	static float s_aHover[8] = {};
	// keyboard: Up, Down, Left and Right move a focus ring over the buttons in reading order, Enter presses the focused
	// one (without focus, Enter is Play). The mouse is unchanged.
	enum
	{
		FOCUS_PLAY,
		FOCUS_DEMOS,
		FOCUS_EDITOR,
		FOCUS_SERVER,
		FOCUS_SETTINGS,
		FOCUS_QUIT,
		NUM_FOCUS
	};
	static int s_Focus = -1;
	CUIRect aFocusRects[NUM_FOCUS];
	const bool Enter = Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
	if(CheckHotKey(KEY_DOWN) || CheckHotKey(KEY_RIGHT))
		s_Focus = (s_Focus + 1) % NUM_FOCUS;
	else if(CheckHotKey(KEY_UP) || CheckHotKey(KEY_LEFT))
		s_Focus = s_Focus < 0 ? NUM_FOCUS - 1 : (s_Focus + NUM_FOCUS - 1) % NUM_FOCUS;
	static CButtonContainer s_PlayButton, s_DemosButton, s_EditorButton, s_ServerButton, s_SettingsButton, s_QuitButton;
	CUIRect Row;

	Column.HSplitTop(Size::Hero, &Row, &Column);
	aFocusRects[FOCUS_PLAY] = Row;
	if(DoKogButton(&s_PlayButton, &s_aHover[0], Row, FontIcon::PLAY, Localize("Play", "Start menu"), (int)EButtonKind::PRIMARY, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus <= FOCUS_PLAY) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;

	Column.HSplitTop(Space::S, nullptr, &Column);
	Column.HSplitTop(Size::Button, &Row, &Column);
	// two columns: the longest translations of "Run server" need about 150 units at the body size
	CUIRect Left, Right;
	const float HalfWidth = (Row.w - Space::S) / 2.0f;
	Row.VSplitLeft(HalfWidth, &Left, &Row);
	Row.VSplitLeft(Space::S, nullptr, &Right);
	aFocusRects[FOCUS_DEMOS] = Left;
	aFocusRects[FOCUS_EDITOR] = Right;

	if(DoKogButton(&s_DemosButton, &s_aHover[1], Left, FontIcon::FILM, Localize("Demos"), (int)EButtonKind::SECONDARY, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_DEMOS) || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;

	const bool Unsaved = GameClient()->Editor()->HasUnsavedData();
	if(DoKogButton(&s_EditorButton, &s_aHover[2], Right, FontIcon::PEN_TO_SQUARE, Localize("Editor"), (int)EButtonKind::SECONDARY, Unsaved ? Pal().m_Warn : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_EDITOR) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	Column.HSplitTop(Space::S, nullptr, &Column);
	Column.HSplitTop(Size::Button, &Row, &Column);
	Row.VSplitLeft(HalfWidth, &Left, &Row);
	Row.VSplitLeft(Space::S, nullptr, &Right);
	aFocusRects[FOCUS_SERVER] = Left;
	aFocusRects[FOCUS_SETTINGS] = Right;

	const bool ServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(DoKogButton(&s_ServerButton, &s_aHover[3], Left, FontIcon::TERMINAL, ServerRunning ? Localize("Stop server") : Localize("Run server"), (int)EButtonKind::SECONDARY, ServerRunning ? Pal().m_Ok : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_SERVER) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(ServerRunning)
			GameClient()->m_LocalServer.KillServer();
		else
			GameClient()->m_LocalServer.RunServer({});
	}

	if(DoKogButton(&s_SettingsButton, &s_aHover[4], Right, FontIcon::GEAR, Localize("Settings"), (int)EButtonKind::SECONDARY, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_SETTINGS) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	Column.HSplitTop(Space::XL, nullptr, &Column);
	Column.HSplitTop(Size::Button, &Row, &Column);
	aFocusRects[FOCUS_QUIT] = Row;
	bool UsedEscape = false;
	if(DoKogButton(&s_QuitButton, &s_aHover[5], Row, FontIcon::POWER_OFF, Localize("Quit"), (int)EButtonKind::QUIET, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_QUIT) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}

	if(s_Focus >= 0)
	{
		// a 2 unit outline outside the button, in the brand colour (3:1 against the panel is tested in kog_style_test)
		const CUIRect &Focused = aFocusRects[s_Focus];
		const float W = Size::Focus;
		const ColorRGBA Ring = Pal().m_Brand;
		CUIRect Outer = Focused;
		Outer.Margin(-W, &Outer);
		CUIRect Edge;
		Outer.HSplitTop(W, &Edge, &Outer);
		Edge.Draw(Ring, IGraphics::CORNER_NONE, 0.0f);
		Outer.HSplitBottom(W, &Outer, &Edge);
		Edge.Draw(Ring, IGraphics::CORNER_NONE, 0.0f);
		Outer.VSplitLeft(W, &Edge, &Outer);
		Edge.Draw(Ring, IGraphics::CORNER_NONE, 0.0f);
		Outer.VSplitRight(W, &Outer, &Edge);
		Edge.Draw(Ring, IGraphics::CORNER_NONE, 0.0f);
	}

	// links, bottom left
	CUIRect Links = MainView;
	Links.VMargin(Space::L, &Links);
	Links.HSplitBottom(Space::XL, &Links, &Row);
	const struct
	{
		const char *m_pText;
		int m_Id;
	} aLinks[] = {{Localize("News"), 0}, {Localize("Website"), 1}, {Localize("Tutorial"), 2}};
	static CButtonContainer s_aLinkButtons[3];
	for(const auto &Link : aLinks)
	{
		CUIRect Item;
		const float Width = TextRender()->TextWidth(Font::Body, Link.m_pText) + 2.0f * Space::M;
		Row.VSplitLeft(Width, &Item, &Row);
		Row.VSplitLeft(Space::XS, nullptr, &Row);
		const bool Hot = Ui()->HotItem() == &s_aLinkButtons[Link.m_Id];
		SLabelProperties Props;
		Props.SetColor(Hot ? Pal().m_Fg : Pal().m_Muted);
		Ui()->DoLabel(&Item, Link.m_pText, Font::Body, TEXTALIGN_MC, Props);
		if(Ui()->DoButtonLogic(&s_aLinkButtons[Link.m_Id], 0, &Item, BUTTONFLAG_LEFT))
		{
			if(Link.m_Id == 0)
				NewPage = CMenus::PAGE_NEWS;
			else if(Link.m_Id == 1)
				Client()->ViewLink("https://kog.tw/");
			else
				GameClient()->m_Menus.JoinTutorial();
		}
	}
	if(CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	// account state: a mark and always a word
	if(g_Config.m_ClKogLogin)
	{
		const bool LoggedIn = GameClient()->m_KogLogin.HasDeviceKey() && g_Config.m_ClKogAccountName[0] != '\0';
		char aText[128];
		if(LoggedIn)
			str_format(aText, sizeof(aText), Localize("Logged in as %s"), g_Config.m_ClKogAccountName);
		else
			str_copy(aText, Localize("Not logged in"));
		CUIRect Chip = MainView;
		Chip.HSplitTop(Size::Row, &Chip, nullptr);
		Chip.VMargin(Space::L, &Chip);
		Chip.VSplitLeft(std::min(Chip.w, TextRender()->TextWidth(Font::Body, aText) + 2.0f * Space::L + Space::S), &Chip, nullptr);
		DrawCard(Chip, Pal().m_Panel, Radius::Control);
		CUIRect Dot;
		Chip.VMargin(Space::M, &Chip);
		Chip.VSplitLeft(Space::S, &Dot, &Chip);
		Dot.HMargin((Dot.h - Space::S) / 2.0f, &Dot);
		Dot.Draw(LoggedIn ? Pal().m_Ok : Pal().m_Muted, IGraphics::CORNER_ALL, Space::XS);
		Chip.VSplitLeft(Space::S, nullptr, &Chip);
		SLabelProperties Props;
		Props.SetColor(Pal().m_Fg);
		Props.m_MaxWidth = Chip.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Chip, aText, Font::Body, TEXTALIGN_ML, Props);
	}
}
