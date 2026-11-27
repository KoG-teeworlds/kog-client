/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);

private:
	bool CheckHotKey(int Key) const;
	void RenderClassicStartMenu(CUIRect MainView, int &NewPage);
	void RenderStartFooter(CUIRect MainView, int NewPage);
	// The KoG start page, in components/kog/kog_menu_start.cpp
	void RenderKogStartMenu(CUIRect MainView, int &NewPage);
	void DrawKogScrim(const CUIRect &Screen);
	bool DoKogButton(const void *pId, float *pHover, const CUIRect &Rect, const char *pIcon, const char *pText, int Kind, const ColorRGBA &Dot);
};

#endif
