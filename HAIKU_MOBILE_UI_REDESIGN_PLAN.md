# Haiku Mobile UI Redesign Plan - iOS-Inspired Architecture

## Executive Summary

This document outlines a comprehensive step-by-step plan to redesign Haiku's interface for mobile operating systems, using iOS as a reference. The plan focuses on modifying existing classes and their functions rather than introducing new ones, leveraging Haiku's robust architecture while adapting it for touch-first, mobile-centric interactions.

## Design Philosophy

### Core Architectural Principles
1. **Preserve Architecture, Change Purpose**: Keep Haiku's mature BView/BWindow hierarchy but repurpose all functions for mobile-only use
2. **Remove Desktop Code**: Completely eliminate desktop-specific behaviors and metaphors 
3. **Transform Function Meaning**: Existing functions get new mobile-first semantics while maintaining architectural integrity
4. **Minimal New Classes**: Extend existing classes with mobile-specific behavior, never create parallel systems
5. **Mobile as Default**: All default behaviors are mobile - no conditional "if mobile" code paths
6. **Performance Focus**: Maintain Haiku's lightweight nature for mobile hardware constraints

### Target Mobile Features (iOS-Inspired)
- Fullscreen app paradigm
- Touch gesture recognition
- Home screen with app icons
- Status bar and notification center
- App switching/multitasking
- Modal presentation system
- Auto-layout responsive design

## Phase 1: Core Infrastructure Modifications

### Step 1.1: Touch Input Infrastructure Enhancement

#### Input Server Modifications (`src/servers/input/`)
**Files to Modify:**
- `src/servers/input/InputServer.cpp` - Main input coordination
- `src/servers/input/InputServer.h` - Touch input method declarations
- `src/servers/input/AddOnManager.cpp` - Touch device add-on loading
- `src/servers/input/AddOnManager.h` - Touch device management headers
- `src/servers/input/PathList.cpp` - Input device path management
- `src/servers/input/PathList.h` - Device path headers
- `src/servers/input/input_server.rdef` - Resource definitions for touch

**Filter Files to Create:**
- `src/add-ons/input_server/filters/TouchGestureFilter.cpp` - Gesture recognition
- `src/add-ons/input_server/filters/TouchGestureFilter.h` - Gesture filter headers
- `src/add-ons/input_server/filters/MultiTouchFilter.cpp` - Multi-touch processing
- `src/add-ons/input_server/filters/MultiTouchFilter.h` - Multi-touch headers

**Device Files to Modify:**
- `src/add-ons/input_server/devices/` - All existing device files for touch compatibility
- `src/servers/input/KeyboardSettings.cpp` - Virtual keyboard integration
- `src/servers/input/KeyboardSettings.h` - Virtual keyboard headers
- `src/servers/input/MouseSettings.cpp` - Touch coordinate calibration
- `src/servers/input/MouseSettings.h` - Touch settings headers

#### App Server Touch Integration (`src/servers/app/`)
**Core Server Files:**
- `src/servers/app/EventDispatcher.cpp` - Touch event routing
- `src/servers/app/EventDispatcher.h` - Touch event headers
- `src/servers/app/InputManager.cpp` - Touch input coordination
- `src/servers/app/InputManager.h` - Touch input headers
- `src/servers/app/Desktop.cpp` - Touch-aware desktop management
- `src/servers/app/Desktop.h` - Desktop touch headers
- `src/servers/app/View.cpp` - Server-side touch view handling
- `src/servers/app/View.h` - Server view touch headers
- `src/servers/app/Window.cpp` - Touch window management
- `src/servers/app/Window.h` - Window touch headers
- `src/servers/app/ServerWindow.cpp` - Touch message processing
- `src/servers/app/ServerWindow.h` - Server window touch headers

### Step 1.2: BView Touch Event Handling Enhancement

#### Interface Kit Modifications 
**Core Interface Files:**
- `src/kits/interface/View.cpp` - Add touch event methods
- `src/kits/interface/View.h` - Touch method declarations  
- `src/kits/interface/InterfaceDefs.cpp` - Touch constants and enums
- `headers/os/interface/View.h` - Public touch API
- `headers/os/interface/InterfaceDefs.h` - Touch definitions
- `headers/private/interface/ViewPrivate.h` - Internal touch state

**Event Handling Files:**
- `src/kits/interface/Input.cpp` - Touch input message processing
- `headers/os/interface/Input.h` - Touch input headers
- `src/kits/app/MessageUtils.cpp` - Touch message utilities
- `src/kits/app/Message.cpp` - Touch message support

**Touch Event Extensions:**
- `src/kits/interface/Dragger.cpp` - Touch drag handling
- `src/kits/interface/Dragger.h` - Touch drag headers
- `headers/private/interface/DraggerPrivate.h` - Touch drag internals

### Step 1.3: Window Management for Mobile

#### BWindow Repurposed for Mobile (Remove Desktop Code)
**Transform Window Functions - Delete Desktop, Keep Mobile:**
- `src/kits/interface/Window.cpp` - REMOVE: window resizing, minimize/maximize, multiple window management. REPURPOSE: constructor defaults to fullscreen, focus = app activation (existing - MUST READ FULLY)
- `src/kits/interface/Window.h` - REMOVE: desktop window flags. REPURPOSE: window types become app states (existing)
- `headers/os/interface/Window.h` - REMOVE: desktop API. KEEP: mobile-relevant API only (existing)
- `headers/private/interface/WindowPrivate.h` - REMOVE: desktop window state. REPURPOSE: mobile app lifecycle state (existing)
- `headers/private/interface/WindowInfo.h` - REMOVE: multi-window info. REPURPOSE: single app info (existing)

**Server Window Always Mobile:**
- `src/servers/app/Window.cpp` - Always mobile app lifecycle (existing - MUST READ FULLY)
- `src/servers/app/Window.h` - Window lifecycle headers (existing)  
- `src/servers/app/ServerWindow.cpp` - Always B_APP_WILL_SUSPEND/B_APP_DID_RESUME (existing - MUST READ FULLY)
- `src/servers/app/ServerWindow.h` - Server window lifecycle headers (existing)

**Desktop Integration Files:**
- `src/servers/app/Desktop.cpp` - Mobile desktop mode
- `src/servers/app/Desktop.h` - Mobile desktop headers
- `src/servers/app/DesktopSettings.cpp` - Mobile settings
- `src/servers/app/DesktopSettings.h` - Mobile settings headers
- `src/servers/app/DesktopListener.cpp` - Mobile event listeners
- `src/servers/app/DesktopListener.h` - Mobile listener headers

**Window Management Files:**
- `src/servers/app/WindowList.cpp` - Mobile window tracking
- `src/servers/app/WindowList.h` - Mobile window list headers
- `src/servers/app/Workspace.cpp` - Mobile workspace management
- `src/servers/app/Workspace.h` - Mobile workspace headers
- `src/servers/app/WorkspacesView.cpp` - Mobile workspace view
- `src/servers/app/WorkspacesView.h` - Mobile workspace view headers

## Phase 2: Layout System Mobile Optimization

### Step 2.1: Auto-Layout Enhancements

#### Layout Manager Modifications
**Core Layout Files:**
- `src/kits/interface/AbstractLayout.cpp` - Base constraint system
- `src/kits/interface/AbstractLayout.h` - Abstract layout headers
- `headers/os/interface/AbstractLayout.h` - Public layout API
- `src/kits/interface/GroupLayout.cpp` - Constraint-based group layout
- `src/kits/interface/GroupLayout.h` - Group layout headers
- `headers/os/interface/GroupLayout.h` - Public group layout API
- `src/kits/interface/TwoDimensionalLayout.cpp` - 2D constraint layout
- `headers/os/interface/TwoDimensionalLayout.h` - 2D layout API

**Layout Item Files:**
- `src/kits/interface/LayoutItem.cpp` - Constraint relationships
- `src/kits/interface/LayoutItem.h` - Layout item headers
- `headers/os/interface/LayoutItem.h` - Public layout item API
- `src/kits/interface/AbstractLayoutItem.cpp` - Abstract item constraints
- `headers/os/interface/AbstractLayoutItem.h` - Abstract item API
- `src/kits/interface/SpaceLayoutItem.cpp` - Spacing constraints
- `headers/os/interface/SpaceLayoutItem.h` - Space item API

**Grid and Layout Builders:**
- `src/kits/interface/GridLayout.cpp` - Mobile grid layout
- `src/kits/interface/GridLayout.h` - Grid layout headers
- `headers/os/interface/GridLayout.h` - Public grid API
- `src/kits/interface/GridLayoutBuilder.cpp` - Grid builder constraints
- `headers/os/interface/GridLayoutBuilder.h` - Grid builder API
- `src/kits/interface/GroupLayoutBuilder.cpp` - Group builder constraints
- `headers/os/interface/GroupLayoutBuilder.h` - Group builder API

**Layout Utilities:**
- `src/kits/interface/LayoutUtils.cpp` - Mobile layout utilities
- `headers/os/interface/LayoutUtils.h` - Layout utils API
- `src/kits/interface/LayoutContext.cpp` - Mobile layout context
- `headers/os/interface/LayoutContext.h` - Layout context API

### Step 2.2: Responsive Design Support

#### Size Class and Orientation Files
**Screen Management:**
- `src/kits/interface/Screen.cpp` - Size class detection
- `headers/os/interface/Screen.h` - Screen API with size classes
- `headers/private/interface/PrivateScreen.h` - Internal screen state
- `src/servers/app/Screen.cpp` - Server-side screen management
- `src/servers/app/Screen.h` - Server screen headers
- `src/servers/app/ScreenManager.cpp` - Multi-screen mobile support
- `src/servers/app/ScreenManager.h` - Screen manager headers

**View Trait Collections:**
- `src/kits/interface/View.cpp` - Size class awareness (existing)
- `headers/private/interface/ViewPrivate.h` - Size class state (existing)
- `src/kits/interface/Window.cpp` - Orientation handling (existing)
- `headers/private/interface/WindowPrivate.h` - Orientation state (existing)

## Phase 3: Mobile-Specific UI Components

### Step 3.1: Navigation and Tab Systems

