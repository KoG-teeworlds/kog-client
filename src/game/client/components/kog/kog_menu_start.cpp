#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/kog/kog_login.h>
#include <game/client/components/menus_start.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/kog_draw.h>
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
		if(g_Config.m_UiSmoothScrollTime <= 0 || g_Config.m_ClKogReduceMotion)
			return Target;
		const float Step = FrameTime / Motion::Hover;
		return Value < Target ? std::min(Value + Step, Target) : std::max(Value - Step, Target);
	}

	void DrawCard(const CUIRect &Rect, const ColorRGBA &Fill, float Radius)
	{
		KogDraw::Panel(Rect, {Fill, &Elevation::SMALL, Radius, IGraphics::CORNER_ALL, Elevation::GLASS_TOP_LIGHTEN});
	}
}

void CMenusStart::DrawKogScrim(const CUIRect &Screen)
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	const bool Glass = KogTheme::IsGlass(); // the glass look lets the map show: the sky is part of the look
	const ColorRGBA Top = Pal().m_Bg.WithAlpha(Glass ? 0.20f : 0.55f);
	const ColorRGBA Bottom = Pal().m_Bg.WithAlpha(Glass ? 0.55f : 0.80f);
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
		KogDraw::Panel(Rect, {Fill, &Elevation::RAISED, Radius::Panel, IGraphics::CORNER_ALL, 0.14f});
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
		const KogTheme::CTextColor IconPropsColor(TextRender(), Primary ? Pal().m_OnBrand : Pal().m_BrandSoft);
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
	const KogTheme::CTextColor PropsColor(TextRender(), TextColor);
	Props.m_MaxWidth = Content.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Content, pText, FontSize, pIcon != nullptr ? TEXTALIGN_ML : TEXTALIGN_MC, Props);

	return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
}

