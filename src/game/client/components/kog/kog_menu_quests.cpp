#include <engine/shared/kog_quests.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/kog_theme.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

using namespace KogStyle;

// The quests of this map, from the map file (see engine/shared/kog_quests.h). The server decides what is completed.
void CMenus::RenderKogQuests(CUIRect View)
{
	const KogQuests::CList &List = GameClient()->m_KogQuests;
	const ColorRGBA Muted = KogTheme::On() ? KogTheme::Palette().m_Muted : ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f);
	const ColorRGBA Accent = KogTheme::On() ? KogTheme::Palette().m_BrandSoft : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

	View.Draw(KogTheme::Neutral(0.25f), IGraphics::CORNER_ALL, Radius::Card);
	View.Margin(Space::M, &View);

	CUIRect Header;
	View.HSplitTop(2.0f * Font::Label, &Header, &View);
	View.HSplitTop(Space::XS, nullptr, &View);
	Ui()->DoLabel(&Header, Localize("Quests"), 2.0f * Font::Label, TEXTALIGN_ML);

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 3.0f * Size::Row;
	s_ScrollRegion.Begin(&View, &ScrollParams);

	for(const KogQuests::SQuest &Quest : List.Quests())
	{
		CUIRect Item;
		View.HSplitTop(Font::Label + Font::Caption + 2.0f * Space::XS + Space::S, &Item, &View);
		s_ScrollRegion.AddRect(Item);
		if(s_ScrollRegion.RectClipped(Item))
			continue;

		CUIRect Title, Description, Scope;
		Item.HSplitTop(Font::Label + Space::XS, &Title, &Item);
		Item.HSplitTop(Font::Caption + Space::XS, &Description, nullptr);

		Title.VSplitRight(Font::Caption * 5.0f, &Title, &Scope);
		Ui()->DoLabel(&Title, Quest.m_Name.c_str(), Font::Label, TEXTALIGN_ML);

		// the scope is always a word, never only a colour
		SLabelProperties ScopeProps;
		ScopeProps.SetColor(Accent);
		Ui()->DoLabel(&Scope, Quest.m_Team ? Localize("Team", "Quest") : Localize("Solo", "Quest"), Font::Caption, TEXTALIGN_MR, ScopeProps);

		SLabelProperties DescriptionProps;
		DescriptionProps.SetColor(Muted);
		DescriptionProps.m_MaxWidth = Description.w;
		DescriptionProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Description, Quest.m_Description.c_str(), Font::Caption, TEXTALIGN_ML, DescriptionProps);
	}

	s_ScrollRegion.End();
}