#### Navigation Enhancement Files
**Tab System:**
- `src/kits/interface/TabView.cpp` - Mobile tab bar transformation
- `src/kits/interface/TabView.h` - Tab view headers
- `headers/os/interface/TabView.h` - Public tab API
- `headers/private/interface/TabViewPrivate.h` - Tab internals
- `src/kits/interface/Tab.cpp` - Individual tab handling
- `headers/os/interface/Tab.h` - Tab API

**Button Navigation:**
- `src/kits/interface/Button.cpp` - Navigation button styles
- `src/kits/interface/Button.h` - Button headers
- `headers/os/interface/Button.h` - Public button API
- `src/kits/interface/PictureButton.cpp` - Icon button support
- `headers/os/interface/PictureButton.h` - Picture button API

**List View Enhancements:**
- `src/kits/interface/ListView.cpp` - Touch scroll physics
- `src/kits/interface/ListView.h` - List view headers
- `headers/os/interface/ListView.h` - Public list API
- `src/kits/interface/ListItem.cpp` - Touch list item handling
- `headers/os/interface/ListItem.h` - List item API
- `src/kits/interface/StringItem.cpp` - String item touch support
- `headers/os/interface/StringItem.h` - String item API
- `src/kits/interface/OutlineListView.cpp` - Hierarchical touch lists
- `headers/os/interface/OutlineListView.h` - Outline list API

**Scrolling Infrastructure:**
- `src/kits/interface/ScrollView.cpp` - Touch scroll mechanics
- `src/kits/interface/ScrollView.h` - Scroll view headers
- `headers/os/interface/ScrollView.h` - Public scroll API
- `src/kits/interface/ScrollBar.cpp` - Touch scroll bar
- `headers/os/interface/ScrollBar.h` - Scroll bar API

### Step 3.2: Control Adaptations

#### Touch-Optimized Control Files
**Button Controls:**
- `src/kits/interface/Button.cpp` - Touch target sizing (existing)
- `src/kits/interface/CheckBox.cpp` - Touch checkbox
- `src/kits/interface/CheckBox.h` - Checkbox headers
- `headers/os/interface/CheckBox.h` - Public checkbox API
- `src/kits/interface/RadioButton.cpp` - Touch radio button
- `headers/os/interface/RadioButton.h` - Radio button API

**Slider Controls:**
- `src/kits/interface/Slider.cpp` - Touch slider enhancements
- `src/kits/interface/Slider.h` - Slider headers
- `headers/os/interface/Slider.h` - Public slider API
- `src/kits/interface/ChannelSlider.cpp` - Multi-channel touch slider
- `headers/os/interface/ChannelSlider.h` - Channel slider API
- `src/kits/interface/ChannelControl.cpp` - Channel control touch
- `headers/os/interface/ChannelControl.h` - Channel control API

**Text Controls:**
- `src/kits/interface/TextControl.cpp` - Touch text input
- `src/kits/interface/TextControl.h` - Text control headers
- `headers/os/interface/TextControl.h` - Public text control API
- `src/kits/interface/TextView.cpp` - Touch text editing
- `src/kits/interface/TextView.h` - Text view headers
- `headers/os/interface/TextView.h` - Public text view API
- `src/kits/interface/textview_support/TextGapBuffer.cpp` - Touch text buffer (existing)
- `src/kits/interface/textview_support/TextGapBuffer.h` - Touch text buffer headers (existing)
- `src/kits/interface/textview_support/InlineInput.cpp` - Touch inline input (existing)
- `src/kits/interface/textview_support/InlineInput.h` - Touch inline input headers (existing)
- `src/kits/interface/textview_support/UndoBuffer.cpp` - Touch undo support (existing)
- `src/kits/interface/textview_support/UndoBuffer.h` - Touch undo support headers (existing)
- `src/kits/interface/textview_support/StyleBuffer.cpp` - Mobile text styling (existing)
- `src/kits/interface/textview_support/StyleBuffer.h` - Mobile text styling headers (existing)
- `src/kits/interface/textview_support/LineBuffer.cpp` - Mobile line wrapping (existing)
- `src/kits/interface/textview_support/LineBuffer.h` - Mobile line wrapping headers (existing)
- `src/kits/interface/textview_support/WidthBuffer.cpp` - Mobile width calculations (existing)
- `src/kits/interface/textview_support/WidthBuffer.h` - Mobile width calculations headers (existing)

**Control Infrastructure:**
- `src/kits/interface/Control.cpp` - Base touch control behavior
- `src/kits/interface/Control.h` - Control headers
- `headers/os/interface/Control.h` - Public control API
- `headers/private/interface/ControlPrivate.h` - Control internals

## Phase 4: Home Screen and App Management

### Step 4.1: Home Screen Implementation (Deskbar → Springboard)

#### Deskbar Repurposed as Springboard (Remove Desktop Code)
**Transform Deskbar Functions - Delete Desktop, Keep Mobile:**
- `src/apps/deskbar/BarApp.cpp` - REMOVE: desktop bar, minimize/window management. REPURPOSE: fullscreen app launcher with icon grid (existing - MUST READ FULLY)
- `src/apps/deskbar/BarApp.h` - REMOVE: desktop positioning. REPURPOSE: home screen management (existing)  
- `src/apps/deskbar/BarView.cpp` - REMOVE: horizontal bar layout. REPURPOSE: iOS-style icon grid with folders (existing - MUST READ FULLY)
- `src/apps/deskbar/BarView.h` - REMOVE: bar view concepts. REPURPOSE: grid view concepts (existing)
- `src/apps/deskbar/BarWindow.cpp` - REMOVE: deskbar window. REPURPOSE: fullscreen home screen (existing - MUST READ FULLY)
- `src/apps/deskbar/BarWindow.h` - REPURPOSE: home screen window (existing)
- `src/apps/deskbar/TeamMenu.cpp` - App switcher integration (existing)
- `src/apps/deskbar/TeamMenu.h` - Team menu headers (existing)
- `src/apps/deskbar/TeamMenuItem.cpp` - App icon items (existing)
- `src/apps/deskbar/TeamMenuItem.h` - App item headers (existing)
- `src/apps/deskbar/WindowMenu.cpp` - Window management menu (existing)
- `src/apps/deskbar/WindowMenu.h` - Window menu headers (existing)
- `src/apps/deskbar/WindowMenuItem.cpp` - Window menu items (existing)
- `src/apps/deskbar/WindowMenuItem.h` - Window item headers (existing)
- `src/apps/deskbar/DeskbarUtils.cpp` - Springboard utilities (existing)
- `src/apps/deskbar/DeskbarUtils.h` - Utils headers (existing)
- `src/apps/deskbar/deskbar.rdef` - Springboard resources (existing)

**Springboard Features:**
- `src/apps/deskbar/AppIconGrid.cpp` - App icon grid layout (NEW)
- `src/apps/deskbar/AppIconGrid.h` - Grid headers (NEW)
- `src/apps/deskbar/AppFolder.cpp` - App folder management (NEW)
- `src/apps/deskbar/AppFolder.h` - Folder headers (NEW)
- `src/apps/deskbar/SpringboardSearch.cpp` - App search functionality (NEW)
- `src/apps/deskbar/SpringboardSearch.h` - Search headers (NEW)
- `src/apps/deskbar/DockArea.cpp` - Bottom dock implementation (NEW)
- `src/apps/deskbar/DockArea.h` - Dock headers (NEW)

#### Desktop Transformation Files
**Desktop Core:**
- `src/servers/app/Desktop.cpp` - HomeScreen mode implementation (existing)
- `src/servers/app/Desktop.h` - HomeScreen headers (existing)
- `src/servers/app/DesktopSettings.cpp` - Home screen settings (existing)
- `src/servers/app/DesktopSettings.h` - Settings headers (existing)

**Home Screen Files:**
- `src/servers/app/HomeScreenView.cpp` - Home screen grid layout (NEW)
- `src/servers/app/HomeScreenView.h` - Home screen headers (NEW)
- `src/servers/app/AppIconView.cpp` - Individual app icons (NEW)
- `src/servers/app/AppIconView.h` - App icon headers (NEW)
- `src/servers/app/AppFolder.cpp` - App folder implementation (NEW)
- `src/servers/app/AppFolder.h` - App folder headers (NEW)
- `src/servers/app/DockView.cpp` - Dock/favorites area (NEW)
- `src/servers/app/DockView.h` - Dock headers (NEW)

**Wallpaper Management:**
- `src/servers/app/BackgroundView.cpp` - Mobile wallpaper support (MODIFY)
- `src/servers/app/BackgroundView.h` - Background headers (MODIFY)
- `src/preferences/backgrounds/BackgroundsView.cpp` - Mobile wallpaper prefs
- `src/preferences/backgrounds/BackgroundsView.h` - Background prefs headers

#### Icon Management System Files
**Icon Library Extensions:**
- `src/libs/icon/Icon.cpp` - Multi-size icon support
- `src/libs/icon/Icon.h` - Icon headers
- `src/libs/icon/IconRenderer.cpp` - Multiple resolution rendering
- `src/libs/icon/IconRenderer.h` - Renderer headers
- `src/libs/icon/IconUtils.cpp` - Icon utilities for mobile
- `headers/os/interface/IconUtils.h` - Public icon utilities

**Badge System (NEW):**
- `src/libs/icon/IconBadge.cpp` - Notification badge rendering (NEW)
- `src/libs/icon/IconBadge.h` - Badge headers (NEW)

### Step 4.2: App Lifecycle Management

#### Application Framework Files
**App Kit Extensions:**
- `src/kits/app/Application.cpp` - Mobile lifecycle states
- `src/kits/app/Application.h` - Application headers
- `headers/os/app/Application.h` - Public application API
- `src/kits/app/AppDefs.cpp` - Mobile app definitions
- `headers/os/app/AppDefs.h` - App definitions API

**Roster Management:**
- `src/kits/app/Roster.cpp` - Mobile app tracking
- `src/kits/app/Roster.h` - Roster headers
- `headers/os/app/Roster.h` - Public roster API
- `src/kits/app/RosterPrivate.h` - Internal roster state

#### App Switching Interface Files
**Desktop App Switching:**
- `src/servers/app/Desktop.cpp` - App switcher implementation (existing)
- `src/servers/app/AppSwitcher.cpp` - Card-based switcher (NEW)
- `src/servers/app/AppSwitcher.h` - App switcher headers (NEW)
- `src/servers/app/AppPreview.cpp` - App screenshot system (NEW)
- `src/servers/app/AppPreview.h` - Preview headers (NEW)

