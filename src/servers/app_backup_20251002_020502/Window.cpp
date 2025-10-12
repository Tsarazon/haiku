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


#include "Window.h"

#include <new>
#include <stdio.h>

#include <Debug.h>

#include <DirectWindow.h>
#include <PortLink.h>
#include <View.h>
#include <ViewPrivate.h>
#include <WindowPrivate.h>

#include "ClickTarget.h"
#include "Decorator.h"
#include "DecorManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "MessagePrivate.h"
#include "PortLink.h"
#include "ServerApp.h"
#include "ServerWindow.h"
#include "WindowBehaviour.h"
#include "Workspace.h"
#include "WorkspacesView.h"


// Debug Configuration
//#define DEBUG_WINDOW
#ifdef DEBUG_WINDOW
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

using std::nothrow;


// WindowStack Implementation

WindowStack::WindowStack(::Decorator* decorator)
	:
	fDecorator(decorator)
{
}


WindowStack::~WindowStack()
{
}


void
WindowStack::SetDecorator(::Decorator* decorator)
{
	fDecorator.SetTo(decorator);
}


::Decorator*
WindowStack::Decorator()
{
	return fDecorator.Get();
}


Window*
WindowStack::TopLayerWindow() const
{
	return fWindowLayerOrder.ItemAt(fWindowLayerOrder.CountItems() - 1);
}


int32
WindowStack::CountWindows()
{
	return fWindowList.CountItems();
}


Window*
WindowStack::WindowAt(int32 index)
{
	return fWindowList.ItemAt(index);
}


bool
WindowStack::AddWindow(Window* window, int32 position)
{
	if (position >= 0) {
		if (fWindowList.AddItem(window, position) == false)
			return false;
	} else if (fWindowList.AddItem(window) == false)
		return false;

	if (fWindowLayerOrder.AddItem(window) == false) {
		fWindowList.RemoveItem(window);
		return false;
	}
	return true;
}


bool
WindowStack::RemoveWindow(Window* window)
{
	if (fWindowList.RemoveItem(window) == false)
		return false;

	fWindowLayerOrder.RemoveItem(window);
	return true;
}


bool
WindowStack::MoveToTopLayer(Window* window)
{
	int32 index = fWindowLayerOrder.IndexOf(window);
	return fWindowLayerOrder.MoveItem(index,
		fWindowLayerOrder.CountItems() - 1);
}


bool
WindowStack::Move(int32 from, int32 to)
{
	return fWindowList.MoveItem(from, to);
}


// Window Implementation - Construction and Destruction

Window::Window(const BRect& frame, const char *name,
		window_look look, window_feel feel, uint32 flags, uint32 workspaces,
		::ServerWindow* window, DrawingEngine* drawingEngine)
	:
	fTitle(name),
	fFrame(frame),
	fScreen(NULL),

	fVisibleRegion(),
	fVisibleContentRegion(),
	fDirtyRegion(),

	fContentRegion(),
	fEffectiveDrawingRegion(),

	fVisibleContentRegionValid(false),
	fContentRegionValid(false),
	fEffectiveDrawingRegionValid(false),

	fRegionPool(),

	fWindow(window),
	fDrawingEngine(drawingEngine),
	fDesktop(window->Desktop()),

	fCurrentUpdateSession(&fUpdateSessions[0]),
	fPendingUpdateSession(&fUpdateSessions[1]),
	fUpdateRequested(false),
	fInUpdate(false),
	fUpdatesEnabled(false),

	fHidden(true),
	fShowLevel(1),
	fMinimized(false),
	fIsFocus(false),

	fLook(look),
	fFeel(feel),
	fWorkspaces(workspaces),
	fCurrentWorkspace(-1),
	fPriorWorkspace(-1),

	fMinWidth(1),
	fMaxWidth(32768),
	fMinHeight(1),
	fMaxHeight(32768),

	fWorkspacesViewCount(0)
{
	_InitWindowStack();

	if (!IsValidLook(fLook))
		fLook = B_TITLED_WINDOW_LOOK;
	if (!IsValidFeel(fFeel))
		fFeel = B_NORMAL_WINDOW_FEEL;

	SetFlags(flags, NULL);

	if (fLook != B_NO_BORDER_WINDOW_LOOK && fCurrentStack.IsSet()) {
		::Decorator* decorator = Decorator();
		if (decorator != NULL) {
			decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth,
				&fMaxHeight);
		}
	}
	if (fFeel != kOffscreenWindowFeel)
		fWindowBehaviour.SetTo(gDecorManager.AllocateWindowBehaviour(this));

	if (feel == kDesktopWindowFeel) {
		// Special handling for desktop window - spans entire screen
		// NOTE: This logic ideally should be in Desktop class or Workspace,
		// not in Window constructor. The desktop window should be sized
		// automatically when workspace changes, not just during construction.
		// See: Desktop::SetWorkspace() and Workspace::SetScreen()
		uint16 width, height;
		uint32 colorSpace;
		float frequency;
		if (Screen() != NULL) {
			Screen()->GetMode(width, height, colorSpace, frequency);
			fFrame.OffsetTo(B_ORIGIN);
			// NOTE: ResizeBy() cannot be called from constructor as it invokes
			// virtual methods and TopView is not yet set. Desktop should handle
			// initial sizing after construction is complete.
			// Desired: ResizeBy(width - fFrame.Width(), height - fFrame.Height(), NULL);
		}
	}

	STRACE(("Window %p, %s:\n", this, Name()));
	STRACE(("\tFrame: (%.1f, %.1f, %.1f, %.1f)\n", fFrame.left, fFrame.top,
		fFrame.right, fFrame.bottom));
	STRACE(("\tWindow %s\n", window ? window->Title() : "NULL"));
}


Window::~Window()
{
	if (fTopView.IsSet()) {
		fTopView->DetachedFromWindow();
	}

	DetachFromWindowStack(false);

	gDecorManager.CleanupForWindow(this);
}


// Window Implementation - Initialization and Properties

status_t
Window::InitCheck() const
{
	if (GetDrawingEngine() == NULL)
		return B_NO_MEMORY;
	
	if (fFeel != kOffscreenWindowFeel && !fWindowBehaviour.IsSet())
		return B_NO_MEMORY;
	
	if (fWindow == NULL)
		return B_BAD_VALUE;
	
	if (fDesktop == NULL)
		return B_BAD_VALUE;
	
	if (!fCurrentStack.IsSet())
		return B_NO_MEMORY;
	
	return B_OK;
}


window_anchor&
Window::Anchor(int32 index)
{
	return fAnchor[index];
}


Window*
Window::NextWindow(int32 index) const
{
	return fAnchor[index].next;
}


Window*
Window::PreviousWindow(int32 index) const
{
	return fAnchor[index].previous;
}


// Window Implementation - Screen and Desktop Management

::Decorator*
Window::Decorator() const
{
	if (!fCurrentStack.IsSet())
		return NULL;
	return fCurrentStack->Decorator();
}


