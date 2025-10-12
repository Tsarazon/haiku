/*
 * Copyright 2001-2020, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Adi Oanca, adioanca@gmail.com
 *		Stephan Aßmus, superstippi@gmx.de
 *		Axel Dörfler, axeld@pinc-software.de
 *		Brecht Machiels, brecht@mos6581.org
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Tri-Edge AI
 *		Jacob Secunda, secundja@gmail.com
 */
#ifndef WINDOW_H
#define WINDOW_H


#include "RegionPool.h"
#include "ServerWindow.h"
#include "View.h"
#include "WindowList.h"

#include <AutoDeleter.h>
#include <ObjectList.h>
#include <Referenceable.h>
#include <Region.h>
#include <String.h>


// Forward Declarations
class Window;
class ClickTarget;
class ClientLooper;
class Decorator;
class Desktop;
class DrawingEngine;
class EventDispatcher;
class Screen;
class WindowBehaviour;
class WorkspacesView;

namespace BPrivate {
	class PortLink;
};


// Constants and Types
namespace {
	// Message code for redraw requests
	const uint32 AS_REDRAW = 'rdrw';
}

typedef	BObjectList<Window>	StackWindows;


// WindowStack Class
class WindowStack : public BReferenceable {
public:
								WindowStack(::Decorator* decorator);
								~WindowStack();

			void				SetDecorator(::Decorator* decorator);
			::Decorator*		Decorator();

	const	StackWindows&		WindowList() const { return fWindowList; }
	const	StackWindows&		LayerOrder() const { return fWindowLayerOrder; }

			Window*				TopLayerWindow() const;

			int32				CountWindows();
			Window*				WindowAt(int32 index);
			bool				AddWindow(Window* window, int32 position = -1);
			bool				RemoveWindow(Window* window);

			bool				MoveToTopLayer(Window* window);
			bool				Move(int32 from, int32 to);

private:
			ObjectDeleter< ::Decorator>
								fDecorator;
			StackWindows		fWindowList;
			StackWindows		fWindowLayerOrder;
};


// Window Class
class Window {
public:
	// Construction and Destruction
								Window(const BRect& frame, const char *name,
									window_look look, window_feel feel,
									uint32 flags, uint32 workspaces,
									::ServerWindow* window,
									DrawingEngine* drawingEngine);
	virtual						~Window();

	// Initialization and Properties
			status_t			InitCheck() const;

			BRect				Frame() const { return fFrame; }
			const char*			Title() const { return fTitle.String(); }

	virtual	bool				IsOffscreenWindow() const { return false; }

	// Window Anchoring
			window_anchor&		Anchor(int32 index);
			Window*				NextWindow(int32 index) const;
			Window*				PreviousWindow(int32 index) const;

	// Screen and Desktop Management
			::Desktop*			Desktop() const { return fDesktop; }
			::Decorator*		Decorator() const;
			::ServerWindow*		ServerWindow() const { return fWindow; }
			::EventTarget&		EventTarget() const
									{ return fWindow->EventTarget(); }

			bool				ReloadDecor();

			void				SetScreen(const ::Screen* screen);
			const ::Screen*		Screen() const;

	// Clipping and Regions
			// IMPORTANT: The following clipping methods require proper locking:
			// - For reading: Desktop must hold ReadLock on clipping
			// - For writing: Desktop must hold WriteLock on clipping
			// Current implementation does not enforce this, but callers must respect it
			void				SetClipping(BRegion* stillAvailableOnScreen);

	inline	BRegion&			VisibleRegion() { return fVisibleRegion; }
			BRegion&			VisibleContentRegion();

			void				GetFullRegion(BRegion* region);
			void				GetBorderRegion(BRegion* region);
			void				GetContentRegion(BRegion* region);

			void				GetEffectiveDrawingRegion(View* view,
									BRegion& region);
			bool				DrawingRegionChanged(View* view) const;

	// Layout and Positioning
			void				MoveBy(int32 x, int32 y, bool moveStack = true);
			void				ResizeBy(int32 x, int32 y,
									BRegion* dirtyRegion,
									bool resizeStack = true);
			void				SetOutlinesDelta(BPoint delta,
									BRegion* dirtyRegion);

			void				ScrollViewBy(View* view, int32 dx, int32 dy);

	// View Management
			void				SetTopView(View* topView);
			View*				TopView() const { return fTopView.Get(); }
			View*				ViewAt(const BPoint& where);

	// Drawing and Updates
			void				ProcessDirtyRegion(const BRegion& dirtyRegion,
									const BRegion& exposeRegion);
			void				ProcessDirtyRegion(const BRegion& exposeRegion)
									{ ProcessDirtyRegion(exposeRegion, exposeRegion); }
			void				RedrawDirtyRegion();