**Memory and State Management:**
- `src/servers/app/ServerApp.cpp` - App state tracking (existing)
- `src/servers/app/ServerApp.h` - Server app headers (existing)
- `src/kits/app/MessageRunner.cpp` - Background task management
- `src/kits/app/MessageRunner.h` - Message runner headers

## Phase 5: System UI Components

### Step 5.1: Status Bar System

#### Status Bar Implementation Files
**System Status Bar:**
- `src/servers/app/StatusBar.cpp` - System status bar implementation (NEW)
- `src/servers/app/StatusBar.h` - Status bar headers (NEW)
- `src/servers/app/StatusBarView.cpp` - Status bar view (NEW)
- `src/servers/app/StatusBarView.h` - Status bar view headers (NEW)
- `src/servers/app/Desktop.cpp` - Status bar integration (existing)

**Status Indicators:**
- `src/servers/app/TimeIndicator.cpp` - Time display (NEW)
- `src/servers/app/TimeIndicator.h` - Time headers (NEW)
- `src/servers/app/BatteryIndicator.cpp` - Battery status (NEW)
- `src/servers/app/BatteryIndicator.h` - Battery headers (NEW)
- `src/servers/app/NetworkIndicator.cpp` - Network status (NEW)
- `src/servers/app/NetworkIndicator.h` - Network headers (NEW)
- `src/servers/app/NotificationIndicator.cpp` - Notification badges (NEW)
- `src/servers/app/NotificationIndicator.h` - Notification headers (NEW)

#### Notification Center Files
**Notification System:**
- `src/servers/app/NotificationCenter.cpp` - Notification center (NEW)
- `src/servers/app/NotificationCenter.h` - Notification headers (NEW)
- `src/servers/app/NotificationView.cpp` - Individual notifications (NEW)
- `src/servers/app/NotificationView.h` - Notification view headers (NEW)
- `src/servers/app/NotificationGroup.cpp` - Grouped notifications (NEW)
- `src/servers/app/NotificationGroup.h` - Group headers (NEW)

**Quick Settings:**
- `src/servers/app/QuickSettings.cpp` - Quick settings panel (NEW)
- `src/servers/app/QuickSettings.h` - Quick settings headers (NEW)
- `src/servers/app/SettingToggle.cpp` - Individual setting toggles (NEW)
- `src/servers/app/SettingToggle.h` - Toggle headers (NEW)

**Integration with App Kit:**
- `src/kits/app/Notification.cpp` - Notification API
- `src/kits/app/Notification.h` - Notification headers
- `headers/os/app/Notification.h` - Public notification API

### Step 5.2: Modal Presentation

#### Sheet and Modal System Files
**Window Modal Extensions:**
- `src/kits/interface/Window.cpp` - Sheet presentation (existing)
- `src/kits/interface/Alert.cpp` - Mobile alert styling
- `src/kits/interface/Alert.h` - Alert headers
- `headers/os/interface/Alert.h` - Public alert API

**Sheet Implementation:**
- `src/servers/app/ModalSheet.cpp` - Sheet window management (NEW)
- `src/servers/app/ModalSheet.h` - Sheet headers (NEW)
- `src/servers/app/ModalDimmer.cpp` - Background dimming (NEW)
- `src/servers/app/ModalDimmer.h` - Dimmer headers (NEW)

**Popover System:**
- `src/kits/interface/PopoverWindow.cpp` - Popover positioning (NEW)
- `src/kits/interface/PopoverWindow.h` - Popover headers (NEW)
- `headers/os/interface/PopoverWindow.h` - Public popover API (NEW)

**Menu Adaptations:**
- `src/kits/interface/Menu.cpp` - Touch-friendly menus
- `src/kits/interface/Menu.h` - Menu headers
- `headers/os/interface/Menu.h` - Public menu API
- `src/kits/interface/MenuItem.cpp` - Touch menu items
- `headers/os/interface/MenuItem.h` - Menu item API
- `src/kits/interface/PopUpMenu.cpp` - Touch popup menus
- `headers/os/interface/PopUpMenu.h` - Popup menu API

## Phase 6: Rendering and Performance Optimization

### Step 6.1: Mobile-Optimized Rendering

#### Compositor System (NEW) - Floating FPS 5+ (AGG Software Render Compatible)
**Built-in App Server Compositor (MUST READ AppServer.cpp/ServerWindow.cpp FULLY):**
- `src/servers/app/compositor/Compositor.cpp` - Main compositor implementation (NEW)
- `src/servers/app/compositor/Compositor.h` - Compositor headers (NEW)
- `src/servers/app/compositor/CompositorLayer.cpp` - Individual window layers (NEW)
- `src/servers/app/compositor/CompositorLayer.h` - Layer headers (NEW)
- `src/servers/app/compositor/FrameTiming.cpp` - Floating FPS 5+ frame timing for AGG (NEW)
- `src/servers/app/compositor/FrameTiming.h` - Frame timing headers (NEW)
- `src/servers/app/compositor/LayerManager.cpp` - Z-order layer management (NEW)
- `src/servers/app/compositor/LayerManager.h` - Layer manager headers (NEW)

**Compositor Integration (MUST READ FULLY BEFORE MODIFYING):**
- `src/servers/app/AppServer.cpp` - Compositor initialization and main loop (existing)
- `src/servers/app/AppServer.h` - App server compositor integration (existing)
- `src/servers/app/ServerWindow.cpp` - Offscreen rendering to compositor (existing)
- `src/servers/app/ServerWindow.h` - Server window compositor integration (existing)

**Simple Animation Support (CPU-based for AGG compatibility):**
- `src/servers/app/compositor/SimpleAnimator.cpp` - Basic CPU animations (NEW)
- `src/servers/app/compositor/SimpleAnimator.h` - Animator headers (NEW)
- `src/servers/app/compositor/Transform.cpp` - Simple layer transforms (NEW)
- `src/servers/app/compositor/Transform.h` - Transform headers (NEW)

#### Drawing Engine Enhancement Files
**Core Drawing System:**
- `src/servers/app/drawing/DrawingEngine.cpp` - Mobile GPU optimization
- `src/servers/app/drawing/DrawingEngine.h` - Drawing engine headers
- `src/servers/app/drawing/HWInterface.cpp` - Hardware interface mobile optimization
- `src/servers/app/drawing/HWInterface.h` - HW interface headers
- `src/servers/app/drawing/AccelerantHWInterface.cpp` - Mobile accelerant interface
- `src/servers/app/drawing/AccelerantHWInterface.h` - Accelerant headers
- `src/servers/app/drawing/BitmapHWInterface.cpp` - Software fallback optimization
- `src/servers/app/drawing/BitmapHWInterface.h` - Bitmap interface headers

**Painter System:**
- `src/servers/app/drawing/Painter/Painter.cpp` - Mobile painter optimization
- `src/servers/app/drawing/Painter/Painter.h` - Painter headers
- `src/servers/app/drawing/Painter/BitmapPainter.cpp` - Bitmap painting optimization
- `src/servers/app/drawing/Painter/BitmapPainter.h` - Bitmap painter headers
- `src/servers/app/drawing/Painter/drawing_modes/DrawingModeFactory.cpp` - Mobile drawing modes
- `src/servers/app/drawing/Painter/drawing_modes/DrawingModeFactory.h` - Drawing mode headers

**AGG Integration:**
- `src/libs/agg/src/` - All AGG files for mobile optimization
- `src/libs/agg/font_freetype/agg_font_freetype.cpp` - Mobile font rendering

#### Animation System Files
**View Animation:**
- `src/kits/interface/ViewAnimation.cpp` - Hardware-accelerated animations (NEW)
- `src/kits/interface/ViewAnimation.h` - Animation headers (NEW)
- `headers/os/interface/ViewAnimation.h` - Public animation API (NEW)
- `src/kits/interface/View.cpp` - Animation integration (existing)

**Transition System:**
- `src/servers/app/AnimationManager.cpp` - Animation coordinator (NEW)
- `src/servers/app/AnimationManager.h` - Animation headers (NEW)
- `src/servers/app/Animator.cpp` - Individual animators (NEW)
- `src/servers/app/Animator.h` - Animator headers (NEW)

### Step 6.2: Memory and Power Management

#### Resource Management Files
**Bitmap Management:**
- `src/servers/app/BitmapManager.cpp` - Mobile bitmap optimization (existing)
- `src/servers/app/BitmapManager.h` - Bitmap manager headers (existing)
- `src/servers/app/ServerBitmap.cpp` - Server bitmap mobile optimization
- `src/servers/app/ServerBitmap.h` - Server bitmap headers

**Font Management:**
- `src/servers/app/font/FontManager.cpp` - Mobile font caching
- `src/servers/app/font/FontManager.h` - Font manager headers
- `src/servers/app/font/FontEngine.cpp` - Mobile font rendering
- `src/servers/app/font/FontEngine.h` - Font engine headers
- `src/servers/app/font/GlobalFontManager.cpp` - Global font management
- `src/servers/app/font/GlobalFontManager.h` - Global font headers

**Memory Pressure System:**
- `src/servers/app/MemoryManager.cpp` - Memory pressure handling (NEW)
- `src/servers/app/MemoryManager.h` - Memory manager headers (NEW)
- `src/kits/app/Application.cpp` - Memory warning handling (existing)

#### Power Optimization Files
**Power Management:**
- `src/servers/app/PowerManager.cpp` - Battery-aware rendering (NEW)
- `src/servers/app/PowerManager.h` - Power manager headers (NEW)
- `src/servers/app/DisplayManager.cpp` - Display power management (NEW)
- `src/servers/app/DisplayManager.h` - Display headers (NEW)

**View Culling:**
- `src/servers/app/View.cpp` - Visibility culling optimization (existing)
- `src/servers/app/ViewManager.cpp` - View lifecycle management (NEW)
- `src/servers/app/ViewManager.h` - View manager headers (NEW)

## Phase 7: Input Method and Accessibility

### Step 7.1: Virtual Keyboard Integration