bool
Window::ReloadDecor()
{
	::Decorator* decorator = NULL;
	WindowBehaviour* windowBehaviour = NULL;
	WindowStack* stack = GetWindowStack();
	if (stack == NULL)
		return false;

	if (stack->WindowAt(0) != this)
		return true;

	if (fLook != B_NO_BORDER_WINDOW_LOOK) {
		decorator = gDecorManager.AllocateDecorator(this);
		if (decorator == NULL)
			return false;

		for (int32 i = 1; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowAt(i);
			BRegion dirty;
			DesktopSettings settings(fDesktop);
			if (decorator->AddTab(settings, window->Title(), window->Look(),
				window->Flags(), -1, &dirty) == NULL) {
				delete decorator;
				return false;
			}
		}
	} else
		return true;

	windowBehaviour = gDecorManager.AllocateWindowBehaviour(this);
	if (windowBehaviour == NULL) {
		delete decorator;
		return false;
	}

	stack->SetDecorator(decorator);
	fWindowBehaviour.SetTo(windowBehaviour);

	for (int32 i = 0; i < stack->CountWindows(); i++) {
		Window* window = stack->WindowAt(i);
		if (window->IsFocus())
			decorator->SetFocus(i, true);
		if (window == stack->TopLayerWindow())
			decorator->SetTopTab(i);
	}

	return true;
}


void
Window::SetScreen(const ::Screen* screen)
{
	// NOTE: Ideally should ASSERT_MULTI_WRITE_LOCKED(fDesktop->ScreenLocker())
	// but this assertion currently fails in Desktop::ShowWindow()
	// TODO: Fix Desktop::ShowWindow() to acquire proper lock before calling this
	fScreen = screen;
}


const ::Screen*
Window::Screen() const
{
	// NOTE: Ideally should ASSERT_MULTI_READ_LOCKED(fDesktop->ScreenLocker())
	// but this assertion currently fails in various places
	// TODO: Review all callers and ensure proper locking
	return fScreen;
}


// Window Implementation - Clipping and Regions

void
Window::SetClipping(BRegion* stillAvailableOnScreen)
{
	GetFullRegion(&fVisibleRegion);
	fVisibleRegion.IntersectWith(stillAvailableOnScreen);

	fVisibleContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;
}


void
Window::GetFullRegion(BRegion* region)
{
	// NOTE: Caller must hold Desktop clipping ReadLock
	// This method is typically called from within Window with the lock held
	
	// Start from the decorator border, extend to use the frame
	GetBorderRegion(region);
	region->Include(fFrame);
}


void
Window::GetBorderRegion(BRegion* region)
{
	// NOTE: Caller must hold Desktop clipping ReadLock
	// This method is typically called from within Window with the lock held

	::Decorator* decorator = Decorator();
	if (decorator)
		*region = decorator->GetFootprint();
	else
		region->MakeEmpty();
}


void
Window::GetContentRegion(BRegion* region)
{
	// NOTE: Caller must hold Desktop clipping ReadLock
	// This method is typically called from within Window with the lock held

	if (!fContentRegionValid) {
		_UpdateContentRegion();
	}

	*region = fContentRegion;
}


BRegion&
Window::VisibleContentRegion()
{
	// NOTE: Caller must hold Desktop clipping ReadLock
	// This method is typically called from within Window with the lock held
	// Regions are expected to be locked when accessed

	if (!fVisibleContentRegionValid) {
		GetContentRegion(&fVisibleContentRegion);
		fVisibleContentRegion.IntersectWith(&fVisibleRegion);
	}
	return fVisibleContentRegion;
}


void
Window::GetEffectiveDrawingRegion(View* view, BRegion& region)
{
	if (!fEffectiveDrawingRegionValid) {
		fEffectiveDrawingRegion = VisibleContentRegion();
		if (fUpdateRequested && !fInUpdate) {
			fEffectiveDrawingRegion.Exclude(
				&fPendingUpdateSession->DirtyRegion());
		} else if (fInUpdate) {
			fEffectiveDrawingRegion.IntersectWith(
				&fCurrentUpdateSession->DirtyRegion());
		}

		fEffectiveDrawingRegionValid = true;
	}

	region = fEffectiveDrawingRegion;
	if (!fContentRegionValid)
		_UpdateContentRegion();

	region.IntersectWith(&view->ScreenAndUserClipping(&fContentRegion));
}


bool
Window::DrawingRegionChanged(View* view) const
{
	return !fEffectiveDrawingRegionValid || !view->IsScreenClippingValid();
}


// Window Implementation - Layout and Positioning

void
Window::_PropagatePosition()
{
	if ((fFlags & B_SAME_POSITION_IN_ALL_WORKSPACES) == 0)
		return;

	for (int32 i = 0; i < kListCount; i++) {
		Anchor(i).position = fFrame.LeftTop();
	}
}


void
Window::MoveBy(int32 x, int32 y, bool moveStack)
{
	if (x == 0 && y == 0)
		return;

	fFrame.OffsetBy(x, y);
	_PropagatePosition();

	fDirtyRegion.OffsetBy(x, y);
	fExposeRegion.OffsetBy(x, y);

	if (fContentRegionValid)
		fContentRegion.OffsetBy(x, y);

	if (fCurrentUpdateSession->IsUsed())
		fCurrentUpdateSession->MoveBy(x, y);
	if (fPendingUpdateSession->IsUsed())
		fPendingUpdateSession->MoveBy(x, y);

	fEffectiveDrawingRegionValid = false;

	if (fTopView.IsSet()) {
		fTopView->MoveBy(x, y, NULL);
		fTopView->UpdateOverlay();
	}

	::Decorator* decorator = Decorator();
	if (moveStack && decorator)
		decorator->MoveBy(x, y);

	WindowStack* stack = GetWindowStack();
	if (moveStack && stack) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);
			if (window == this)
				continue;
			window->MoveBy(x, y, false);
		}
	}

	BMessage msg(B_WINDOW_MOVED);
	msg.AddInt64("when", system_time());
	msg.AddPoint("where", fFrame.LeftTop());
	fWindow->SendMessageToClient(&msg);
}