			void				MarkDirty(BRegion& regionOnScreen);
			void				MarkContentDirty(BRegion& dirtyRegion,
									BRegion& exposeRegion);
			void				MarkContentDirtyAsync(BRegion& dirtyRegion);
			void				InvalidateView(View* view, BRegion& viewRegion);

			void				DisableUpdateRequests();
			void				EnableUpdateRequests();

			void				BeginUpdate(BPrivate::PortLink& link);
			void				EndUpdate();
			bool				InUpdate() const { return fInUpdate; }
			bool				NeedsUpdate() const { return fUpdateRequested; }

			DrawingEngine*		GetDrawingEngine() const
									{ return fDrawingEngine.Get(); }

			void				CopyContents(BRegion* region,
									int32 xOffset, int32 yOffset);

	// Region Pool Management
			::RegionPool*		RegionPool() { return &fRegionPool; }
	inline	BRegion*			GetRegion()
									{ return fRegionPool.GetRegion(); }
	inline	BRegion*			GetRegion(const BRegion& copy)
									{ return fRegionPool.GetRegion(copy); }
	inline	void				RecycleRegion(BRegion* region)
									{ fRegionPool.Recycle(region); }

	// User Interaction - Mouse Events
			void				MouseDown(BMessage* message, BPoint where,
									const ClickTarget& lastClickTarget,
									int32& clickCount,
									ClickTarget& _clickTarget);
			void				MouseUp(BMessage* message, BPoint where,
									int32* _viewToken);
			void				MouseMoved(BMessage* message, BPoint where,
									int32* _viewToken, bool isLatestMouseMoved,
									bool isFake);

	// User Interaction - Keyboard Events
			void				ModifiersChanged(int32 modifiers);

	// Window State Management
			void				SetTitle(const char* name, BRegion& dirty);

			void				SetFocus(bool focus);
			bool				IsFocus() const { return fIsFocus; }

			void				SetHidden(bool hidden);
	inline	bool				IsHidden() const { return fHidden; }

			void				SetShowLevel(int32 showLevel);
	inline	int32				ShowLevel() const { return fShowLevel; }

			void				SetMinimized(bool minimized);
	inline	bool				IsMinimized() const { return fMinimized; }

			bool				IsVisible() const;

			bool				IsDragging() const;
			bool				IsResizing() const;

	// Window Limits
			void				SetSizeLimits(int32 minWidth, int32 maxWidth,
									int32 minHeight, int32 maxHeight);
			void				GetSizeLimits(int32* minWidth, int32* maxWidth,
									int32* minHeight, int32* maxHeight) const;

	// Window Appearance
			bool				SetTabLocation(float location, bool isShifting,
									BRegion& dirty);
			float				TabLocation() const;

			bool				SetDecoratorSettings(const BMessage& settings,
													 BRegion& dirty);
			bool				GetDecoratorSettings(BMessage* settings);

			void				HighlightDecorator(bool active);

			void				FontsChanged(BRegion* updateRegion);
			void				ColorsChanged(BRegion* updateRegion);

			void				SetLook(window_look look,
									BRegion* updateRegion);
			void				SetFeel(window_feel feel);
			void				SetFlags(uint32 flags, BRegion* updateRegion);

			window_look			Look() const { return fLook; }
			window_feel			Feel() const { return fFeel; }
			uint32				Flags() const { return fFlags; }

	// Workspace Management
			void				SetCurrentWorkspace(int32 index)
									{ fCurrentWorkspace = index; fPriorWorkspace = index; }
			int32				CurrentWorkspace() const
									{ return fCurrentWorkspace; }

			void				SetPriorWorkspace(int32 index)
									{ fPriorWorkspace = index; }
			int32				PriorWorkspace() const
									{ return fPriorWorkspace; }

			uint32				Workspaces() const { return fWorkspaces; }
			void				SetWorkspaces(uint32 workspaces)
									{ fWorkspaces = workspaces; }
			bool				InWorkspace(int32 index) const;

			void				WorkspaceActivated(int32 index, bool active);
			void				WorkspacesChanged(uint32 oldWorkspaces,
									uint32 newWorkspaces);
			void				Activated(bool active);

	// Window Hierarchy and Relationships
			bool				SupportsFront();

			bool				IsModal() const;
			bool				IsFloating() const;
			bool				IsNormal() const;

			bool				HasModal() const;

			Window*				Frontmost(Window* first = NULL,
									int32 workspace = -1);
			Window*				Backmost(Window* first = NULL,
									int32 workspace = -1);

			bool				AddToSubset(Window* window);
			void				RemoveFromSubset(Window* window);
			bool				HasInSubset(const Window* window) const;
			bool				SameSubset(Window* window);
			uint32				SubsetWorkspaces() const;
			bool				InSubsetWorkspace(int32 index) const;