#### Virtual Keyboard Mobile Enhancement (Input Server Device)
**Virtual Keyboard Device Modifications (MUST READ FULLY BEFORE MODIFYING):**
- `src/add-ons/input_server/devices/virtualkeyboard/VirtualKeyboardInputDevice.cpp` - Touch input device (existing)
- `src/add-ons/input_server/devices/virtualkeyboard/VirtualKeyboardInputDevice.h` - Device headers (existing)
- `src/add-ons/input_server/devices/virtualkeyboard/VirtualKeyboardWindow.cpp` - Keyboard window (existing)
- `src/add-ons/input_server/devices/virtualkeyboard/VirtualKeyboardWindow.h` - Window headers (existing)
- `src/add-ons/input_server/devices/virtualkeyboard/VirtualKeyboard.rdef` - Resources (existing)
- `src/add-ons/input_server/devices/virtualkeyboard/Jamfile` - Build file (existing)

**Mobile Enhancements (NEW) - Use existing icons and codebase where possible:**
- `src/add-ons/input_server/devices/virtualkeyboard/TouchOptimization.cpp` - Touch layout optimization (NEW)
- `src/add-ons/input_server/devices/virtualkeyboard/TouchOptimization.h` - Touch headers (NEW)

#### Input Server Enhancement Files
**Virtual Keyboard System:**
- `src/servers/input/VirtualKeyboard.cpp` - Virtual keyboard management (NEW)
- `src/servers/input/VirtualKeyboard.h` - Virtual keyboard headers (NEW)
- `src/servers/input/InputServer.cpp` - VK integration (existing)
- `src/servers/input/InputServer.h` - Input server headers (existing)
- `src/servers/input/KeyboardManager.cpp` - Keyboard state management (NEW)
- `src/servers/input/KeyboardManager.h` - Keyboard manager headers (NEW)

**Input Method Framework:**
- `src/add-ons/input_server/methods/VirtualKeyboardMethod.cpp` - VK input method (NEW)
- `src/add-ons/input_server/methods/VirtualKeyboardMethod.h` - VK method headers (NEW)
- `src/add-ons/input_server/methods/TextPrediction.cpp` - Text prediction (NEW)
- `src/add-ons/input_server/methods/TextPrediction.h` - Prediction headers (NEW)

**Layout Avoidance:**
- `src/kits/interface/View.cpp` - Keyboard avoidance (existing)
- `src/kits/interface/Window.cpp` - Window keyboard avoidance (existing)
- `src/servers/app/KeyboardAvoidance.cpp` - Server-side avoidance (NEW)
- `src/servers/app/KeyboardAvoidance.h` - Avoidance headers (NEW)

#### Accessibility Framework Files
**Core Accessibility:**
- `src/kits/interface/AccessibilityManager.cpp` - Accessibility manager (NEW)
- `src/kits/interface/AccessibilityManager.h` - Accessibility headers (NEW)
- `headers/os/interface/AccessibilityManager.h` - Public accessibility API (NEW)
- `src/kits/interface/View.cpp` - Accessibility integration (existing)
- `headers/private/interface/ViewPrivate.h` - Accessibility view state (existing)

**Screen Reading:**
- `src/servers/app/ScreenReader.cpp` - Screen reader service (NEW)
- `src/servers/app/ScreenReader.h` - Screen reader headers (NEW)
- `src/servers/app/AccessibilityServer.cpp` - Accessibility server (NEW)
- `src/servers/app/AccessibilityServer.h` - Accessibility server headers (NEW)

**Dynamic Type Support:**
- `src/kits/interface/Font.cpp` - Dynamic type support
- `src/servers/app/font/FontManager.cpp` - Dynamic font scaling (existing)
- `src/preferences/appearance/FontView.cpp` - Dynamic type preferences

### Step 7.2: Gesture Recognition System

#### Comprehensive Gesture Support Files
**Gesture Recognition:**
- `src/servers/input/GestureRecognizer.cpp` - System gesture recognition (NEW)
- `src/servers/input/GestureRecognizer.h` - Gesture headers (NEW)
- `src/add-ons/input_server/filters/SystemGestureFilter.cpp` - System gestures (NEW)
- `src/add-ons/input_server/filters/SystemGestureFilter.h` - System gesture headers (NEW)
- `src/add-ons/input_server/filters/CustomGestureFilter.cpp` - Custom gestures (NEW)
- `src/add-ons/input_server/filters/CustomGestureFilter.h` - Custom gesture headers (NEW)

**Gesture Configuration:**
- `src/preferences/input/GestureView.cpp` - Gesture preferences (NEW)
- `src/preferences/input/GestureView.h` - Gesture preferences headers (NEW)
- `src/preferences/input/InputWindow.cpp` - Input preferences integration (existing)

**Gesture API:**
- `headers/os/interface/GestureDefs.h` - Gesture definitions (NEW)
- `src/kits/interface/GestureRecognizer.cpp` - Client gesture API (NEW)
- `headers/os/interface/GestureRecognizer.h` - Public gesture API (NEW)

### Step 7.3: Minimal Mobile Decorators (No Theming)

#### Decorator System Mobile Adaptations (MUST READ FULLY BEFORE MODIFYING)
**Existing Decorator System - Use existing codebase maximally:**
- `src/servers/app/decorator/Decorator.cpp` - Base mobile decorator (existing)
- `src/servers/app/decorator/Decorator.h` - Decorator headers (existing)
- `src/servers/app/decorator/DefaultDecorator.cpp` - Mobile default decorator (existing)
- `src/servers/app/decorator/DefaultDecorator.h` - Default decorator headers (existing)
- `src/servers/app/decorator/DecorManager.cpp` - Decorator management (existing)
- `src/servers/app/decorator/DecorManager.h` - Manager headers (existing)

**Minimal Mobile-Specific Decorators (NEW) - Only essential mobile features:**
- `src/servers/app/decorator/MobileDecorator.cpp` - Minimal fullscreen mobile decorator (NEW)
- `src/servers/app/decorator/MobileDecorator.h` - Mobile decorator headers (NEW)

## Phase 8: Developer Tools and APIs

### Step 8.1: Mobile Development Support

#### Interface Builder Enhancement Files
**Visual Design Tool:**
- `src/apps/interface-builder/InterfaceBuilder.cpp` - Visual interface tool (NEW)
- `src/apps/interface-builder/InterfaceBuilder.h` - Interface builder headers (NEW)
- `src/apps/interface-builder/ConstraintEditor.cpp` - Constraint editing (NEW)
- `src/apps/interface-builder/ConstraintEditor.h` - Constraint editor headers (NEW)
- `src/apps/interface-builder/DevicePreview.cpp` - Multi-device preview (NEW)
- `src/apps/interface-builder/DevicePreview.h` - Device preview headers (NEW)
- `src/apps/interface-builder/AssetManager.cpp` - Asset management (NEW)
- `src/apps/interface-builder/AssetManager.h` - Asset manager headers (NEW)

**Icon-O-Matic Extensions:**
- `src/apps/icon-o-matic/` - All existing files for mobile icon support
- `src/apps/icon-o-matic/export/MobileIconExporter.cpp` - Mobile icon export (NEW)
- `src/apps/icon-o-matic/export/MobileIconExporter.h` - Export headers (NEW)

#### Developer API Documentation Files
**API Documentation:**
- `headers/os/interface/` - All headers updated with mobile APIs
- `headers/os/app/` - All headers updated with mobile lifecycle
- `documentation/mobile/MobileAPIGuide.md` - Mobile API documentation (NEW)
- `documentation/mobile/TouchEventGuide.md` - Touch event guide (NEW)
- `documentation/mobile/LayoutGuide.md` - Mobile layout guide (NEW)

### Step 8.2: Testing and Simulation

#### Mobile Testing Framework Files
**Test Infrastructure:**
- `src/tests/kits/interface/mobile/TouchEventTest.cpp` - Touch event testing (NEW)
- `src/tests/kits/interface/mobile/TouchEventTest.h` - Touch test headers (NEW)
- `src/tests/kits/interface/mobile/GestureTest.cpp` - Gesture testing (NEW)
- `src/tests/kits/interface/mobile/GestureTest.h` - Gesture test headers (NEW)
- `src/tests/kits/interface/mobile/LayoutTest.cpp` - Mobile layout testing (NEW)
- `src/tests/kits/interface/mobile/LayoutTest.h` - Layout test headers (NEW)

**Performance Testing:**
- `src/tests/servers/app/mobile/RenderingPerformanceTest.cpp` - Rendering performance (NEW)
- `src/tests/servers/app/mobile/RenderingPerformanceTest.h` - Performance headers (NEW)
- `src/tests/servers/app/mobile/MemoryUsageTest.cpp` - Memory testing (NEW)
- `src/tests/servers/app/mobile/MemoryUsageTest.h` - Memory test headers (NEW)
- `src/tests/servers/app/mobile/BatteryUsageTest.cpp` - Battery testing (NEW)
- `src/tests/servers/app/mobile/BatteryUsageTest.h` - Battery test headers (NEW)

**Simulation Tools:**
- `src/tools/mobile-simulator/MobileSimulator.cpp` - Device simulator (NEW)
- `src/tools/mobile-simulator/MobileSimulator.h` - Simulator headers (NEW)
- `src/tools/mobile-simulator/TouchSimulator.cpp` - Touch simulation (NEW)
- `src/tools/mobile-simulator/TouchSimulator.h` - Touch sim headers (NEW)

## Phase 9: System Integration

### Step 9.1: Boot and System Services

#### NO Mobile Mode Detection Needed
**Mobile is the ONLY mode - no desktop mode preserved**

**Direct Mobile Implementation:**
- `src/servers/app/AppServer.cpp` - Always mobile behavior (existing - MUST READ FULLY)
- `src/apps/deskbar/BarApp.cpp` - Always Springboard mode (existing - MUST READ FULLY)
- All windows always fullscreen by default
- Virtual keyboard always available
- Mobile decorators always active

**NO flags, NO detection, NO dual modes** - just make everything mobile by default.

#### Minimal System Services (QEMU-friendly)
**Power Management - Use Existing Infrastructure:**
- `src/servers/power/power_daemon.cpp` - Extend for mobile states (existing - MUST READ FULLY)
- `src/servers/power/power_monitor.h` - Power monitoring interface (existing)
- `src/servers/power/mobile_power_states.cpp` - Mobile-specific states only (NEW)
- `src/servers/power/mobile_power_states.h` - Mobile power headers (NEW)