void
Window::ResizeBy(int32 x, int32 y, BRegion* dirtyRegion, bool resizeStack)
{
	int32 wantWidth = fFrame.IntegerWidth() + x;
	int32 wantHeight = fFrame.IntegerHeight() + y;

	WindowStack* stack = GetWindowStack();
	if (resizeStack && stack) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);

			if (wantWidth < window->fMinWidth)
				wantWidth = window->fMinWidth;
			if (wantWidth > window->fMaxWidth)
				wantWidth = window->fMaxWidth;

			if (wantHeight < window->fMinHeight)
				wantHeight = window->fMinHeight;
			if (wantHeight > window->fMaxHeight)
				wantHeight = window->fMaxHeight;
		}
	}

	x = wantWidth - fFrame.IntegerWidth();
	y = wantHeight - fFrame.IntegerHeight();

	if (x == 0 && y == 0)
		return;

	fFrame.right += x;
	fFrame.bottom += y;

	fContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;

	if (fTopView.IsSet()) {
		fTopView->ResizeBy(x, y, dirtyRegion);
		fTopView->UpdateOverlay();
	}

	::Decorator* decorator = Decorator();
	if (decorator && resizeStack)
		decorator->ResizeBy(x, y, dirtyRegion);

	if (resizeStack && stack) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);
			if (window == this)
				continue;
			window->ResizeBy(x, y, dirtyRegion, false);
		}
	}

	BRect frame(Frame());
	BMessage msg(B_WINDOW_RESIZED);
	msg.AddInt64("when", system_time());
	msg.AddInt32("width", frame.IntegerWidth());
	msg.AddInt32("height", frame.IntegerHeight());
	fWindow->SendMessageToClient(&msg);
}


void
Window::SetOutlinesDelta(BPoint delta, BRegion* dirtyRegion)
{
	float wantWidth = fFrame.IntegerWidth() + delta.x;
	float wantHeight = fFrame.IntegerHeight() + delta.y;

	WindowStack* stack = GetWindowStack();
	if (stack != NULL) {
		for (int32 i = 0; i < stack->CountWindows(); i++) {
			Window* window = stack->WindowList().ItemAt(i);

			if (wantWidth < window->fMinWidth)
				wantWidth = window->fMinWidth;
			if (wantWidth > window->fMaxWidth)
				wantWidth = window->fMaxWidth;

			if (wantHeight < window->fMinHeight)
				wantHeight = window->fMinHeight;
			if (wantHeight > window->fMaxHeight)
				wantHeight = window->fMaxHeight;
		}

		delta.x = wantWidth - fFrame.IntegerWidth();
		delta.y = wantHeight - fFrame.IntegerHeight();
	}

	::Decorator* decorator = Decorator();

	if (decorator != NULL)
		decorator->SetOutlinesDelta(delta, dirtyRegion);

	_UpdateContentRegion();
}


void
Window::ScrollViewBy(View* view, int32 dx, int32 dy)
{
	if (!view || view == fTopView.Get() || (dx == 0 && dy == 0))
		return;

	BRegion* dirty = fRegionPool.GetRegion();
	if (!dirty)
		return;

	view->ScrollBy(dx, dy, dirty);

	if (!IsOffscreenWindow() && IsVisible() && view->IsVisible()) {
		dirty->IntersectWith(&VisibleContentRegion());
		_TriggerContentRedraw(*dirty);
	}

	fRegionPool.Recycle(dirty);
}


void
Window::CopyContents(BRegion* region, int32 xOffset, int32 yOffset)
{
	if (!IsVisible())
		return;

	BRegion* newDirty = fRegionPool.GetRegion(*region);

	region->IntersectWith(&VisibleContentRegion());
	if (region->CountRects() > 0) {
		region->OffsetBy(xOffset, yOffset);
		region->IntersectWith(&fVisibleContentRegion);
		if (region->CountRects() > 0) {
			region->OffsetBy(-xOffset, -yOffset);

			BRegion* allDirtyRegions = fRegionPool.GetRegion(fDirtyRegion);
			if (allDirtyRegions != NULL) {
				if (fPendingUpdateSession->IsUsed()) {
					allDirtyRegions->Include(
						&fPendingUpdateSession->DirtyRegion());
				}
				if (fCurrentUpdateSession->IsUsed()) {
					allDirtyRegions->Include(
						&fCurrentUpdateSession->DirtyRegion());
				}
				allDirtyRegions->IntersectWith(region);
			}

			BRegion* copyRegion = fRegionPool.GetRegion(*region);
			if (copyRegion != NULL) {
				if (allDirtyRegions != NULL)
					copyRegion->Exclude(allDirtyRegions);

				if (fDrawingEngine->LockParallelAccess()) {
					fDrawingEngine->CopyRegion(copyRegion, xOffset, yOffset);
					fDrawingEngine->UnlockParallelAccess();

					newDirty->Exclude(copyRegion);

					copyRegion->OffsetBy(xOffset, yOffset);
					if (fPendingUpdateSession->IsUsed())
						fPendingUpdateSession->DirtyRegion().Exclude(copyRegion);
				}

				fRegionPool.Recycle(copyRegion);
			} else {
				if (fDrawingEngine->LockParallelAccess()) {
					fDrawingEngine->CopyRegion(region, xOffset, yOffset);
					fDrawingEngine->UnlockParallelAccess();
				}
			}

			if (allDirtyRegions != NULL)
				fRegionPool.Recycle(allDirtyRegions);
		}
	}

	newDirty->OffsetBy(xOffset, yOffset);
	newDirty->IntersectWith(&fVisibleContentRegion);
	if (newDirty->CountRects() > 0)
		ProcessDirtyRegion(*newDirty);

	fRegionPool.Recycle(newDirty);
}


// Window Implementation - View Management

void
Window::SetTopView(View* topView)
{
	if (fTopView.IsSet()) {
		fTopView->DetachedFromWindow();
	}

	fTopView.SetTo(topView);

	if (fTopView.IsSet()) {
		fTopView->MoveBy((int32)(fFrame.left - fTopView->Frame().left),
			(int32)(fFrame.top - fTopView->Frame().top), NULL);

		fTopView->ResizeBy((int32)(fFrame.Width() - fTopView->Frame().Width()),
			(int32)(fFrame.Height() - fTopView->Frame().Height()), NULL);

		fTopView->AttachedToWindow(this);
	}
}


View*
Window::ViewAt(const BPoint& where)
{
	return fTopView->ViewAt(where);
}


// Window Implementation - Drawing and Updates

void
Window::ProcessDirtyRegion(const BRegion& dirtyRegion, const BRegion& exposeRegion)
{
	if (fDirtyRegion.CountRects() == 0) {
		ServerWindow()->RequestRedraw();
	}

	fDirtyRegion.Include(&dirtyRegion);
	fExposeRegion.Include(&exposeRegion);
}


void
Window::RedrawDirtyRegion()
{
	if (TopLayerStackWindow() != this) {
		fDirtyRegion.MakeEmpty();
		fExposeRegion.MakeEmpty();
		return;
	}

	if (IsVisible()) {
		_DrawBorder();

		BRegion* dirtyContentRegion = fRegionPool.GetRegion(VisibleContentRegion());
		BRegion* exposeContentRegion = fRegionPool.GetRegion(VisibleContentRegion());
		dirtyContentRegion->IntersectWith(&fDirtyRegion);
		exposeContentRegion->IntersectWith(&fExposeRegion);

		_TriggerContentRedraw(*dirtyContentRegion, *exposeContentRegion);

		fRegionPool.Recycle(dirtyContentRegion);
		fRegionPool.Recycle(exposeContentRegion);
	}

	fDirtyRegion.MakeEmpty();
	fExposeRegion.MakeEmpty();
}