	// WorkspacesView Management
			bool				HasWorkspacesViews() const
									{ return fWorkspacesViewCount != 0; }
			void				AddWorkspacesView()
									{ fWorkspacesViewCount++; }
			void				RemoveWorkspacesView()
									{ fWorkspacesViewCount--; }
			void				FindWorkspacesViews(
									BObjectList<WorkspacesView>& list) const;

	// Window Stack Management
			WindowStack*		GetWindowStack();

			bool				DetachFromWindowStack(
									bool ownStackNeeded = true);
			bool				AddWindowToStack(Window* window);
			Window*				StackedWindowAt(const BPoint& where);
			Window*				TopLayerStackWindow();

			int32				PositionInStack() const;
			bool				MoveToTopStackLayer();
			bool				MoveToStackPosition(int32 index,
									bool isMoving);

	// Static Utility Methods
	static	bool				IsValidLook(window_look look);
	static	bool				IsValidFeel(window_feel feel);
	static	bool				IsModalFeel(window_feel feel);
	static	bool				IsFloatingFeel(window_feel feel);

	static	uint32				ValidWindowFlags();
	static	uint32				ValidWindowFlags(window_feel feel);

protected:
	// Protected Helper Methods
			void				_ShiftPartOfRegion(BRegion* region,
									BRegion* regionToShift, int32 xOffset,
									int32 yOffset);

			void				_TriggerContentRedraw(BRegion& dirty,
									const BRegion& expose = BRegion());
			void				_DrawBorder();

			void				_TransferToUpdateSession(
									BRegion* contentDirtyRegion);
			void				_SendUpdateMessage();

			void				_UpdateContentRegion();

			void				_ObeySizeLimits();
			void				_PropagatePosition();

private:
	// UpdateSession Nested Class
	class UpdateSession {
	public:
									UpdateSession();

				void				Include(BRegion* additionalDirty);
				void				Exclude(BRegion* dirtyInNextSession);

		inline	BRegion&			DirtyRegion()
										{ return fDirtyRegion; }

				void				MoveBy(int32 x, int32 y);

				void				SetUsed(bool used);
		inline	bool				IsUsed() const
										{ return fInUse; }

	private:
				BRegion				fDirtyRegion;
				bool				fInUse;
	};

	// Private Helper Methods
			WindowStack*		_InitWindowStack();

	// Member Variables - Basic Properties
			BString				fTitle;
			// NOTE: Using BRect (floating point) for historical reasons
			// Ideally should use integer rects throughout the system
			// for pixel-perfect positioning and to avoid rounding issues
			BRect				fFrame;
			const ::Screen*		fScreen;

			window_anchor		fAnchor[kListCount];

	// Member Variables - Regions
			BRegion				fVisibleRegion;
			BRegion				fVisibleContentRegion;
			BRegion				fDirtyRegion;
			BRegion				fExposeRegion;
			BRegion				fContentRegion;
			BRegion				fEffectiveDrawingRegion;

			bool				fVisibleContentRegionValid : 1;
			bool				fContentRegionValid : 1;
			bool				fEffectiveDrawingRegionValid : 1;

			::RegionPool		fRegionPool;

	// Member Variables - Window Relationships
			BObjectList<Window> fSubsets;

	// Member Variables - Core Objects
			ObjectDeleter<WindowBehaviour>
								fWindowBehaviour;
			ObjectDeleter<View>	fTopView;
			::ServerWindow*		fWindow;
			ObjectDeleter<DrawingEngine>
								fDrawingEngine;
			::Desktop*			fDesktop;

	// Member Variables - Update Management
			UpdateSession		fUpdateSessions[2];
			UpdateSession*		fCurrentUpdateSession;
			UpdateSession*		fPendingUpdateSession;

			bool				fUpdateRequested : 1;
			bool				fInUpdate : 1;
			bool				fUpdatesEnabled : 1;

	// Member Variables - Window State
			bool				fHidden : 1;
			int32				fShowLevel;
			bool				fMinimized : 1;
			bool				fIsFocus : 1;

	// Member Variables - Window Appearance
			window_look			fLook;
			window_feel			fFeel;
			uint32				fOriginalFlags;
			uint32				fFlags;

	// Member Variables - Workspace
			uint32				fWorkspaces;
			int32				fCurrentWorkspace;
			int32				fPriorWorkspace;

	// Member Variables - Size Limits
			int32				fMinWidth;
			int32				fMaxWidth;
			int32				fMinHeight;
			int32				fMaxHeight;

	// Member Variables - WorkspacesView
			int32				fWorkspacesViewCount;

	// Member Variables - Window Stack
			BReference<WindowStack>		fCurrentStack;

	friend class DecorManager;
};


#endif // WINDOW_H