**NO Additional Services Needed:**
- **Network**: Use existing Haiku network stack (works in QEMU)
- **Location**: Not needed for QEMU testing
- **Camera**: Not needed for initial mobile port
- **Sensors**: Can simulate with fake data if needed later

**Note:** QEMU runs with standard Haiku networking, no special mobile stack needed. Existing power_daemon handles ACPI, we just add mobile states.

### Step 9.2: Preference and Settings

#### Mobile Settings Application (True Mobile UI - iOS Style Single App)
**Mobile Settings App - Native Mobile UI, NOT launcher for desktop preflets:**
- `src/apps/mobile-settings/MobileSettings.cpp` - Main mobile settings app (NEW)
- `src/apps/mobile-settings/MobileSettings.h` - Settings headers (NEW)
- `src/apps/mobile-settings/SettingsWindow.cpp` - Fullscreen mobile window (NEW)
- `src/apps/mobile-settings/SettingsWindow.h` - Settings window headers (NEW)
- `src/apps/mobile-settings/SettingsListView.cpp` - iOS-style grouped list view (NEW)
- `src/apps/mobile-settings/SettingsListView.h` - List view headers (NEW)
- `src/apps/mobile-settings/SettingsItem.cpp` - Settings row item (NEW)
- `src/apps/mobile-settings/SettingsItem.h` - Item headers (NEW)
- `src/apps/mobile-settings/SettingsDetailView.cpp` - Detail pane view (NEW)
- `src/apps/mobile-settings/SettingsDetailView.h` - Detail headers (NEW)

**Essential Settings Panels (Embedded in mobile app, NOT separate windows):**
- `src/apps/mobile-settings/panels/DisplayPanel.cpp` - Screen brightness/rotation (NEW)
- `src/apps/mobile-settings/panels/DisplayPanel.h` - Display headers (NEW)
- `src/apps/mobile-settings/panels/SoundPanel.cpp` - Volume and sounds (NEW)
- `src/apps/mobile-settings/panels/SoundPanel.h` - Sound headers (NEW)
- `src/apps/mobile-settings/panels/InputPanel.cpp` - Touch/keyboard settings (NEW)
- `src/apps/mobile-settings/panels/InputPanel.h` - Input headers (NEW)
- `src/apps/mobile-settings/panels/WallpaperPanel.cpp` - Background settings (NEW)
- `src/apps/mobile-settings/panels/WallpaperPanel.h` - Wallpaper headers (NEW)
- `src/apps/mobile-settings/panels/TimePanel.cpp` - Date and time (NEW)
- `src/apps/mobile-settings/panels/TimePanel.h` - Time headers (NEW)
- `src/apps/mobile-settings/panels/AppsPanel.cpp` - App management (NEW)
- `src/apps/mobile-settings/panels/AppsPanel.h` - Apps headers (NEW)
- `src/apps/mobile-settings/panels/AboutPanel.cpp` - System info (NEW)
- `src/apps/mobile-settings/panels/AboutPanel.h` - About headers (NEW)

**Reuse Code from Existing Preferences (MUST READ FULLY):**
- Read `src/preferences/screen/ScreenWindow.cpp` for display logic
- Read `src/preferences/sounds/HWindow.cpp` for sound controls
- Read `src/preferences/input/InputWindow.cpp` for input handling
- Read `src/preferences/backgrounds/BackgroundsView.cpp` for wallpaper
- Read `src/preferences/time/TimeWindow.cpp` for time settings

## Build System Integration Files

### Jam Build System Updates
**Core Build Files:**
- `Jamfile` - Root build file mobile integration
- `build/jam/ArchitectureRules` - Mobile architecture support
- `build/jam/MainBuildRules` - Mobile build rules
- `build/jam/HaikuImage` - Mobile image creation
- `build/jam/OptionalPackages` - Mobile package definitions

**Kit Build Files:**
- `src/kits/interface/Jamfile` - Interface kit mobile build
- `src/kits/app/Jamfile` - App kit mobile build
- `src/libs/Jamfile` - Libraries mobile build
- `src/servers/app/Jamfile` - App server mobile build
- `src/servers/input/Jamfile` - Input server mobile build

### Meson Build System Updates
**Mobile Meson Files:**
- `meson.build` - Root mobile meson integration
- `src/kits/interface/meson.build` - Interface kit meson
- `src/kits/app/meson.build` - App kit meson
- `src/kits/meson.build` - All kits coordination
- `src/libs/meson.build` - Libraries meson build
- `src/servers/meson.build` - Servers meson build (NEW)

**Cross-Compilation:**
- `build/meson/haiku-mobile-cross.ini` - Mobile cross-compile config (NEW)
- `build/meson/mobile-toolchain.ini` - Mobile toolchain config (NEW)

### Resource and Asset Files
**Resource Definitions:**
- `src/apps/mobile-settings/MobileSettings.rdef` - Settings app resources (NEW)
- `src/servers/app/app_server_mobile.rdef` - Mobile app server resources (NEW)
- `src/apps/interface-builder/InterfaceBuilder.rdef` - Interface builder resources (NEW)

**Asset Management:**
- `data/artwork/mobile/` - Mobile-specific artwork (NEW)
- `data/artwork/mobile/icons/` - Mobile icon assets (NEW)
- `data/artwork/mobile/wallpapers/` - Mobile wallpapers (NEW)
- `data/system-data/mobile/` - Mobile system data (NEW)





## Additional Critical Mobile Components (Using Existing Code)

### App Lifecycle Management
**Extend existing BApplication system:**
- `src/kits/app/Application.cpp` - Add mobile app state tracking (background/foreground/suspended)
- Use existing `be_app->PostMessage()` system for app lifecycle events
- Extend existing `B_QUIT_REQUESTED` handling for mobile app backgrounding

### Mobile Notification System  
**Use existing notification infrastructure:**
- `src/servers/notification/NotificationServer.cpp` - Extend for mobile banner notifications
- `src/servers/notification/NotificationWindow.cpp` - Adapt for mobile slide-down banners
- `src/kits/app/Notification.cpp` - Add mobile-specific notification categories
- **New:** `src/apps/mobile-notification-center/` - iOS-style notification center app

### App Switching and Recent Apps
**Transform existing Deskbar Switcher:**
- `src/apps/deskbar/Switcher.cpp` - Convert TSwitchManager to mobile app carousel
- `src/apps/deskbar/Switcher.h` - Repurpose for fullscreen app cards view
- Use existing window management in `src/servers/app/Desktop.cpp`

### Screen Orientation Support
**Extend existing graphics system:**
- `src/servers/app/drawing/DrawingEngine.cpp` - Add rotation matrix support
- Use existing orientation handling in `src/kits/interface/SplitLayout.cpp` patterns
- Extend `src/servers/app/Screen.cpp` for orientation events

### Mobile Layout System
**Transform existing layouter infrastructure:**
- `src/kits/interface/layouter/ComplexLayouter.cpp/.h` - Adapt for responsive mobile layouts
- `src/kits/interface/layouter/CollapsingLayouter.cpp/.h` - iOS-style collapsible interfaces
- `src/kits/interface/layouter/LayoutOptimizer.cpp/.h` - Mobile screen optimization
- `src/kits/interface/layouter/Layouter.cpp/.h` - Core mobile layout engine

### Power Management Integration
**Connect mobile compositor to existing power system:**
- Extend `src/servers/power/power_daemon.cpp` for mobile display states
- Use existing ACPI integration for battery/charging states
- Connect mobile compositor frame limiting to power events

## Phase 10: Tracker Mobile Transformation

### Complete Tracker File Manager Conversion
- `src/kits/tracker/DeskWindow.h` - REMOVE: desktop window. REPURPOSE: mobile desk headers
- `src/kits/tracker/DesktopPoseView.cpp` - REMOVE: desktop poses. REPURPOSE: mobile desktop file management
- `src/kits/tracker/DesktopPoseView.h` - REMOVE: desktop view. REPURPOSE: mobile desktop headers
- `src/kits/tracker/QueryPoseView.cpp` - TRANSFORM: mobile search results interface with touch navigation
- `src/kits/tracker/QueryPoseView.h` - TRANSFORM: mobile query view headers
- `src/kits/tracker/VirtualDirectoryPoseView.cpp` - TRANSFORM: cloud storage and virtual folders for mobile
- `src/kits/tracker/VirtualDirectoryPoseView.h` - TRANSFORM: mobile virtual directory headers

**File Operations & Navigation:**
- `src/kits/tracker/FSUtils.cpp` - EXTEND: mobile file system utilities and touch-optimized operations
- `src/kits/tracker/FSUtils.h` - EXTEND: mobile file system headers
- `src/kits/tracker/Navigator.cpp` - TRANSFORM: touch-friendly navigation controls with gesture support
- `src/kits/tracker/Navigator.h` - TRANSFORM: mobile navigation headers
- `src/kits/tracker/DirMenu.cpp` - TRANSFORM: mobile breadcrumb and folder navigation
- `src/kits/tracker/DirMenu.h` - TRANSFORM: mobile directory menu headers
- `src/kits/tracker/MountMenu.cpp` - TRANSFORM: mobile storage device management interface
- `src/kits/tracker/MountMenu.h` - TRANSFORM: mobile mount menu headers

**Search & Organization:**
- `src/kits/tracker/FindPanel.cpp` - REMOVE: desktop find panel. REPURPOSE: mobile file search interface
- `src/kits/tracker/FindPanel.h` - REMOVE: desktop panel. REPURPOSE: mobile search headers
- `src/kits/tracker/QueryContainerWindow.cpp` - TRANSFORM: mobile search results and filter interface
- `src/kits/tracker/QueryContainerWindow.h` - TRANSFORM: mobile query window headers
- `src/kits/tracker/RecentItems.cpp` - TRANSFORM: recent files and quick access for mobile
- `src/kits/tracker/RecentItems.h` - TRANSFORM: mobile recent items headers
- `src/kits/tracker/FavoritesMenu.cpp` - TRANSFORM: mobile bookmarks and favorites interface
- `src/kits/tracker/FavoritesMenu.h` - TRANSFORM: mobile favorites headers