void
Window::MarkDirty(BRegion& regionOnScreen)
{
	if (fDesktop)
		fDesktop->MarkDirty(regionOnScreen);
}


void
Window::MarkContentDirty(BRegion& dirtyRegion, BRegion& exposeRegion)
{
	if (fHidden || IsOffscreenWindow())
		return;

	dirtyRegion.IntersectWith(&VisibleContentRegion());
	exposeRegion.IntersectWith(&VisibleContentRegion());
	_TriggerContentRedraw(dirtyRegion, exposeRegion);
}


void
Window::MarkContentDirtyAsync(BRegion& dirtyRegion)
{
	if (fHidden || IsOffscreenWindow())
		return;

	dirtyRegion.IntersectWith(&VisibleContentRegion());

	if (fDirtyRegion.CountRects() == 0) {
		ServerWindow()->RequestRedraw();
	}

	fDirtyRegion.Include(&dirtyRegion);
}


void
Window::InvalidateView(View* view, BRegion& viewRegion)
{
	if (view && IsVisible() && view->IsVisible()) {
		if (!fContentRegionValid)
			_UpdateContentRegion();

		view->LocalToScreenTransform().Apply(&viewRegion);
		viewRegion.IntersectWith(&VisibleContentRegion());
		if (viewRegion.CountRects() > 0) {
			viewRegion.IntersectWith(
				&view->ScreenAndUserClipping(&fContentRegion));

			_TriggerContentRedraw(viewRegion);
		}
	}
}


void
Window::DisableUpdateRequests()
{
	fUpdatesEnabled = false;
}


void
Window::EnableUpdateRequests()
{
	fUpdatesEnabled = true;
	if (!fUpdateRequested && fPendingUpdateSession->IsUsed())
		_SendUpdateMessage();
}


void
Window::BeginUpdate(BPrivate::PortLink& link)
{
	if (!fUpdateRequested) {
		link.StartMessage(B_ERROR);
		link.Flush();
		fprintf(stderr, "Window::BeginUpdate() - no update requested!\n");
		return;
	}

	UpdateSession* temp = fCurrentUpdateSession;
	fCurrentUpdateSession = fPendingUpdateSession;
	fPendingUpdateSession = temp;
	fPendingUpdateSession->SetUsed(false);
	fInUpdate = true;
	fEffectiveDrawingRegionValid = false;

	// NOTE: Potential optimization - each view could be drawn individually
	// right before executing the first drawing command from the client during update.
	// This would allow for more granular control and potentially better performance.
	// Implementation could check View::IsBackgroundDirty() to determine if redraw needed.
	// However, this requires careful coordination with the client-side drawing protocol.
	if (!fContentRegionValid)
		_UpdateContentRegion();

	BRegion* dirty = fRegionPool.GetRegion(
		fCurrentUpdateSession->DirtyRegion());
	if (!dirty) {
		link.StartMessage(B_ERROR);
		link.Flush();
		return;
	}

	dirty->IntersectWith(&VisibleContentRegion());

	link.StartMessage(B_OK);
	link.Attach<BPoint>(fFrame.LeftTop());
	link.Attach<float>(fFrame.Width());
	link.Attach<float>(fFrame.Height());
	fTopView->AddTokensForViewsInRegion(link, *dirty, &fContentRegion);
	link.Attach<int32>(B_NULL_TOKEN);
	link.Flush();

	fDrawingEngine->SetCopyToFrontEnabled(false);

	if (fDrawingEngine->LockParallelAccess()) {
		fTopView->Draw(GetDrawingEngine(), dirty, &fContentRegion, true);

		fDrawingEngine->UnlockParallelAccess();
	}

	fRegionPool.Recycle(dirty);
}


void
Window::EndUpdate()
{
	if (fInUpdate) {
		fDrawingEngine->SetCopyToFrontEnabled(true);

		BRegion* dirty = fRegionPool.GetRegion(
			fCurrentUpdateSession->DirtyRegion());

		if (dirty) {
			dirty->IntersectWith(&VisibleContentRegion());

			fDrawingEngine->CopyToFront(*dirty);
			fRegionPool.Recycle(dirty);
		}

		fCurrentUpdateSession->SetUsed(false);

		fInUpdate = false;
		fEffectiveDrawingRegionValid = false;
	}
	if (fPendingUpdateSession->IsUsed()) {
		_SendUpdateMessage();
	} else {
		fUpdateRequested = false;
	}
}


// Window Implementation - User Interaction - Mouse Events

void
Window::MouseDown(BMessage* message, BPoint where,
	const ClickTarget& lastClickTarget, int32& clickCount,
	ClickTarget& _clickTarget)
{
	int32 windowToken = fWindow->ServerToken();
	int32 lastHitRegion = 0;
	if (lastClickTarget.GetType() == ClickTarget::TYPE_WINDOW_DECORATOR
		&& lastClickTarget.WindowToken() == windowToken) {
		lastHitRegion = lastClickTarget.WindowElement();
	}

	int32 hitRegion = 0;
	bool eventEaten = fWindowBehaviour->MouseDown(message, where, lastHitRegion,
		clickCount, hitRegion);

	if (eventEaten) {
		_clickTarget = ClickTarget(ClickTarget::TYPE_WINDOW_DECORATOR,
			windowToken, (int32)hitRegion);
	} else {
		int32 viewToken = B_NULL_TOKEN;
		if (View* view = ViewAt(where)) {
			if (HasModal())
				return;

			if (!IsFocus()) {
				bool acceptFirstClick
					= (Flags() & B_WILL_ACCEPT_FIRST_CLICK) != 0;

				if (!acceptFirstClick) {
					bool avoidFocus = (Flags() & B_AVOID_FOCUS) != 0;
					DesktopSettings desktopSettings(fDesktop);
					if (desktopSettings.MouseMode() == B_NORMAL_MOUSE)
						fDesktop->ActivateWindow(this);
					else if (!avoidFocus)
						fDesktop->SetFocusWindow(this);

					if (!desktopSettings.AcceptFirstClick() && !avoidFocus)
						return;
				}
			}

			viewToken = view->Token();
			view->MouseDown(message, where);
		}

		_clickTarget = ClickTarget(ClickTarget::TYPE_WINDOW_CONTENTS,
			windowToken, viewToken);
	}
}


void
Window::MouseUp(BMessage* message, BPoint where, int32* _viewToken)
{
	fWindowBehaviour->MouseUp(message, where);

	if (View* view = ViewAt(where)) {
		if (HasModal())
			return;

		*_viewToken = view->Token();
		view->MouseUp(message, where);
	}
}


void
Window::MouseMoved(BMessage *message, BPoint where, int32* _viewToken,
	bool isLatestMouseMoved, bool isFake)
{
	View* view = ViewAt(where);
	if (view != NULL)
		*_viewToken = view->Token();

	if (!isLatestMouseMoved)
		return;

	fWindowBehaviour->MouseMoved(message, where, isFake);

	if (view != NULL) {
		view->MouseMoved(message, where);

		ServerWindow()->App()->SetCurrentCursor(view->Cursor());
	}
}


