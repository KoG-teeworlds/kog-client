#include "kog_notices.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/kog_notice_wire.h>
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

static const char *Headline(KogNotices::EKind Kind)
{
	switch(Kind)
	{
	case KogNotices::EKind::ACHIEVEMENT: return Localize("Achievement unlocked");
	case KogNotices::EKind::QUEST: return Localize("Quest completed");
	case KogNotices::EKind::QUEST_PROGRESS: return Localize("Quest progress");
	case KogNotices::EKind::MAINTENANCE: return Localize("Maintenance");
	case KogNotices::EKind::WARNING: return Localize("Warning");
	case KogNotices::EKind::TOURNAMENT: return Localize("Tournament");
	default: return Localize("Notice");
	}
}

void CKogNotices::OnStateChange(int NewState, int OldState)
{
	m_Toasts.Clear();
}

void CKogNotices::Add(KogNotices::SToast Toast)
{
	m_Toasts.Show(std::move(Toast), Client()->GlobalTime());
}

void CKogNotices::OnMessage(int Msg, void *pRawMsg)
{
	// a server that agreed to notices sends the typed message and no chat line
	if(Msg != NETMSGTYPE_SV_CHAT || Client()->State() != IClient::STATE_ONLINE || GameClient()->m_KogHello.Has(EKogCap::NOTICES))
		return;
	const CNetMsg_Sv_Chat *pMsg = static_cast<const CNetMsg_Sv_Chat *>(pRawMsg);
	if(pMsg->m_ClientId != SERVER_CLIENT_ID || pMsg->m_pMessage == nullptr)
		return;
	const char *pBody = nullptr;
	const KogNotices::EKind Kind = KogNotices::Classify(pMsg->m_pMessage, &pBody);
	if(Kind == KogNotices::EKind::NONE || !CKogLogin::ServerIsKog(ServerBrowser(), Client()->ServerAddress()))
		return;
	KogNotices::SToast Toast;
	Toast.m_Kind = Kind;
	Toast.m_Text.assign(pBody, std::min<size_t>(str_length(pBody), KogNotices::MAX_TEXT_BYTES));
	Add(std::move(Toast));
}

void CKogNotices::OnNotice(CUnpacker *pUnpacker)
{
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_KogHello.Has(EKogCap::NOTICES))
		return;
	KogNoticeWire::SNotice Notice;
	if(!KogNoticeWire::Unpack(pUnpacker, &Notice))
		return;

	// the chat log keeps every notice, also the ones that get no toast
	GameClient()->m_Chat.AddLine(SERVER_CLIENT_ID, 0, Notice.m_aText);

	KogNotices::SToast Toast;
	Toast.m_Kind = Notice.m_Kind;
	Toast.m_Key = Notice.m_aKey;
	Toast.m_Text = Notice.m_aText;
	Toast.m_Value = Notice.m_Value;
	Toast.m_Limit = Notice.m_Limit;
	// the headline says what it is, so the fixed words of the text are not repeated
	const char *pBody = nullptr;
	const KogNotices::EKind Words = KogNotices::Classify(Notice.m_aText, &pBody);
	if(Words == Notice.m_Kind)
		Toast.m_Text = pBody;
	Add(std::move(Toast));
}

void CKogNotices::OnRender()
{
	if(m_Toasts.Empty() || Client()->State() != IClient::STATE_ONLINE)
		return;
	const float Now = Client()->GlobalTime();
	m_Toasts.Expire(Now);

	Ui()->MapScreen();
	const CUIRect *pScreen = Ui()->Screen();
	const bool Themed = KogTheme::On();
	const SPalette &Palette = KogTheme::Palette();

	float Y = Space::XXL * 2.0f;
	for(const KogNotices::SToast &Toast : m_Toasts.All())
	{
		const float Age = Now - Toast.m_Start;
		const float FadeIn = std::min(1.0f, Age / Motion::Panel);
		const float FadeOut = std::min(1.0f, (Toast.m_Duration - Age) / Motion::Panel);
		const float Alpha = std::min(FadeIn, FadeOut);

		char aTitle[96];
		str_copy(aTitle, Headline(Toast.m_Kind));
		if(Toast.HasCountdown())
		{
			const int Left = Toast.SecondsLeft(Now);
			str_format(aTitle, sizeof(aTitle), "%s  %d:%02d", Headline(Toast.m_Kind), Left / 60, Left % 60);
		}
		const char *pTitle = aTitle;
		const float Width = std::min(pScreen->w - 2.0f * Space::L, std::max(TextRender()->TextWidth(Font::Label, Toast.m_Text.c_str()), TextRender()->TextWidth(Font::Caption, pTitle)) + 2.0f * Space::XL);
		const float ProgressHeight = Toast.HasProgress() ? Space::S : 0.0f;
		const float Height = Font::Caption + Font::Label + 2.0f * Space::M + Space::XS + ProgressHeight;
		CUIRect Box(pScreen->w / 2.0f - Width / 2.0f, Y - (1.0f - FadeIn) * Space::M, Width, Height);

		const ColorRGBA Fill = Themed ? Palette.m_Panel2.WithMultipliedAlpha(Alpha) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f * Alpha);
		const bool Alert = Toast.m_Kind == KogNotices::EKind::MAINTENANCE || Toast.m_Kind == KogNotices::EKind::WARNING;
		const ColorRGBA Accent = Themed ? (Alert ? Palette.m_Warn : Palette.m_Brand).WithMultipliedAlpha(Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
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
		CUIRect Progress;
		if(Toast.HasProgress())
			Body.HSplitBottom(ProgressHeight, &Body, &Progress);
		SLabelProperties TitleProps;
		const KogTheme::CTextColor TitlePropsColor(TextRender(), Muted);
		Ui()->DoLabel(&Title, pTitle, Font::Caption, TEXTALIGN_ML, TitleProps);
		SLabelProperties BodyProps;
		const KogTheme::CTextColor BodyPropsColor(TextRender(), Fg);
		BodyProps.m_MaxWidth = Body.w;
		BodyProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Body, Toast.m_Text.c_str(), Font::Label, TEXTALIGN_ML, BodyProps);
		if(Toast.HasProgress())
		{
			Progress.HMargin(Space::XS, &Progress);
			Progress.Draw(Muted.WithMultipliedAlpha(0.3f), IGraphics::CORNER_ALL, Progress.h / 2.0f);
			CUIRect Done = Progress;
			Done.w *= std::clamp((float)Toast.m_Value / (float)Toast.m_Limit, 0.0f, 1.0f);
			Done.Draw(Accent, IGraphics::CORNER_ALL, Progress.h / 2.0f);
		}

		Y += Height + Space::S;
	}
}