**File Information & Properties:**
- `src/kits/tracker/infowindow/InfoWindow.cpp` - REMOVE: desktop info window. REPURPOSE: mobile file properties
- `src/kits/tracker/infowindow/InfoWindow.h` - REMOVE: desktop window. REPURPOSE: mobile info headers
- `src/kits/tracker/infowindow/GeneralInfoView.cpp` - TRANSFORM: file information for mobile interface
- `src/kits/tracker/infowindow/GeneralInfoView.h` - TRANSFORM: mobile general info headers
- `src/kits/tracker/infowindow/FilePermissionsView.cpp` - TRANSFORM: mobile permissions interface
- `src/kits/tracker/infowindow/FilePermissionsView.h` - TRANSFORM: mobile permissions headers
- `src/kits/tracker/infowindow/AttributesView.cpp` - TRANSFORM: mobile metadata viewer
- `src/kits/tracker/infowindow/AttributesView.h` - TRANSFORM: mobile attributes headers

**Mobile Integration Features:**
- `src/kits/tracker/FilePanel.cpp` - REMOVE: desktop file panel. REPURPOSE: mobile file picker for apps
- `src/kits/tracker/FilePanelPriv.cpp` - REMOVE: desktop panel private. REPURPOSE: mobile picker internals
- `src/kits/tracker/FilePanelPriv.h` - REMOVE: desktop private. REPURPOSE: mobile panel private headers
- `src/kits/tracker/OpenWithWindow.cpp` - REMOVE: desktop open dialog. REPURPOSE: mobile app chooser interface
- `src/kits/tracker/OpenWithWindow.h` - REMOVE: desktop dialog. REPURPOSE: mobile app chooser headers
- `src/kits/tracker/Thumbnails.cpp` - EXTEND: mobile thumbnail generation and caching optimization
- `src/kits/tracker/Thumbnails.h` - EXTEND: mobile thumbnails headers
- `src/kits/tracker/IconCache.cpp` - EXTEND: efficient icon caching for mobile performance
- `src/kits/tracker/IconCache.h` - EXTEND: mobile icon cache headers

**Additional Tracker Files (continuing mobile transformation):**
- `src/kits/tracker/Model.cpp` - TRANSFORM: file model for mobile touch interactions
- `src/kits/tracker/Model.h` - TRANSFORM: mobile file model headers
- `src/kits/tracker/PoseList.cpp` - TRANSFORM: mobile file list management
- `src/kits/tracker/PoseList.h` - TRANSFORM: mobile pose list headers
- `src/kits/tracker/Settings.cpp` - REMOVE: desktop settings. REPURPOSE: mobile file manager preferences
- `src/kits/tracker/Settings.h` - REMOVE: desktop settings. REPURPOSE: mobile settings headers
- `src/kits/tracker/TrackerSettings.cpp` - REMOVE: desktop tracker settings. REPURPOSE: mobile configuration
- `src/kits/tracker/TrackerSettings.h` - REMOVE: desktop config. REPURPOSE: mobile tracker headers
- `src/kits/tracker/TextWidget.cpp` - TRANSFORM: mobile text editing widgets with touch support
- `src/kits/tracker/TextWidget.h` - TRANSFORM: mobile text widget headers
- `src/kits/tracker/TitleView.cpp` - REMOVE: desktop title view. REPURPOSE: mobile header with breadcrumbs
- `src/kits/tracker/TitleView.h` - REMOVE: desktop title. REPURPOSE: mobile title headers

**Mobile File Operations:**
- `src/kits/tracker/FSClipboard.cpp` - EXTEND: mobile file clipboard with touch gestures
- `src/kits/tracker/FSClipboard.h` - EXTEND: mobile clipboard headers
- `src/kits/tracker/FSUndoRedo.cpp` - EXTEND: mobile undo/redo with gesture support
- `src/kits/tracker/FSUndoRedo.h` - EXTEND: mobile undo headers
- `src/kits/tracker/EntryIterator.cpp` - TRANSFORM: efficient mobile file iteration
- `src/kits/tracker/EntryIterator.h` - TRANSFORM: mobile iterator headers
- `src/kits/tracker/NodeWalker.cpp` - TRANSFORM: mobile file tree navigation
- `src/kits/tracker/NodeWalker.h` - TRANSFORM: mobile node walker headers

**Mobile UI Components:**
- `src/kits/tracker/CountView.cpp` - TRANSFORM: mobile file count display
- `src/kits/tracker/CountView.h` - TRANSFORM: mobile count view headers
- `src/kits/tracker/StatusWindow.cpp` - REMOVE: desktop status. REPURPOSE: mobile progress indicators
- `src/kits/tracker/StatusWindow.h` - REMOVE: desktop status. REPURPOSE: mobile status headers
- `src/kits/tracker/Utilities.cpp` - EXTEND: mobile file utilities and helpers
- `src/kits/tracker/Utilities.h` - EXTEND: mobile utilities headers
- `src/kits/tracker/Shortcuts.cpp` - REMOVE: desktop shortcuts. REPURPOSE: mobile quick actions
- `src/kits/tracker/Shortcuts.h` - REMOVE: desktop shortcuts. REPURPOSE: mobile shortcuts headers

**Mobile Menu Systems:**
- `src/kits/tracker/NavMenu.cpp` - TRANSFORM: mobile navigation menu with touch support
- `src/kits/tracker/SlowMenu.cpp` - TRANSFORM: mobile context menus
- `src/kits/tracker/SlowMenu.h` - TRANSFORM: mobile slow menu headers
- `src/kits/tracker/TemplatesMenu.cpp` - TRANSFORM: mobile file templates
- `src/kits/tracker/TemplatesMenu.h` - TRANSFORM: mobile templates headers
- `src/kits/tracker/LiveMenu.cpp` - TRANSFORM: mobile live folder menus
- `src/kits/tracker/LiveMenu.h` - TRANSFORM: mobile live menu headers
- `src/kits/tracker/GroupedMenu.cpp` - TRANSFORM: mobile grouped context menus
- `src/kits/tracker/GroupedMenu.h` - TRANSFORM: mobile grouped menu headers

**Additional Mobile Components:**
- `src/kits/tracker/BackgroundImage.cpp` - REMOVE: desktop wallpaper. REPURPOSE: mobile folder backgrounds
- `src/kits/tracker/BackgroundImage.h` - REMOVE: desktop background. REPURPOSE: mobile background headers
- `src/kits/tracker/Bitmaps.cpp` - EXTEND: mobile-optimized bitmap handling
- `src/kits/tracker/Bitmaps.h` - EXTEND: mobile bitmap headers
- `src/kits/tracker/ViewState.cpp` - REMOVE: desktop view state. REPURPOSE: mobile view preferences
- `src/kits/tracker/ViewState.h` - REMOVE: desktop state. REPURPOSE: mobile view state headers
- `src/kits/tracker/SelectionWindow.cpp` - REMOVE: desktop selection. REPURPOSE: mobile multi-select interface
- `src/kits/tracker/SelectionWindow.h` - REMOVE: desktop selection. REPURPOSE: mobile selection headers
- `src/kits/tracker/OverrideAlert.cpp` - TRANSFORM: mobile file conflict resolution
- `src/kits/tracker/OverrideAlert.h` - TRANSFORM: mobile override headers
- `src/kits/tracker/TrashWatcher.cpp` - EXTEND: mobile trash management
- `src/kits/tracker/TrashWatcher.h` - EXTEND: mobile trash headers

**Mobile Virtual Directory Support:**
- `src/kits/tracker/VirtualDirectoryEntryList.cpp` - EXTEND: mobile virtual directory support
- `src/kits/tracker/VirtualDirectoryEntryList.h` - EXTEND: mobile virtual entry headers
- `src/kits/tracker/VirtualDirectoryManager.cpp` - EXTEND: mobile virtual directory management
- `src/kits/tracker/VirtualDirectoryManager.h` - EXTEND: mobile virtual manager headers
- `src/kits/tracker/VirtualDirectoryWindow.cpp` - REMOVE: desktop virtual window. REPURPOSE: mobile virtual folders
- `src/kits/tracker/VirtualDirectoryWindow.h` - REMOVE: desktop virtual. REPURPOSE: mobile virtual headers

**Mobile Widget Support:**
- `src/kits/tracker/WidgetAttributeText.cpp` - TRANSFORM: mobile attribute display widgets
- `src/kits/tracker/WidgetAttributeText.h` - TRANSFORM: mobile widget headers
- `src/kits/tracker/MiniMenuField.cpp` - TRANSFORM: mobile compact menu fields
- `src/kits/tracker/MiniMenuField.h` - TRANSFORM: mobile mini menu headers
- `src/kits/tracker/DialogPane.cpp` - REMOVE: desktop dialog panes. REPURPOSE: mobile dialog fragments
- `src/kits/tracker/DialogPane.h` - REMOVE: desktop pane. REPURPOSE: mobile dialog headers

**Additional System Integration:**
- `src/kits/tracker/AutoMounterSettings.cpp` - TRANSFORM: mobile storage auto-mounting
- `src/kits/tracker/AttributeStream.cpp` - EXTEND: mobile file attributes handling
- `src/kits/tracker/AttributeStream.h` - EXTEND: mobile attribute stream headers
- `src/kits/tracker/DraggableContainerIcon.cpp` - TRANSFORM: mobile drag and drop for files
- `src/kits/tracker/DraggableContainerIcon.h` - TRANSFORM: mobile draggable headers
- `src/kits/tracker/IconMenuItem.cpp` - TRANSFORM: mobile icon menu items
- `src/kits/tracker/MimeTypeList.cpp` - EXTEND: mobile MIME type handling
- `src/kits/tracker/MimeTypeList.h` - EXTEND: mobile MIME headers
- `src/kits/tracker/NodePreloader.cpp` - EXTEND: mobile file preloading optimization
- `src/kits/tracker/NodePreloader.h` - EXTEND: mobile preloader headers
- `src/kits/tracker/PendingNodeMonitorCache.cpp` - EXTEND: mobile file monitoring cache
- `src/kits/tracker/PendingNodeMonitorCache.h` - EXTEND: mobile monitor cache headers
- `src/kits/tracker/TaskLoop.cpp` - EXTEND: mobile background task processing
- `src/kits/tracker/TaskLoop.h` - EXTEND: mobile task loop headers
- `src/kits/tracker/TrackerString.cpp` - EXTEND: mobile string handling utilities
- `src/kits/tracker/TrackerString.h` - EXTEND: mobile string headers

## Phase 11: System Preferences Integration

### Step 11.1: Complete Preferences System Mobile Transformation