// Window Implementation - User Interaction - Keyboard Events

void
Window::ModifiersChanged(int32 modifiers)
{
	fWindowBehaviour->ModifiersChanged(modifiers);
}


// Window Implementation - Window State Management

void
Window::SetTitle(const char* name, BRegion& dirty)
{
	fTitle = name;

	::Decorator* decorator = Decorator();
	if (decorator) {
		int32 index = PositionInStack();
		decorator->SetTitle(index, name, &dirty);
	}
}


void
Window::SetFocus(bool focus)
{
	::Decorator* decorator = Decorator();

	BRegion* dirty = NULL;
	if (decorator)
		dirty = fRegionPool.GetRegion(decorator->GetFootprint());
	if (dirty) {
		dirty->IntersectWith(&fVisibleRegion);
		fDesktop->MarkDirty(*dirty);
		fRegionPool.Recycle(dirty);
	}

	fIsFocus = focus;
	if (decorator) {
		int32 index = PositionInStack();
		decorator->SetFocus(index, focus);
	}

	Activated(focus);
}


void
Window::SetHidden(bool hidden)
{
	if (fHidden != hidden) {
		fHidden = hidden;

		fTopView->SetHidden(hidden);
	}
}


void
Window::SetShowLevel(int32 showLevel)
{
	if (showLevel == fShowLevel)
		return;

	fShowLevel = showLevel;
}


void
Window::SetMinimized(bool minimized)
{
	if (minimized == fMinimized)
		return;

	fMinimized = minimized;
}


bool
Window::IsVisible() const
{
	if (IsOffscreenWindow())
		return true;

	if (IsHidden())
		return false;

	return fCurrentWorkspace >= 0 && fCurrentWorkspace < kWorkingList;
}


bool
Window::IsDragging() const
{
	if (!fWindowBehaviour.IsSet())
		return false;
	return fWindowBehaviour->IsDragging();
}


bool
Window::IsResizing() const
{
	if (!fWindowBehaviour.IsSet())
		return false;
	return fWindowBehaviour->IsResizing();
}


// Window Implementation - Window Limits

void
Window::SetSizeLimits(int32 minWidth, int32 maxWidth, int32 minHeight,
	int32 maxHeight)
{
	if (minWidth < 0)
		minWidth = 0;

	if (minHeight < 0)
		minHeight = 0;

	fMinWidth = minWidth;
	fMaxWidth = maxWidth;
	fMinHeight = minHeight;
	fMaxHeight = maxHeight;

	::Decorator* decorator = Decorator();
	if (decorator) {
		decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth,
			&fMaxHeight);
	}

	_ObeySizeLimits();
}


void
Window::GetSizeLimits(int32* minWidth, int32* maxWidth,
	int32* minHeight, int32* maxHeight) const
{
	*minWidth = fMinWidth;
	*maxWidth = fMaxWidth;
	*minHeight = fMinHeight;
	*maxHeight = fMaxHeight;
}


// Window Implementation - Window Appearance

bool
Window::SetTabLocation(float location, bool isShifting, BRegion& dirty)
{
	::Decorator* decorator = Decorator();
	if (decorator) {
		int32 index = PositionInStack();
		return decorator->SetTabLocation(index, location, isShifting, &dirty);
	}

	return false;
}


float
Window::TabLocation() const
{
	::Decorator* decorator = Decorator();
	if (decorator) {
		int32 index = PositionInStack();
		return decorator->TabLocation(index);
	}
	return 0.0;
}


bool
Window::SetDecoratorSettings(const BMessage& settings, BRegion& dirty)
{
	if (settings.what == 'prVu') {
		BString path;
		if (settings.FindString("preview", &path) == B_OK)
			return gDecorManager.PreviewDecorator(path, this) == B_OK;
		return false;
	}

	::Decorator* decorator = Decorator();
	if (decorator)
		return decorator->SetSettings(settings, &dirty);

	return false;
}


bool
Window::GetDecoratorSettings(BMessage* settings)
{
	if (fDesktop)
		fDesktop->GetDecoratorSettings(this, *settings);

	::Decorator* decorator = Decorator();
	if (decorator)
		return decorator->GetSettings(settings);

	return false;
}


void
Window::FontsChanged(BRegion* updateRegion)
{
	::Decorator* decorator = Decorator();
	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->FontsChanged(settings, updateRegion);
	}
}


void
Window::ColorsChanged(BRegion* updateRegion)
{
	::Decorator* decorator = Decorator();
	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->ColorsChanged(settings, updateRegion);
	}
}


void
Window::SetLook(window_look look, BRegion* updateRegion)
{
	fLook = look;

	fContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;

	if (!fCurrentStack.IsSet())
		return;

	int32 stackPosition = PositionInStack();

	::Decorator* decorator = Decorator();
	if (decorator == NULL && look != B_NO_BORDER_WINDOW_LOOK) {
		decorator = gDecorManager.AllocateDecorator(this);
		fCurrentStack->SetDecorator(decorator);
		if (IsFocus())
			decorator->SetFocus(stackPosition, true);
	}

	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->SetLook(stackPosition, settings, look, updateRegion);

		decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth,
			&fMaxHeight);
		_ObeySizeLimits();
	}

	if (look == B_NO_BORDER_WINDOW_LOOK) {
		fCurrentStack->SetDecorator(NULL);
	}
}


void
Window::SetFeel(window_feel feel)
{
	if ((fFeel == B_MODAL_SUBSET_WINDOW_FEEL
			|| fFeel == B_FLOATING_SUBSET_WINDOW_FEEL)
		&& feel != B_MODAL_SUBSET_WINDOW_FEEL
		&& feel != B_FLOATING_SUBSET_WINDOW_FEEL)
		fSubsets.MakeEmpty();

	fFeel = feel;

	fFlags = fOriginalFlags;
	fFlags &= ValidWindowFlags(fFeel);

	if (!IsNormal()) {
		fFlags |= B_SAME_POSITION_IN_ALL_WORKSPACES;
		_PropagatePosition();
	}
}


void
Window::SetFlags(uint32 flags, BRegion* updateRegion)
{
	fOriginalFlags = flags;
	fFlags = flags & ValidWindowFlags(fFeel);
	if (!IsNormal())
		fFlags |= B_SAME_POSITION_IN_ALL_WORKSPACES;

	if ((fFlags & B_SAME_POSITION_IN_ALL_WORKSPACES) != 0)
		_PropagatePosition();

	::Decorator* decorator = Decorator();
	if (decorator == NULL)
		return;

	int32 stackPosition = PositionInStack();
	decorator->SetFlags(stackPosition, flags, updateRegion);

	decorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth, &fMaxHeight);
	_ObeySizeLimits();

	// NOTE: Disabled automatic update request control for kWindowScreenFlag
	// The nested enable/disable calls could interfere with manual control
	// and lose the previous update state. If this functionality is needed,
	// it should be implemented with a proper nesting counter or state tracking.
