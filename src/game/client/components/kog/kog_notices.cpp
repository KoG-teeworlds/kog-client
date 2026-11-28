#include "kog_notices.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/kog/kog_login.h>
#include <game/client/gameclient.h>
#include <game/client/kog_theme.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

using namespace KogStyle;

static constexpr int SERVER_CLIENT_ID = -1; // the sender of a server message

void CKogNotices::OnStateChange(int NewState, int OldState)
{
	m_Toasts.clear();
	m_RateLimit.Reset();
}

void CKogNotices::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg != NETMSGTYPE_SV_CHAT || Client()->State() != IClient::STATE_ONLINE)
		return;
	const CNetMsg_Sv_Chat *pMsg = static_cast<const CNetMsg_Sv_Chat *>(pRawMsg);
	if(pMsg->m_ClientId != SERVER_CLIENT_ID || pMsg->m_pMessage == nullptr)
		return;
	const char *pBody = nullptr;
	const KogNotices::EKind Kind = KogNotices::Classify(pMsg->m_pMessage, &pBody);
	if(Kind == KogNotices::EKind::NONE || !CKogLogin::ServerIsKog(ServerBrowser(), Client()->ServerAddress()))
		return;
	const float Now = Client()->GlobalTime();
	if(!m_RateLimit.Allow(Now))
		return;
	SToast Toast;
	Toast.m_Kind = Kind;
	Toast.m_Text.assign(pBody, std::min<size_t>(str_length(pBody), KogNotices::MAX_TEXT_BYTES));
	Toast.m_Start = Now;
	m_Toasts.push_back(std::move(Toast));
	if(m_Toasts.size() > (size_t)KogNotices::MAX_TOASTS_PER_WINDOW)
		m_Toasts.pop_front();
}

void CKogNotices::OnRender()
{
	if(m_Toasts.empty() || Client()->State() != IClient::STATE_ONLINE)
		return;
	const float Now = Client()->GlobalTime();
	while(!m_Toasts.empty() && Now - m_Toasts.front().m_Start > KogNotices::TOAST_SECONDS)
		m_Toasts.pop_front();

	Ui()->MapScreen();
	const CUIRect *pScreen = Ui()->Screen();
	const bool Themed = KogTheme::On();
	const SPalette &Palette = KogTheme::Palette();

	float Y = Space::XXL * 2.0f;
	for(const SToast &Toast : m_Toasts)
	{
		const float Age = Now - Toast.m_Start;
		const float FadeIn = std::min(1.0f, Age / Motion::Panel);
		const float FadeOut = std::min(1.0f, (KogNotices::TOAST_SECONDS - Age) / Motion::Panel);
		const float Alpha = std::min(FadeIn, FadeOut);

		const char *pTitle = Toast.m_Kind == KogNotices::EKind::ACHIEVEMENT ? Localize("Achievement unlocked") : Localize("Quest completed");
		const float Width = std::min(pScreen->w - 2.0f * Space::L, std::max(TextRender()->TextWidth(Font::Label, Toast.m_Text.c_str()), TextRender()->TextWidth(Font::Caption, pTitle)) + 2.0f * Space::XL);
		const float Height = Font::Caption + Font::Label + 2.0f * Space::M + Space::XS;
		CUIRect Box(pScreen->w / 2.0f - Width / 2.0f, Y - (1.0f - FadeIn) * Space::M, Width, Height);

		const ColorRGBA Fill = Themed ? Palette.m_Panel2.WithMultipliedAlpha(Alpha) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f * Alpha);
		const ColorRGBA Accent = Themed ? Palette.m_Brand.WithMultipliedAlpha(Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
		const ColorRGBA Fg = Themed ? Palette.m_Fg.WithAlpha(Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
		const ColorRGBA Muted = Themed ? Palette.m_BrandSoft.WithAlpha(Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f * Alpha);

		Graphics()->TextureClear();
		Box.Draw(Fill, IGraphics::CORNER_ALL, Radius::Card);
		CUIRect Bar;
		Box.VSplitLeft(Space::XS, &Bar, nullptr);
		Bar.HMargin(Space::S, &Bar);
		Bar.Draw(Accent, IGraphics::CORNER_ALL, Space::XS / 2.0f);

		CUIRect Content, Title, Body;
		Box.VMargin(Space::XL, &Content);
		Content.HMargin(Space::M, &Content);
		Content.HSplitTop(Font::Caption + Space::XS, &Title, &Body);
		SLabelProperties TitleProps;
		const KogTheme::CTextColor TitlePropsColor(TextRender(), Muted);
		Ui()->DoLabel(&Title, pTitle, Font::Caption, TEXTALIGN_ML, TitleProps);
		SLabelProperties BodyProps;
		const KogTheme::CTextColor BodyPropsColor(TextRender(), Fg);
		BodyProps.m_MaxWidth = Body.w;
		BodyProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Body, Toast.m_Text.c_str(), Font::Label, TEXTALIGN_ML, BodyProps);

		Y += Height + Space::S;
	}
}