#### All Preference Applications Must Be Integrated (MUST READ FULLY BEFORE MODIFYING)
**Desktop preferences to be integrated into Mobile Settings app:**

**Appearance Preferences (`src/preferences/appearance/`):**
- `src/preferences/appearance/Appearance.cpp` - REMOVE: desktop window. REPURPOSE: mobile appearance settings panel (existing - MUST READ FULLY)
- `src/preferences/appearance/AppearanceWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile settings integration (existing)
- `src/preferences/appearance/FontView.cpp` - TRANSFORM: mobile dynamic type settings (existing - MUST READ FULLY)
- `src/preferences/appearance/FontSelectionView.cpp` - TRANSFORM: mobile font picker interface (existing)
- `src/preferences/appearance/ColorsView.cpp` - TRANSFORM: mobile color scheme settings (existing)
- `src/preferences/appearance/AntialiasingSettingsView.cpp` - TRANSFORM: mobile font rendering settings (existing)
- `src/preferences/appearance/LookAndFeelSettingsView.cpp` - TRANSFORM: mobile UI theme settings (existing)

**Background Preferences (`src/preferences/backgrounds/`):**
- `src/preferences/backgrounds/Backgrounds.cpp` - REMOVE: desktop app. REPURPOSE: mobile wallpaper settings (existing - MUST READ FULLY)
- `src/preferences/backgrounds/BackgroundsView.cpp` - TRANSFORM: mobile wallpaper selection interface (existing - MUST READ FULLY)
- `src/preferences/backgrounds/BackgroundImage.cpp` - TRANSFORM: mobile wallpaper management (existing)
- `src/preferences/backgrounds/ImageFilePanel.cpp` - TRANSFORM: mobile image picker (existing)

**Screen Preferences (`src/preferences/screen/`):**
- `src/preferences/screen/ScreenApplication.cpp` - REMOVE: desktop app. REPURPOSE: mobile display settings (existing - MUST READ FULLY)
- `src/preferences/screen/ScreenWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile display panel (existing - MUST READ FULLY)
- `src/preferences/screen/ScreenSettings.cpp` - TRANSFORM: mobile display configuration (existing)
- `src/preferences/screen/MonitorView.cpp` - TRANSFORM: mobile screen orientation controls (existing)
- `src/preferences/screen/RefreshSlider.cpp` - TRANSFORM: mobile brightness controls (existing)

**Input Preferences (`src/preferences/input/`):**
- `src/preferences/input/Input.cpp` - REMOVE: desktop app. REPURPOSE: mobile input settings (existing - MUST READ FULLY)
- `src/preferences/input/InputWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile input panel (existing - MUST READ FULLY)
- `src/preferences/input/InputMouse.cpp` - TRANSFORM: mobile touch sensitivity settings (existing)
- `src/preferences/input/InputKeyboard.cpp` - TRANSFORM: mobile virtual keyboard settings (existing)
- `src/preferences/input/InputTouchpadPref.cpp` - TRANSFORM: mobile gesture settings (existing)
- `src/preferences/input/KeyboardView.cpp` - TRANSFORM: mobile keyboard configuration (existing)
- `src/preferences/input/MouseView.cpp` - TRANSFORM: mobile touch settings view (existing)

**Sound Preferences (`src/preferences/sounds/`):**
- `src/preferences/sounds/HApp.cpp` - REMOVE: desktop app. REPURPOSE: mobile sound settings (existing - MUST READ FULLY)
- `src/preferences/sounds/HWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile sound panel (existing - MUST READ FULLY)
- `src/preferences/sounds/HEventList.cpp` - TRANSFORM: mobile notification sounds (existing)
- `src/preferences/sounds/SoundFilePanel.cpp` - TRANSFORM: mobile sound picker (existing)

**Time Preferences (`src/preferences/time/`):**
- `src/preferences/time/Time.cpp` - REMOVE: desktop app. REPURPOSE: mobile date/time settings (existing - MUST READ FULLY)
- `src/preferences/time/TimeWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile time panel (existing - MUST READ FULLY)
- `src/preferences/time/TimeSettings.cpp` - TRANSFORM: mobile time configuration (existing)
- `src/preferences/time/ClockView.cpp` - TRANSFORM: mobile clock display settings (existing)
- `src/preferences/time/AnalogClock.cpp` - TRANSFORM: mobile analog clock widget (existing)
- `src/preferences/time/DateTimeView.cpp` - TRANSFORM: mobile date/time picker (existing)
- `src/preferences/time/NetworkTimeView.cpp` - TRANSFORM: mobile network time sync (existing)
- `src/preferences/time/ZoneView.cpp` - TRANSFORM: mobile timezone selection (existing)

**Network Preferences (`src/preferences/network/`):**
- `src/preferences/network/Network.cpp` - REMOVE: desktop app. REPURPOSE: mobile network settings (existing - MUST READ FULLY)
- `src/preferences/network/NetworkWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile network panel (existing)
- `src/preferences/network/InterfaceView.cpp` - TRANSFORM: mobile WiFi management (existing)
- `src/preferences/network/InterfaceAddressView.cpp` - TRANSFORM: mobile IP configuration (existing)
- `src/preferences/network/IPAddressControl.cpp` - TRANSFORM: mobile IP address input (existing)