#if 0
	if ((fOriginalFlags & kWindowScreenFlag) != (flags & kWindowScreenFlag)) {
		if ((flags & kWindowScreenFlag) != 0)
			DisableUpdateRequests();
		else
			EnableUpdateRequests();
	}
#endif
}


// Window Implementation - Workspace Management

void
Window::WorkspaceActivated(int32 index, bool active)
{
	BMessage activatedMsg(B_WORKSPACE_ACTIVATED);
	activatedMsg.AddInt64("when", system_time());
	activatedMsg.AddInt32("workspace", index);
	activatedMsg.AddBool("active", active);

	ServerWindow()->SendMessageToClient(&activatedMsg);
}


void
Window::WorkspacesChanged(uint32 oldWorkspaces, uint32 newWorkspaces)
{
	fWorkspaces = newWorkspaces;

	BMessage changedMsg(B_WORKSPACES_CHANGED);
	changedMsg.AddInt64("when", system_time());
	changedMsg.AddInt32("old", oldWorkspaces);
	changedMsg.AddInt32("new", newWorkspaces);

	ServerWindow()->SendMessageToClient(&changedMsg);
}


void
Window::Activated(bool active)
{
	BMessage msg(B_WINDOW_ACTIVATED);
	msg.AddBool("active", active);
	ServerWindow()->SendMessageToClient(&msg);
}


bool
Window::InWorkspace(int32 index) const
{
	return (fWorkspaces & (1UL << index)) != 0;
}


// Window Implementation - Window Hierarchy and Relationships

bool
Window::SupportsFront()
{
	if (fFeel == kDesktopWindowFeel
		|| fFeel == kMenuWindowFeel
		|| (fFlags & B_AVOID_FRONT) != 0)
		return false;

	return true;
}


bool
Window::IsModal() const
{
	return IsModalFeel(fFeel);
}


bool
Window::IsFloating() const
{
	return IsFloatingFeel(fFeel);
}


bool
Window::IsNormal() const
{
	return !IsFloatingFeel(fFeel) && !IsModalFeel(fFeel);
}


bool
Window::HasModal() const
{
	for (Window* window = NextWindow(fCurrentWorkspace); window != NULL;
			window = window->NextWindow(fCurrentWorkspace)) {
		if (window->IsHidden() || !window->IsModal())
			continue;

		if (window->HasInSubset(this))
			return true;
	}

	return false;
}


Window*
Window::Backmost(Window* window, int32 workspace)
{
	if (workspace == -1)
		workspace = fCurrentWorkspace;

	ASSERT(workspace != -1);
	if (workspace == -1)
		return NULL;

	if (fFeel == kDesktopWindowFeel)
		return NULL;

	if (window == NULL)
		window = PreviousWindow(workspace);

	for (; window != NULL; window = window->PreviousWindow(workspace)) {
		if (window->IsHidden() || window == this)
			continue;

		if (HasInSubset(window))
			return window;
	}

	return NULL;
}


Window*
Window::Frontmost(Window* first, int32 workspace)
{
	if (workspace == -1)
		workspace = fCurrentWorkspace;

	ASSERT(workspace != -1);
	if (workspace == -1)
		return NULL;

	if (fFeel == kDesktopWindowFeel)
		return first ? first : NextWindow(workspace);

	if (first == NULL)
		first = NextWindow(workspace);

	for (Window* window = first; window != NULL;
			window = window->NextWindow(workspace)) {
		if (window->IsHidden() || window == this)
			continue;

		if (window->HasInSubset(this))
			return window;
	}

	return NULL;
}


bool
Window::AddToSubset(Window* window)
{
	return fSubsets.AddItem(window);
}


void
Window::RemoveFromSubset(Window* window)
{
	fSubsets.RemoveItem(window);
}


bool
Window::HasInSubset(const Window* window) const
{
	if (window == NULL || fFeel == window->Feel()
		|| fFeel == B_NORMAL_WINDOW_FEEL)
		return false;

	if (fFeel == kMenuWindowFeel)
		return window->ServerWindow()->App() == ServerWindow()->App();
	if (window->Feel() == kMenuWindowFeel)
		return false;

	const int32 kFeels[] = {kPasswordWindowFeel, kWindowScreenFeel,
		B_MODAL_ALL_WINDOW_FEEL, B_FLOATING_ALL_WINDOW_FEEL};

	for (uint32 order = 0;
			order < sizeof(kFeels) / sizeof(kFeels[0]); order++) {
		if (fFeel == kFeels[order])
			return true;
		if (window->Feel() == kFeels[order])
			return false;
	}

	if ((fFeel == B_FLOATING_APP_WINDOW_FEEL
			&& window->Feel() != B_MODAL_APP_WINDOW_FEEL)
		|| fFeel == B_MODAL_APP_WINDOW_FEEL)
		return window->ServerWindow()->App() == ServerWindow()->App();

	return fSubsets.HasItem(window);
}


void
Window::FindWorkspacesViews(BObjectList<WorkspacesView>& list) const
{
	int32 count = fWorkspacesViewCount;
	fTopView->FindViews(kWorkspacesViewFlag, (BObjectList<View>&)list, count);
}


uint32
Window::SubsetWorkspaces() const
{
	if (fFeel == B_MODAL_ALL_WINDOW_FEEL
		|| fFeel == B_FLOATING_ALL_WINDOW_FEEL)
		return B_ALL_WORKSPACES;

	if (fFeel == B_FLOATING_APP_WINDOW_FEEL) {
		Window* front = fDesktop->FrontWindow();
		if (front != NULL && front->IsNormal()
			&& front->ServerWindow()->App() == ServerWindow()->App())
			return ServerWindow()->App()->Workspaces();

		return 0;
	}

	if (fFeel == B_MODAL_APP_WINDOW_FEEL) {
		uint32 workspaces = ServerWindow()->App()->Workspaces();
		if (workspaces == 0) {
			return 1UL << fDesktop->CurrentWorkspace();
		}
		return workspaces;
	}

	if (fFeel == B_MODAL_SUBSET_WINDOW_FEEL
		|| fFeel == B_FLOATING_SUBSET_WINDOW_FEEL) {
		uint32 workspaces = 0;
		bool hasNormalFront = false;
		for (int32 i = 0; i < fSubsets.CountItems(); i++) {
			Window* window = fSubsets.ItemAt(i);

			if (!window->IsHidden())
				workspaces |= window->Workspaces();
			if (window == fDesktop->FrontWindow() && window->IsNormal())
				hasNormalFront = true;
		}

		if (fFeel == B_FLOATING_SUBSET_WINDOW_FEEL && !hasNormalFront)
			return 0;

		return workspaces;
	}

	return 0;
}


bool
Window::InSubsetWorkspace(int32 index) const
{
	return (SubsetWorkspaces() & (1UL << index)) != 0;
}


// Window Implementation - Window Stack Management

