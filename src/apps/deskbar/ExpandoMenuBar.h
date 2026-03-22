// SPDX-License-Identifier: OpenTracker
// Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
// See LICENSE for full terms.

#ifndef EXPANDO_MENU_BAR_H
#define EXPANDO_MENU_BAR_H


// application list
// top level at window
// in expanded mode horizontal and vertical


#include <MenuBar.h>
#include <Locker.h>


const float kSepItemWidth = 5.0f;


enum drag_and_drop_selection {
	kNoSelection,
	kDeskbarMenuSelection,
	kAppMenuSelection,
	kAnyMenuSelection
};


class BBitmap;
class TBarView;
class TBarMenuTitle;
class TTeamMenuItem;


class TExpandoMenuBar : public BMenuBar {
public:
							TExpandoMenuBar(menu_layout layout,
								TBarView* barView = NULL);

	virtual	void			AllAttached();
	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();

	virtual	void			Draw(BRect update);
	virtual	void			DrawBackground(BRect update);

	virtual	void			MessageReceived(BMessage* message);

	virtual	void			MouseDown(BPoint where);
	virtual	void			MouseMoved(BPoint where, uint32 code,
								const BMessage* message);
	virtual	void			MouseUp(BPoint where);

			void			BuildItems();

			BMenuItem*		ItemAtPoint(BPoint point);
			TTeamMenuItem*	TeamItemAtPoint(BPoint location,
								BMenuItem** _item = NULL);
			bool			InDeskbarMenu(BPoint) const;

			void			CheckItemSizes(int32 delta, bool reset = false);

			float			MinHorizontalItemWidth();
			float			MaxHorizontalItemWidth();

			menu_layout		MenuLayout() const;
			void			SetMenuLayout(menu_layout layout);

			void			SizeWindow(int32 delta);
			bool			CheckForSizeOverrun();

			void			StartMonitoringWindows();
			void			StopMonitoringWindows();

private:
	static	int				CompareByName(const void* first,
								const void* second);
	static	int32			monitor_team_windows(void* arg);

			void			AddTeam(BList* team, BBitmap* icon, char* name,
								char* signature);
			void			AddTeam(team_id team, const char* signature);
			void			RemoveTeam(team_id team, bool partial);

			void			_FinishedDrag(bool invoke = false);

			void			_CheckItemSizesTaskbar();
			void			_CheckItemSizesHorizontal(int32 delta, bool reset);

			bool			CheckForSizeOverrunVertical();
			bool			CheckForSizeOverrunHorizontal();

			float			MaxHorizontalWidth();

			void			_UpdateFocusedItem(team_id team);

			bool			Vertical() const
								{ return MenuLayout() == B_ITEMS_IN_COLUMN; };
private:
			TBarView*		fBarView;
			bool			fOverflow : 1;
			bool			fUnderflow : 1;
			bool			fFirstBuild : 1;

			TTeamMenuItem*	fPreviousDragTargetItem;
			BMenuItem*		fLastMousedOverItem;
			BMenuItem*		fLastClickedItem;
			bigtime_t		fLastClickTime;
			BList			fTeamList;

			TTeamMenuItem*	fFocusedItem;

	static	bool			sDoMonitor;
	static	thread_id		sMonThread;
	static	BLocker			sMonLocker;
};


#endif // EXPANDO_MENU_BAR_H
