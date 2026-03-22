// SPDX-License-Identifier: OpenTracker
// Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
// See LICENSE for full terms.

#ifndef TEAM_MENU_ITEM_H
#define TEAM_MENU_ITEM_H


//	Individual team/application listing
//	item for TeamMenu in mini/vertical mode
//	item for ExpandoMenuBar in vertical or horizontal expanded mode


#include "TruncatableMenuItem.h"


const float kSwitchWidth = 12.0f;

const uint32 kMinimizeTeam = 'mntm';
const uint32 kBringTeamToFront = 'bftm';


class BBitmap;
class TBarView;
class TWindowMenuItem;

class TTeamMenuItem : public TTruncatableMenuItem {
public:
								TTeamMenuItem(BList* team, BBitmap* icon,
									char* name, char* signature,
									float width = -1.0f, float height = -1.0f);
								TTeamMenuItem(float width = -1.0f,
									float height = -1.0f);
	virtual						~TTeamMenuItem();

			bool				HandleMouseDown(BPoint where);
			status_t			Invoke(BMessage* message = NULL);

			void				SetOverrideWidth(float width)
									{ fOverrideWidth = width; };
			void				SetOverrideHeight(float height)
									{ fOverrideHeight = height; };
			void				SetOverrideSelected(bool selected);

			int32				ArrowDirection() const
									{ return fArrowDirection; };
			void				SetArrowDirection(int32 direction)
									{ fArrowDirection = direction; };

			BBitmap*			Icon() const { return fIcon; };
			void				SetIcon(BBitmap* icon);

			bool				IsExpanded() const { return fExpanded; };
			void				ToggleExpandState(bool resizeWindow);

			BRect				ExpanderBounds() const;
			TWindowMenuItem*	ExpandedWindowItem(int32 id);

			float				LabelWidth() const { return fLabelWidth; };
			BList*				Teams() const { return fTeam; };
			const char*			Signature() const { return fSignature; };

			bool				IsFocused() const { return fIsFocused; };
			void				SetFocused(bool focused);

protected:
			void				GetContentSize(float* width, float* height);
			void				Draw();
			void				DrawContent();
			void				DrawExpanderArrow();

private:
			void				_Init(BList* team, BBitmap* icon,
									char* name, char* signature,
									float width = -1.0f, float height = -1.0f);

			bool				_IsSelected() const;

	// mode-specific background drawing
			void				_DrawBackgroundTaskbar(BMenu* menu,
									BRect frame, rgb_color menuColor,
									bool canHandle);
			void				_DrawBackgroundVertical(BMenu* menu,
									BRect frame, rgb_color menuColor,
									bool canHandle);
			void				_DrawBackgroundHorizontal(BMenu* menu,
									BRect frame, rgb_color menuColor,
									bool canHandle);

	// mode-specific content drawing
			void				_DrawContentTaskbar(BMenu* menu, BRect frame);
			void				_DrawContentWithIcon(BMenu* menu, BRect frame);

private:
			BList*				fTeam;
			BBitmap*			fIcon;
			char*				fSignature;

			float				fOverrideWidth;
			float				fOverrideHeight;

			TBarView*			fBarView;
			float				fLabelWidth;
			float				fLabelAscent;
			float				fLabelDescent;
			float				fLabelHeight;

			bool				fOverriddenSelected;
			bool				fIsFocused;

			bool				fExpanded;
			int32				fArrowDirection;
};


#endif	// TEAM_MENU_ITEM_H