int32
Window::PositionInStack() const
{
	if (!fCurrentStack.IsSet())
		return -1;
	return fCurrentStack->WindowList().IndexOf(this);
}


bool
Window::DetachFromWindowStack(bool ownStackNeeded)
{
	if (!fCurrentStack.IsSet())
		return false;
	if (fCurrentStack->CountWindows() == 1)
		return true;

	int32 index = PositionInStack();

	if (fCurrentStack->RemoveWindow(this) == false)
		return false;

	BRegion invalidatedRegion;
	::Decorator* decorator = fCurrentStack->Decorator();
	if (decorator != NULL) {
		decorator->RemoveTab(index, &invalidatedRegion);
		decorator->SetTopTab(fCurrentStack->LayerOrder().CountItems() - 1);
	}

	Window* remainingTop = fCurrentStack->TopLayerWindow();
	if (remainingTop != NULL) {
		if (decorator != NULL)
			decorator->SetDrawingEngine(remainingTop->GetDrawingEngine());
		remainingTop->SetFocus(remainingTop->IsFocus());
		remainingTop->SetLook(remainingTop->Look(), NULL);
	}

	fCurrentStack = NULL;
	if (ownStackNeeded == true)
		_InitWindowStack();
	SetFocus(IsFocus());

	if (remainingTop != NULL) {
		invalidatedRegion.Include(&remainingTop->VisibleRegion());
		fDesktop->RebuildAndRedrawAfterWindowChange(remainingTop,
			invalidatedRegion);
	}
	return true;
}


bool
Window::AddWindowToStack(Window* window)
{
	ASSERT_MULTI_WRITE_LOCKED(fDesktop->WindowLocker());

	WindowStack* stack = GetWindowStack();
	if (stack == NULL)
		return false;

	BRegion dirty;
	BRect ownFrame = Frame();
	BRect frame = window->Frame();
	float deltaToX = round(ownFrame.left - frame.left);
	float deltaToY = round(ownFrame.top - frame.top);
	frame.OffsetBy(deltaToX, deltaToY);
	float deltaByX = round(ownFrame.right - frame.right);
	float deltaByY = round(ownFrame.bottom - frame.bottom);
	dirty.Include(&window->VisibleRegion());
	window->MoveBy(deltaToX, deltaToY, false);
	window->ResizeBy(deltaByX, deltaByY, &dirty, false);

	::Decorator* otherDecorator = window->Decorator();
	if (otherDecorator != NULL)
		dirty.Include(otherDecorator->TitleBarRect());
	::Decorator* decorator = stack->Decorator();
	if (decorator != NULL)
		dirty.Include(decorator->TitleBarRect());

	int32 position = PositionInStack() + 1;
	if (position >= stack->CountWindows())
		position = -1;
	if (stack->AddWindow(window, position) == false)
		return false;
	window->DetachFromWindowStack(false);
	window->fCurrentStack.SetTo(stack);

	if (decorator != NULL) {
		DesktopSettings settings(fDesktop);
		decorator->AddTab(settings, window->Title(), window->Look(),
			window->Flags(), position, &dirty);
	}

	window->SetLook(window->Look(), &dirty);
	fDesktop->RebuildAndRedrawAfterWindowChange(TopLayerStackWindow(), dirty);
	window->SetFocus(window->IsFocus());
	return true;
}


Window*
Window::StackedWindowAt(const BPoint& where)
{
	::Decorator* decorator = Decorator();
	if (decorator == NULL)
		return this;

	int tab = decorator->TabAt(where);
	Window* window = fCurrentStack->WindowAt(tab);
	if (window != NULL)
		return window;
	return this;
}


Window*
Window::TopLayerStackWindow()
{
	if (!fCurrentStack.IsSet())
		return this;
	return fCurrentStack->TopLayerWindow();
}


WindowStack*
Window::GetWindowStack()
{
	if (!fCurrentStack.IsSet())
		return _InitWindowStack();
	return fCurrentStack;
}


bool
Window::MoveToTopStackLayer()
{
	::Decorator* decorator = Decorator();
	if (decorator == NULL)
		return false;
	decorator->SetDrawingEngine(GetDrawingEngine());
	SetLook(Look(), NULL);
	decorator->SetTopTab(PositionInStack());
	return fCurrentStack->MoveToTopLayer(this);
}


bool
Window::MoveToStackPosition(int32 to, bool isMoving)
{
	if (!fCurrentStack.IsSet())
		return false;
	int32 index = PositionInStack();
	if (fCurrentStack->Move(index, to) == false)
		return false;

	BRegion dirty;
	::Decorator* decorator = Decorator();
	if (decorator && decorator->MoveTab(index, to, isMoving, &dirty) == false)
		return false;

	fDesktop->RebuildAndRedrawAfterWindowChange(this, dirty);
	return true;
}


// Window Implementation - Static Utility Methods

bool
Window::IsValidLook(window_look look)
{
	return look == B_TITLED_WINDOW_LOOK
		|| look == B_DOCUMENT_WINDOW_LOOK
		|| look == B_MODAL_WINDOW_LOOK
		|| look == B_FLOATING_WINDOW_LOOK
		|| look == B_BORDERED_WINDOW_LOOK
		|| look == B_NO_BORDER_WINDOW_LOOK
		|| look == kDesktopWindowLook
		|| look == kLeftTitledWindowLook;
}


bool
Window::IsValidFeel(window_feel feel)
{
	return feel == B_NORMAL_WINDOW_FEEL
		|| feel == B_MODAL_SUBSET_WINDOW_FEEL
		|| feel == B_MODAL_APP_WINDOW_FEEL
		|| feel == B_MODAL_ALL_WINDOW_FEEL
		|| feel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| feel == B_FLOATING_APP_WINDOW_FEEL
		|| feel == B_FLOATING_ALL_WINDOW_FEEL
		|| feel == kDesktopWindowFeel
		|| feel == kMenuWindowFeel
		|| feel == kWindowScreenFeel
		|| feel == kPasswordWindowFeel
		|| feel == kOffscreenWindowFeel;
}


bool
Window::IsModalFeel(window_feel feel)
{
	return feel == B_MODAL_SUBSET_WINDOW_FEEL
		|| feel == B_MODAL_APP_WINDOW_FEEL
		|| feel == B_MODAL_ALL_WINDOW_FEEL;
}


bool
Window::IsFloatingFeel(window_feel feel)
{
	return feel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| feel == B_FLOATING_APP_WINDOW_FEEL
		|| feel == B_FLOATING_ALL_WINDOW_FEEL;
}