**Media Preferences (`src/preferences/media/`):**
- `src/preferences/media/Media.cpp` - REMOVE: desktop app. REPURPOSE: mobile media settings (existing - MUST READ FULLY)
- `src/preferences/media/MediaWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile media panel (existing)
- `src/preferences/media/MediaViews.cpp` - TRANSFORM: mobile media device management (existing)
- `src/preferences/media/MidiSettingsView.cpp` - TRANSFORM: mobile MIDI configuration (existing)

**Keyboard/Keymap Preferences (`src/preferences/keymap/`):**
- `src/preferences/keymap/Keymap.cpp` - REMOVE: desktop app. REPURPOSE: mobile keyboard layout (existing - MUST READ FULLY)
- `src/preferences/keymap/KeymapWindow.cpp` - REMOVE: desktop window. REPURPOSE: mobile keymap panel (existing)
- `src/preferences/keymap/KeyboardLayoutView.cpp` - TRANSFORM: mobile layout visualization (existing)
- `src/preferences/keymap/KeyboardLayout.cpp` - TRANSFORM: mobile layout management (existing)

**Additional Preferences (Mobile Integration Required):**
- `src/preferences/bluetooth/` (12 files) - Mobile Bluetooth settings
- `src/preferences/locale/` (6 files) - Mobile localization settings  
- `src/preferences/mail/` (15 files) - Mobile email account setup
- `src/preferences/screensaver/` (7 files) - Mobile screensaver/lock screen
- `src/preferences/shortcuts/` (8 files) - Mobile gesture shortcuts
- `src/preferences/filetypes/` (20 files) - Mobile file associations
- `src/preferences/virtualmemory/` (4 files) - Mobile memory management
- `src/preferences/printers/` (12 files) - Mobile printing (if needed)
- `src/preferences/repositories/` (11 files) - Mobile software repositories
- `src/preferences/notifications/` (10 files) - Mobile notification settings

**New Mobile Settings Panels (Extended):**
- `src/apps/mobile-settings/panels/GeneralPanel.cpp` - General mobile settings (NEW)
- `src/apps/mobile-settings/panels/PrivacyPanel.cpp` - Privacy and security (NEW)
- `src/apps/mobile-settings/panels/StoragePanel.cpp` - Storage management (NEW)
- `src/apps/mobile-settings/panels/BatteryPanel.cpp` - Battery and power settings (NEW)
- `src/apps/mobile-settings/panels/AccessibilityPanel.cpp` - Accessibility features (NEW)

## Phase 12: System Resources and Assets

### Step 12.1: Mobile Icon System

#### Complete Icon Adaptation (`data/artwork/icons/` - 200+ icons)
**All system icons require mobile adaptation:**
- **Application Icons** (57 icons): App_Mail, App_Terminal, App_Tracker, etc. - TRANSFORM: mobile app icon sizes and styles
- **Device Icons** (25 icons): Device_Harddisk, Device_USB, Device_Mobile, etc. - TRANSFORM: mobile device representation
- **File Type Icons** (35 icons): File_Audio, File_Video, File_Text, etc. - TRANSFORM: mobile file preview icons
- **Action Icons** (20 icons): Action_Download, Action_Search, Action_Stop, etc. - TRANSFORM: mobile toolbar icons
- **System Icons** (30 icons): Server_App, System_Kernel, Trash_Empty, etc. - TRANSFORM: mobile system indicators
- **Preference Icons** (25 icons): Prefs_Appearance, Prefs_Network, etc. - TRANSFORM: mobile settings icons

**New Mobile Icon Categories:**
- `data/artwork/mobile/icons/navigation/` - Back, forward, menu, search icons (NEW)
- `data/artwork/mobile/icons/status/` - Battery, signal, WiFi status icons (NEW)
- `data/artwork/mobile/icons/controls/` - Touch controls and gesture icons (NEW)
- `data/artwork/mobile/icons/widgets/` - Widget and home screen icons (NEW)

### Step 12.2: Mobile Cursors and Visual Assets

#### Mobile Cursor System (`data/artwork/cursors/` - 25+ cursors)
**Transform all cursors for mobile/touch interface:**
- `data/artwork/cursors/Pointer` - TRANSFORM: mobile touch pointer
- `data/artwork/cursors/IBeam` - TRANSFORM: mobile text selection cursor
- `data/artwork/cursors/Grab` - TRANSFORM: mobile drag and drop cursor
- `data/artwork/cursors/ResizeNorthSouth` - TRANSFORM: mobile resize indicators
- All resize cursors (8 cursors) - TRANSFORM: mobile resize handles
- Context menu and action cursors (10 cursors) - TRANSFORM: mobile action indicators

**New Mobile Visual Assets:**
- `data/artwork/mobile/gestures/` - Gesture indication graphics (NEW)
- `data/artwork/mobile/animations/` - Touch feedback animations (NEW)
- `data/artwork/mobile/backgrounds/` - Default mobile wallpapers (NEW)

## Phase 13: Add-ons Mobile Adaptation

### Step 13.1: Mobile Look and Feel

#### Control Look Add-ons (`src/add-ons/control_look/`)
**Transform existing control look for mobile:**
- `src/add-ons/control_look/BeControlLook/BeControlLook.cpp` - TRANSFORM: mobile Be-style controls (existing - MUST READ FULLY)
- `src/add-ons/control_look/FlatControlLook/FlatControlLook.cpp` - TRANSFORM: mobile flat design controls (existing - MUST READ FULLY)

**New Mobile Control Look:**
- `src/add-ons/control_look/MobileControlLook/MobileControlLook.cpp` - Mobile-optimized control rendering (NEW)
- `src/add-ons/control_look/MobileControlLook/MobileControlLook.h` - Mobile control look headers (NEW)
- `src/add-ons/control_look/MobileControlLook/TouchControls.cpp` - Touch-optimized control sizing (NEW)

#### Mobile Decorators (`src/add-ons/decorators/`)
**Transform existing decorators for mobile:**
- `src/add-ons/decorators/BeDecorator/BeDecorator.cpp` - REMOVE: desktop decorations. REPURPOSE: mobile status indicators (existing)
- `src/add-ons/decorators/FlatDecorator/FlatDecorator.cpp` - REMOVE: desktop decorations. REPURPOSE: mobile flat design (existing)

**New Mobile Decorators:**
- `src/add-ons/decorators/MobileDecorator/MobileDecorator.cpp` - Fullscreen mobile app decoration (NEW)
- `src/add-ons/decorators/MobileDecorator/StatusBarDecorator.cpp` - Mobile status bar integration (NEW)

### Step 13.2: Mobile-Adapted Translators

#### Image Translators Mobile Optimization (`src/add-ons/translators/`)
**Optimize existing translators for mobile:**
- `src/add-ons/translators/jpeg/JPEGTranslator.cpp` - EXTEND: mobile image optimization (existing)
- `src/add-ons/translators/png/PNGTranslator.cpp` - EXTEND: mobile PNG handling (existing)
- `src/add-ons/translators/gif/GIFTranslator.cpp` - EXTEND: mobile GIF support (existing)
- `src/add-ons/translators/webp/WebPTranslator.cpp` - EXTEND: mobile WebP optimization (existing)
- `src/add-ons/translators/avif/AVIFTranslator.cpp` - EXTEND: mobile AVIF support (existing)

**Media Translators Mobile Support:**
- Audio/video translators for mobile media consumption
- Thumbnail generation optimization for mobile galleries
- Memory-efficient image processing for mobile devices

## Phase 14: System Libraries Mobile Enhancement

### Step 14.1: Essential Libraries Mobile Adaptation

#### Icon Library Mobile Extensions (`src/libs/icon/` - existing)
**Already included in main plan - no changes needed**

#### ALM (Auckland Layout Manager) Mobile Support (`src/libs/alm/`)
**Transform for mobile constraint-based layouts:**
- `src/libs/alm/ALMLayout.cpp` - EXTEND: mobile layout constraints (existing - MUST READ FULLY)
- `src/libs/alm/ALMLayoutBuilder.cpp` - EXTEND: mobile layout builder (existing)
- `src/libs/alm/Area.cpp` - EXTEND: mobile area management (existing)
- `src/libs/alm/Column.cpp` - EXTEND: mobile column constraints (existing)
- `src/libs/alm/Row.cpp` - EXTEND: mobile row constraints (existing)

#### Print Library Mobile Adaptation (`src/libs/print/libprint/`)
**Mobile printing support (optional):**
- `src/libs/print/libprint/PrintProcess.cpp` - EXTEND: mobile print jobs (existing)
- `src/libs/print/libprint/JobSetupDlg.cpp` - TRANSFORM: mobile print dialog (existing)
- AirPrint and mobile printing protocol support integration

## Phase 15: Localization and Accessibility

### Step 15.1: Mobile Localization System

#### Catalog System Mobile Adaptation (`data/catalogs/`)
**Extend existing localization for mobile:**
- All existing catalog files must be reviewed for mobile context
- Mobile-specific strings for touch interactions
- Gesture terminology localization
- Mobile UI element descriptions

**New Mobile Catalog Categories:**
- `data/catalogs/mobile/gestures/` - Gesture action descriptions (NEW)
- `data/catalogs/mobile/accessibility/` - Mobile accessibility strings (NEW)
- `data/catalogs/mobile/tutorials/` - First-time user guidance (NEW)

### Step 15.2: Mobile Accessibility Framework

#### VoiceOver-Style Screen Reading
**Extend existing accessibility infrastructure:**
- Integration with existing Haiku accessibility system
- Mobile screen reader optimization
- Touch-based navigation support
- Voice control integration

## Phase 16: Launch System Mobile Integration

### Step 16.1: Launch Daemon Mobile Adaptation

#### Critical System Service Management (`src/servers/launch/`)
**Launch daemon manages all system service startup and must be mobile-optimized:**

**Core Launch System Files (MUST READ FULLY BEFORE MODIFYING):**
- `src/servers/launch/LaunchDaemon.cpp` - EXTEND: mobile service lifecycle management (existing - MUST READ FULLY)
- `src/servers/launch/LaunchDaemon.h` - EXTEND: mobile service coordination headers (existing)
- `src/servers/launch/Target.cpp` - TRANSFORM: mobile service targets and dependencies (existing - MUST READ FULLY)
- `src/servers/launch/Target.h` - TRANSFORM: mobile target management headers (existing)
- `src/servers/launch/Job.cpp` - EXTEND: mobile job execution with power management (existing - MUST READ FULLY)
- `src/servers/launch/Job.h` - EXTEND: mobile job headers (existing)
- `src/servers/launch/Worker.cpp` - TRANSFORM: mobile background worker management (existing)
- `src/servers/launch/Worker.h` - TRANSFORM: mobile worker headers (existing)

**Mobile Service Configuration:**
- `src/servers/launch/Conditions.cpp` - EXTEND: mobile-specific service conditions (existing - MUST READ FULLY)
- `src/servers/launch/Conditions.h` - EXTEND: mobile condition headers (existing)
- `src/servers/launch/Events.cpp` - EXTEND: mobile event handling and power events (existing)
- `src/servers/launch/Events.h` - EXTEND: mobile event headers (existing)
- `src/servers/launch/SettingsParser.cpp` - EXTEND: mobile service settings parsing (existing)
- `src/servers/launch/SettingsParser.h` - EXTEND: mobile settings headers (existing)

**Mobile Resource Management:**
- `src/servers/launch/BaseJob.cpp` - EXTEND: mobile job resource constraints (existing)
- `src/servers/launch/BaseJob.h` - EXTEND: mobile base job headers (existing)
- `src/servers/launch/Utility.cpp` - EXTEND: mobile launch utilities (existing)
- `src/servers/launch/Utility.h` - EXTEND: mobile utility headers (existing)
- `src/servers/launch/Log.cpp` - EXTEND: mobile service logging (existing)
- `src/servers/launch/Log.h` - EXTEND: mobile log headers (existing)

**Mobile System Jobs:**
- `src/servers/launch/InitRealTimeClockJob.cpp` - EXTEND: mobile time synchronization (existing)
- `src/servers/launch/InitRealTimeClockJob.h` - EXTEND: mobile time job headers (existing)
- `src/servers/launch/InitSharedMemoryDirectoryJob.cpp` - EXTEND: mobile shared memory setup (existing)
- `src/servers/launch/InitSharedMemoryDirectoryJob.h` - EXTEND: mobile shared memory headers (existing)
- `src/servers/launch/InitTemporaryDirectoryJob.cpp` - EXTEND: mobile temp directory management (existing)
- `src/servers/launch/InitTemporaryDirectoryJob.h` - EXTEND: mobile temp headers (existing)
- `src/servers/launch/AbstractEmptyDirectoryJob.cpp` - EXTEND: mobile directory cleanup (existing)
- `src/servers/launch/AbstractEmptyDirectoryJob.h` - EXTEND: mobile cleanup headers (existing)

**Mobile System Monitoring:**
- `src/servers/launch/FileWatcher.cpp` - EXTEND: mobile file system monitoring (existing)
- `src/servers/launch/FileWatcher.h` - EXTEND: mobile file watcher headers (existing)
- `src/servers/launch/NetworkWatcher.cpp` - EXTEND: mobile network change monitoring (existing)
- `src/servers/launch/NetworkWatcher.h` - EXTEND: mobile network headers (existing)
- `src/servers/launch/VolumeWatcher.cpp` - EXTEND: mobile storage monitoring (existing)
- `src/servers/launch/VolumeWatcher.h` - EXTEND: mobile volume headers (existing)

## Phase 17: System Configuration Mobile Adaptation

### Step 17.1: Mobile System Settings Integration

#### Essential System Configuration Files (`data/settings/`)
**Core mobile system configuration support:**

**Network Configuration Mobile Support:**
- `data/settings/network/hosts` - EXTEND: mobile network configuration (existing - MUST READ FULLY)
- `data/settings/network/services` - EXTEND: mobile service definitions (existing - MUST READ FULLY)

**Kernel Configuration Mobile Support:**
- `data/settings/kernel/drivers/kernel` - EXTEND: mobile kernel driver configuration (existing)
- `data/settings/kernel/drivers/vesa` - EXTEND: mobile display driver settings (existing)

**System Configuration Files:**
- `data/settings/first_login` - TRANSFORM: mobile first-time setup experience (existing)
- `data/settings/shortcuts_settings` - TRANSFORM: mobile gesture and shortcut configuration (existing)

**Media Configuration Mobile Support:**
- `data/settings/media/usb_vision/` - EXTEND: mobile camera and media device configuration (existing)
- All locale files in `data/settings/media/usb_vision/Locales/` - EXTEND: mobile media localization (existing)

**New Mobile Configuration Files:**
- `data/settings/mobile/touch_sensitivity` - Mobile touch calibration settings (NEW)
- `data/settings/mobile/gesture_preferences` - Mobile gesture configuration (NEW)
- `data/settings/mobile/power_management` - Mobile power management settings (NEW)
- `data/settings/mobile/orientation_lock` - Screen orientation preferences (NEW)
- `data/settings/mobile/notification_settings` - Mobile notification configuration (NEW)

## Conclusion

This comprehensive mobile UI redesign plan transforms Haiku OS into a modern touch-first mobile operating system. The plan covers 465 files across 17 phases, transforming everything from low-level input handling to high-level user interfaces while preserving Haiku's architectural integrity.