void CMenusStart::RenderKogStartMenu(CUIRect MainView, int &NewPage)
{
	// the rails run to the edges of the window, not of the inset the menu gives its pages
	MainView = *Ui()->Screen();
	DrawKogScrim(MainView);
	const Start::SPlan Plan = Start::PlanStart(MainView.w, MainView.h);
	const float Top = MainView.y;
	const float Bottom = MainView.y + MainView.h;

	// the rails: glass over the sky, the shadow of the left one falls on the page
	const CUIRect LeftRail = {MainView.x, Top, Plan.m_RailWidth, MainView.h};
	KogDraw::Panel(LeftRail, {Pal().m_Panel, &Elevation::FLOATING, 0.0f, IGraphics::CORNER_NONE, Elevation::GLASS_TOP_LIGHTEN});
	const float RightTop = Top + Start::RIGHT_TOP;
	const float RightBottom = Bottom - Start::RIGHT_BOTTOM;
	const CUIRect RightRail = {MainView.x + MainView.w - Plan.m_RightWidth, RightTop, Plan.m_RightWidth, std::max(RightBottom - RightTop, 1.0f)};
	KogDraw::Panel(RightRail, {Pal().m_Panel, &Elevation::RAISED, Radius::Panel, IGraphics::CORNER_L, Elevation::GLASS_TOP_LIGHTEN});

	const auto DrawLogo = [&](const CUIRect &Rect) {
		Graphics()->TextureSet(g_pData->m_aImages[KogTheme::IsLight() ? IMAGE_BANNER_LIGHT : IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		const IGraphics::CQuadItem Logo(Rect.x, Rect.y, Rect.w, Rect.h);
		Graphics()->QuadsDrawTL(&Logo, 1);
		Graphics()->QuadsEnd();
		Graphics()->TextureClear();
	};
	const auto Divider = [&](float Y) {
		const CUIRect Line = {LeftRail.x + Space::M, Y, LeftRail.w - 2.0f * Space::M, 1.0f};
		Line.Draw(KogTheme::Layer(0.14f), IGraphics::CORNER_NONE, 0.0f);
	};
	const auto DrawIcon = [&](const CUIRect &Rect, const char *pIcon, float Size, const ColorRGBA &Color) {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		SLabelProperties IconProps;
		const KogTheme::CTextColor IconColor(TextRender(), Color);
		Ui()->DoLabel(&Rect, pIcon, Size, TEXTALIGN_MC, IconProps);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	// logo of the rail
	const float RailLogoHeight = Start::LOGO_BLOCK - 2.0f * Space::M;
	const float RailLogoWidth = std::min(RailLogoHeight * 603.3f / 351.9f, LeftRail.w - 2.0f * Space::L);
	DrawLogo({LeftRail.x + Space::L, Top + Space::M, RailLogoWidth, RailLogoWidth * 351.9f / 603.3f});
	Divider(Top + Start::LOGO_BLOCK - 1.0f);

	// hero: the big logo and the one primary action
	{
		const float LogoWidth = Plan.m_HeroLogoHeight * 603.3f / 351.9f;
		DrawLogo({MainView.x + Plan.m_CentreX - LogoWidth / 2.0f, Top + Plan.m_HeroLogoY, LogoWidth, Plan.m_HeroLogoHeight});
	}

	static float s_aHover[16] = {};
	// keyboard: Up, Down, Left and Right move a focus ring over every control in reading order (hero, rail, links), Enter presses the
	// focused one (without focus, Enter is Play). The mouse is unchanged.
	enum
	{
		FOCUS_PLAY,
		FOCUS_DEMOS,
		FOCUS_EDITOR,
		FOCUS_SERVER,
		FOCUS_ACCOUNT,
		FOCUS_SETTINGS,
		FOCUS_QUIT,
		FOCUS_NEWS,
		FOCUS_WEBSITE,
		FOCUS_TUTORIAL,
		NUM_FOCUS
	};
	static int s_Focus = -1;
	CUIRect aFocusRects[NUM_FOCUS];
	for(CUIRect &Rect : aFocusRects)
		Rect = {-100.0f, -100.0f, 0.0f, 0.0f};
	const bool Enter = Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
	if(CheckHotKey(KEY_DOWN) || CheckHotKey(KEY_RIGHT))
		s_Focus = (s_Focus + 1) % NUM_FOCUS;
	else if(CheckHotKey(KEY_UP) || CheckHotKey(KEY_LEFT))
		s_Focus = s_Focus < 0 ? NUM_FOCUS - 1 : (s_Focus + NUM_FOCUS - 1) % NUM_FOCUS;

	static CButtonContainer s_PlayButton, s_DemosButton, s_EditorButton, s_ServerButton, s_SettingsButton, s_QuitButton, s_AccountButton, s_LogoutButton, s_DevicesButton;
	static CButtonContainer s_aLinkButtons[3];

	CUIRect PlayRect = {MainView.x + Plan.m_CentreX - Plan.m_PlayWidth / 2.0f, Top + Plan.m_PlayY, Plan.m_PlayWidth, Size::Hero};
	aFocusRects[FOCUS_PLAY] = PlayRect;
	if(DoKogButton(&s_PlayButton, &s_aHover[0], PlayRect, FontIcon::PLAY, Localize("Play", "Start menu"), (int)EButtonKind::PRIMARY, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus <= FOCUS_PLAY) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;

	// a row of the rail: an icon, a word, a quiet fill that comes with the hover
	const auto DoRailRow = [&](const void *pId, float *pHover, const CUIRect &Row, const char *pIcon, const char *pText, const ColorRGBA &Dot) {
		const bool Hot = Ui()->HotItem() == pId;
		*pHover = Approach(*pHover, Hot ? 1.0f : 0.0f, Client()->RenderFrameTime());
		const float Overlay = Ui()->CheckActiveItem(pId) ? State::PressOverlay : *pHover * State::HoverOverlay;
		if(Overlay > 0.0f)
			Row.Draw(KogTheme::Layer(Overlay + 0.04f), IGraphics::CORNER_ALL, Radius::Card);
		CUIRect Icon, Label;
		Row.VSplitLeft(Space::M, nullptr, &Label);
		Label.VSplitLeft(Icon::Large, &Icon, &Label);
		Label.VSplitLeft(Space::S, nullptr, &Label);
		DrawIcon(Icon, pIcon, Font::Label, Pal().m_BrandSoft);
		if(Dot.a > 0.0f)
		{
			CUIRect DotRect;
			Label.VSplitRight(Space::M, &Label, &DotRect);
			DotRect.HMargin((DotRect.h - Space::S) / 2.0f, &DotRect);
			DotRect.VSplitLeft(Space::S, &DotRect, nullptr);
			DotRect.Draw(Dot, IGraphics::CORNER_ALL, Space::XS);
		}
		SLabelProperties Props;
		const KogTheme::CTextColor PropsColor(TextRender(), Pal().m_Fg);
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pText, Font::Body, TEXTALIGN_ML, Props);
		return Ui()->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_LEFT) != 0;
	};

	// navigation
	CUIRect Nav = {LeftRail.x + Space::S, Top + Plan.m_NavY, LeftRail.w - 2.0f * Space::S, Start::ROW};
	aFocusRects[FOCUS_DEMOS] = Nav;
	if(DoRailRow(&s_DemosButton, &s_aHover[1], Nav, FontIcon::FILM, Localize("Demos"), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_DEMOS) || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;
	Nav.y += Start::ROW;
	aFocusRects[FOCUS_EDITOR] = Nav;
	const bool Unsaved = GameClient()->Editor()->HasUnsavedData();
	if(DoRailRow(&s_EditorButton, &s_aHover[2], Nav, FontIcon::PEN_TO_SQUARE, Localize("Editor"), Unsaved ? Pal().m_Warn : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_EDITOR) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}
	Nav.y += Start::ROW;
	aFocusRects[FOCUS_SERVER] = Nav;
	const bool ServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(DoRailRow(&s_ServerButton, &s_aHover[3], Nav, FontIcon::TERMINAL, ServerRunning ? Localize("Stop server") : Localize("Run server"), ServerRunning ? Pal().m_Ok : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_SERVER) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(ServerRunning)
			GameClient()->m_LocalServer.KillServer();
		else
			GameClient()->m_LocalServer.RunServer({});
	}

	// bottom of the rail, from the bottom up: Quit, Settings, the account
	CUIRect QuitRow = {LeftRail.x + Space::S, Bottom - Space::S - Start::ROW, LeftRail.w - 2.0f * Space::S, Start::ROW};
	CUIRect SettingsRow = QuitRow;
	SettingsRow.y -= Start::ROW;
	aFocusRects[FOCUS_QUIT] = QuitRow;
	aFocusRects[FOCUS_SETTINGS] = SettingsRow;
	bool UsedEscape = false;
	if(DoRailRow(&s_QuitButton, &s_aHover[5], QuitRow, FontIcon::POWER_OFF, Localize("Quit"), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_QUIT) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}
	if(DoRailRow(&s_SettingsButton, &s_aHover[4], SettingsRow, FontIcon::GEAR, Localize("Settings"), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) || (Enter && s_Focus == FOCUS_SETTINGS) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;
	Divider(SettingsRow.y - Space::XS);

	// the account: the tee, the state in words, one action. Logged in, a click opens two more actions. No new network behaviour:
	// the same StartLogin and StartLogout as the settings page, and a link to the account site in the browser.
	if(g_Config.m_ClKogLogin)
	{
		static bool s_AccountOpen = false;
		CKogLogin &Login = GameClient()->m_KogLogin;
		const bool LoggedIn = Login.HasDeviceKey() && g_Config.m_ClKogAccountName[0] != '\0';
		const bool Busy = Login.Busy();
		float Y = SettingsRow.y - Space::XS - Start::ACCOUNT_BLOCK;
		if(LoggedIn && s_AccountOpen)
		{
			CUIRect Action = {LeftRail.x + Space::S, Y - Start::ACCOUNT_ACTION, LeftRail.w - 2.0f * Space::S, Start::ACCOUNT_ACTION};
			if(DoRailRow(&s_DevicesButton, &s_aHover[7], Action, FontIcon::KEY, Localize("Devices on kog.tw"), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) && str_startswith(g_Config.m_ClKogAccountUrl, "https://"))
				Client()->ViewLink(g_Config.m_ClKogAccountUrl);
			Action.y -= Start::ACCOUNT_ACTION;
			if(DoRailRow(&s_LogoutButton, &s_aHover[8], Action, FontIcon::RIGHT_TO_BRACKET, Localize("Log out on this device"), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) && !Busy)
			{
				Login.StartLogout();
				s_AccountOpen = false;
			}
			Y -= 2.0f * Start::ACCOUNT_ACTION;
		}
		const CUIRect Block = {LeftRail.x + Space::S, Y, LeftRail.w - 2.0f * Space::S, Start::ACCOUNT_BLOCK};
		aFocusRects[FOCUS_ACCOUNT] = Block;
		const bool Hot = Ui()->HotItem() == &s_AccountButton;
		s_aHover[6] = Approach(s_aHover[6], Hot ? 1.0f : 0.0f, Client()->RenderFrameTime());
		if(s_aHover[6] > 0.0f)
			Block.Draw(KogTheme::Layer(s_aHover[6] * State::HoverOverlay + 0.04f), IGraphics::CORNER_ALL, Radius::Card);

		// the tee of the player in a round socket
		const float Socket = 40.0f;
		const CUIRect Avatar = {Block.x + Space::S, Block.y + (Block.h - Socket) / 2.0f, Socket, Socket};
		Avatar.Draw(KogTheme::Layer(0.14f), IGraphics::CORNER_ALL, Socket / 2.0f);
		{
			const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
			const CSkins::CSkinContainer *pContainer = GameClient()->m_Skins.FindContainerOrNullptr(g_Config.m_ClPlayerSkin[0] == '\0' ? "default" : g_Config.m_ClPlayerSkin);
			if(pContainer != nullptr && pContainer->IsSpecial())
				pContainer = nullptr;
			CTeeRenderInfo TeeInfo;
			TeeInfo.Apply(pContainer == nullptr || pContainer->Skin() == nullptr ? pDefaultSkin : pContainer->Skin().get());
			TeeInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
			TeeInfo.m_Size = Socket * 0.85f;
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(Avatar.x + Avatar.w / 2.0f, Avatar.y + Avatar.h / 2.0f + OffsetToMid.y));
		}
		// the state as a mark on the socket and in words next to it
		CUIRect Mark = {Avatar.x + Socket - Space::S - 1.0f, Avatar.y + Socket - Space::S - 1.0f, Space::S + 2.0f, Space::S + 2.0f};
		Mark.Draw(Pal().m_Panel.WithAlpha(1.0f), IGraphics::CORNER_ALL, Mark.w / 2.0f);
		Mark.Margin(1.0f, &Mark);
		Mark.Draw(LoggedIn ? Pal().m_Ok : Pal().m_Muted, IGraphics::CORNER_ALL, Mark.w / 2.0f);

		CUIRect Text = Block;
		Text.VSplitLeft(Socket + 2.0f * Space::S, nullptr, &Text);
		Text.VSplitRight(Space::S, &Text, nullptr);
		CUIRect Caption, Name;
		Text.HSplitMid(&Caption, &Name);
		const char *pCaption = Busy ? Localize("Logging in...") : (LoggedIn ? Localize("Logged in as") : Localize("Not logged in"));
		{
			SLabelProperties Props;
			const KogTheme::CTextColor PropsColor(TextRender(), Pal().m_Muted);
			Props.m_MaxWidth = Caption.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Caption, pCaption, Font::Caption, TEXTALIGN_BL, Props);
		}
		{
			SLabelProperties Props;
			const KogTheme::CTextColor PropsColor(TextRender(), LoggedIn ? Pal().m_BrandSoft : Pal().m_Fg);
			Props.m_MaxWidth = Name.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Name, LoggedIn ? g_Config.m_ClKogAccountName : (Busy ? Localize("Cancel") : Localize("Log in with KoG")), Font::Body, TEXTALIGN_TL, Props);
		}
		if(Ui()->DoButtonLogic(&s_AccountButton, 0, &Block, BUTTONFLAG_LEFT) || (Enter && s_Focus == FOCUS_ACCOUNT))
		{
			if(Busy)
				Login.Cancel();
			else if(LoggedIn)
				s_AccountOpen = !s_AccountOpen;
			else
				Login.StartLogin();
		}
	}

	// links in the right rail, from the bottom up, an icon and a word each
	{
		const struct
		{
			const char *m_pIcon;
			const char *m_pText;
		} aLinks[3] = {{FontIcon::NEWSPAPER, Localize("News")}, {FontIcon::EARTH_AMERICAS, Localize("Website")}, {FontIcon::QUESTION, Localize("Tutorial")}};
		const float Item = 60.0f;
		for(int i = 0; i < 3; ++i)
		{
			const CUIRect Cell = {RightRail.x, RightRail.y + RightRail.h - Space::S - (3 - i) * Item, RightRail.w, Item};
			aFocusRects[FOCUS_NEWS + i] = Cell;
			const bool Hot = Ui()->HotItem() == &s_aLinkButtons[i];
			s_aHover[9 + i] = Approach(s_aHover[9 + i], Hot ? 1.0f : 0.0f, Client()->RenderFrameTime());
			if(s_aHover[9 + i] > 0.0f)
				Cell.Draw(KogTheme::Layer(s_aHover[9 + i] * State::HoverOverlay + 0.04f), IGraphics::CORNER_ALL, Radius::Card);
			CUIRect Icon, Label;
			Cell.HSplitTop(Cell.h * 0.55f, &Icon, &Label);
			DrawIcon(Icon, aLinks[i].m_pIcon, Font::Title * 0.85f, Hot ? Pal().m_Brand : Pal().m_BrandSoft);
			SLabelProperties Props;
			const KogTheme::CTextColor PropsColor(TextRender(), Hot ? Pal().m_Fg : Pal().m_Muted);
			Props.m_MaxWidth = Label.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Label, aLinks[i].m_pText, Font::Caption, TEXTALIGN_TC, Props);
			if(Ui()->DoButtonLogic(&s_aLinkButtons[i], 0, &Cell, BUTTONFLAG_LEFT) || (Enter && s_Focus == FOCUS_NEWS + i))
			{
				if(i == 0)
					NewPage = CMenus::PAGE_NEWS;
				else if(i == 1)
					Client()->ViewLink("https://kog.tw/");
				else
					GameClient()->m_Menus.JoinTutorial();
			}
		}
	}
	if(CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	if(s_Focus >= 0 && aFocusRects[s_Focus].w > 0.0f)
	{
		// a 2 unit outline outside the focused control, in the brand colour (3:1 against the panel is tested in kog_style_test)
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
}