uint32
Window::ValidWindowFlags()
{
	return B_NOT_MOVABLE
		| B_NOT_CLOSABLE
		| B_NOT_ZOOMABLE
		| B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE
		| B_NOT_H_RESIZABLE
		| B_NOT_V_RESIZABLE
		| B_AVOID_FRONT
		| B_AVOID_FOCUS
		| B_WILL_ACCEPT_FIRST_CLICK
		| B_OUTLINE_RESIZE
		| B_NO_WORKSPACE_ACTIVATION
		| B_NOT_ANCHORED_ON_ACTIVATE
		| B_ASYNCHRONOUS_CONTROLS
		| B_QUIT_ON_WINDOW_CLOSE
		| B_SAME_POSITION_IN_ALL_WORKSPACES
		| B_AUTO_UPDATE_SIZE_LIMITS
		| B_CLOSE_ON_ESCAPE
		| B_NO_SERVER_SIDE_WINDOW_MODIFIERS
		| kWindowScreenFlag
		| kAcceptKeyboardFocusFlag;
}


uint32
Window::ValidWindowFlags(window_feel feel)
{
	uint32 flags = ValidWindowFlags();
	if (IsModalFeel(feel))
		return flags & ~(B_AVOID_FOCUS | B_AVOID_FRONT);

	return flags;
}


// Window Implementation - Protected Helper Methods

void
Window::_ShiftPartOfRegion(BRegion* region, BRegion* regionToShift,
	int32 xOffset, int32 yOffset)
{
	BRegion* common = fRegionPool.GetRegion(*regionToShift);
	if (!common)
		return;
	common->IntersectWith(region);
	if (common->CountRects() > 0) {
		region->Exclude(common);
		common->OffsetBy(xOffset, yOffset);
		region->Include(common);
	}
	fRegionPool.Recycle(common);
}


void
Window::_TriggerContentRedraw(BRegion& dirty, const BRegion& expose)
{
	if (!IsVisible() || dirty.CountRects() == 0 || (fFlags & kWindowScreenFlag) != 0)
		return;

	_TransferToUpdateSession(&dirty);

	if (expose.CountRects() > 0) {
		if (fDrawingEngine->LockParallelAccess()) {
			bool copyToFrontEnabled = fDrawingEngine->CopyToFrontEnabled();
			fDrawingEngine->SetCopyToFrontEnabled(true);
			fTopView->Draw(fDrawingEngine.Get(), &expose, &fContentRegion, true);
			fDrawingEngine->SetCopyToFrontEnabled(copyToFrontEnabled);
			fDrawingEngine->UnlockParallelAccess();
		}
	}
}


void
Window::_DrawBorder()
{
	::Decorator* decorator = Decorator();
	if (!decorator)
		return;

	BRegion* dirtyBorderRegion = fRegionPool.GetRegion();
	if (!dirtyBorderRegion)
		return;
	GetBorderRegion(dirtyBorderRegion);
	dirtyBorderRegion->IntersectWith(&fVisibleRegion);
	dirtyBorderRegion->IntersectWith(&fDirtyRegion);

	DrawingEngine* engine = decorator->GetDrawingEngine();
	if (dirtyBorderRegion->CountRects() > 0 && engine->LockParallelAccess()) {
		engine->ConstrainClippingRegion(dirtyBorderRegion);
		bool copyToFrontEnabled = engine->CopyToFrontEnabled();
		engine->SetCopyToFrontEnabled(false);

		decorator->Draw(dirtyBorderRegion->Frame());

		engine->SetCopyToFrontEnabled(copyToFrontEnabled);
		engine->CopyToFront(*dirtyBorderRegion);

		// HACK: Synchronize DrawState between Decorator and ServerWindow
		// When decorator draws text, it modifies the Painter's DrawState
		// which can become out of sync with ServerWindow's cached DrawState.
		// This workaround forces a resync, but ideally the DrawState management
		// should be refactored to avoid this issue entirely.
		// See: Painter class and ServerWindow::_UpdateDrawState()
		fWindow->ResyncDrawState();

		engine->UnlockParallelAccess();
	}
	fRegionPool.Recycle(dirtyBorderRegion);
}


void
Window::_TransferToUpdateSession(BRegion* contentDirtyRegion)
{
	if (contentDirtyRegion->CountRects() <= 0)
		return;

	fPendingUpdateSession->SetUsed(true);
	fPendingUpdateSession->Include(contentDirtyRegion);

	if (!fUpdateRequested) {
		_SendUpdateMessage();
	}
}


void
Window::_SendUpdateMessage()
{
	if (!fUpdatesEnabled)
		return;

	BMessage message(_UPDATE_);
	if (ServerWindow()->SendMessageToClient(&message) != B_OK) {
		return;
	}

	fUpdateRequested = true;
	fEffectiveDrawingRegionValid = false;
}


void
Window::_UpdateContentRegion()
{
	fContentRegion.Set(fFrame);

	::Decorator* decorator = Decorator();
	if (decorator)
		fContentRegion.Exclude(&decorator->GetFootprint());

	fContentRegionValid = true;
}


void
Window::_ObeySizeLimits()
{
	if (fMaxWidth < fMinWidth)
		fMaxWidth = fMinWidth;

	if (fMaxHeight < fMinHeight)
		fMaxHeight = fMinHeight;

	float minWidthDiff = fMinWidth - fFrame.Width();
	float minHeightDiff = fMinHeight - fFrame.Height();
	float maxWidthDiff = fMaxWidth - fFrame.Width();
	float maxHeightDiff = fMaxHeight - fFrame.Height();

	float xDiff = 0.0;
	if (minWidthDiff > 0.0)
		xDiff = minWidthDiff;
	else if (maxWidthDiff < 0.0)
		xDiff = maxWidthDiff;

	float yDiff = 0.0;
	if (minHeightDiff > 0.0)
		yDiff = minHeightDiff;
	else if (maxHeightDiff < 0.0)
		yDiff = maxHeightDiff;

	if (fDesktop)
		fDesktop->ResizeWindowBy(this, xDiff, yDiff);
	else
		ResizeBy((int32)xDiff, (int32)yDiff, NULL);
}


// Window Implementation - Private Helper Methods

WindowStack*
Window::_InitWindowStack()
{
	fCurrentStack = NULL;
	::Decorator* decorator = NULL;
	if (fLook != B_NO_BORDER_WINDOW_LOOK)
		decorator = gDecorManager.AllocateDecorator(this);

	WindowStack* stack = new(std::nothrow) WindowStack(decorator);
	if (stack == NULL)
		return NULL;

	if (stack->AddWindow(this) != true) {
		delete stack;
		return NULL;
	}
	fCurrentStack.SetTo(stack, true);
	return stack;
}


// UpdateSession Implementation

Window::UpdateSession::UpdateSession()
	:
	fDirtyRegion(),
	fInUse(false)
{
}


void
Window::UpdateSession::Include(BRegion* additionalDirty)
{
	fDirtyRegion.Include(additionalDirty);
}


void
Window::UpdateSession::Exclude(BRegion* dirtyInNextSession)
{
	fDirtyRegion.Exclude(dirtyInNextSession);
}


void
Window::UpdateSession::MoveBy(int32 x, int32 y)
{
	fDirtyRegion.OffsetBy(x, y);
}


void
Window::UpdateSession::SetUsed(bool used)
{
	fInUse = used;
	if (!fInUse)
		fDirtyRegion.MakeEmpty();
}
