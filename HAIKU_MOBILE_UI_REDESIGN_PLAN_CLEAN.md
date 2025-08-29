# Haiku Mobile UI Redesign Plan - iOS-Inspired Architecture

## Executive Summary

This document outlines a comprehensive plan to redesign Haiku's interface for mobile operating systems, using iOS as reference. The approach completely removes desktop functionality and repurposes existing Haiku architecture for mobile-only use.

## Design Philosophy

### Core Architectural Principles
1. **Mobile-Only Approach**: Remove all desktop code completely - mobile is the ONLY mode
2. **Preserve Architecture, Change Purpose**: Keep Haiku's BView/BWindow hierarchy but repurpose all functions for mobile use
3. **Transform Function Meaning**: Existing functions get new mobile semantics while maintaining architectural integrity
4. **Minimal New Classes**: Extend existing classes rather than create parallel systems
5. **QEMU Compatibility**: Everything runs on x86_64 QEMU without special hardware requirements

### Target Mobile Features (iOS-Inspired)
- Fullscreen app paradigm (always)
- Touch gesture recognition
- Home screen with app icons (Deskbar → Springboard)
- Status bar and notification center
- App switching/multitasking
- Modal presentation system
- Auto-layout responsive design

## Complete File Summary

### **Total Files Affected: 514 files**

### Files Modified (Existing): 369 files
**Interface Kit (src/kits/interface/)**: 58 files
- Core classes: View, Window, Layout system (47 files)
- Mobile layouter system: ComplexLayouter, CollapsingLayouter, LayoutOptimizer (7 files) 
- Text input system: InlineInput, TextGapBuffer, UndoBuffer, etc. (11 files)

**App Kit (src/kits/app/)**: 28 files
- Application lifecycle, BApplication, Roster, Notification system

**App Server (src/servers/app/)**: 78 files
- Desktop, Window, View, Drawing system, Screen management
- Notification integration, existing compositor hooks

**Input Server (src/servers/input/)**: 8 files
- Touch input, gesture recognition, virtual keyboard integration

**Deskbar → Springboard**: 16 files
- Transform existing deskbar into mobile home screen

**VirtualKeyboard Device**: 6 files
- Location: `src/add-ons/input_server/devices/virtualkeyboard/`

**Power Management**: 3 files
- Extend existing `src/servers/power/power_daemon.cpp`

**Decorator System**: 6 files
- Minimal changes to existing decorators

**Existing Preferences**: 20 files
- Reuse logic from existing preference panels

**Mail Application Mobile Adaptation**: 23 files
- Transform existing Mail app to mobile-first email client

**Clock Application Mobile Widgets**: 6 files
- Transform Clock app to mobile world clock and timer widgets

**Workspaces Mobile App Switcher**: 3 files  
- Transform Workspaces app to iOS-style app switcher and recent apps

**Switcher Edge Gesture System**: 14 files
- Transform Switcher app to mobile edge gestures and quick app access

**Tracker Mobile File Manager**: 99 files
- Transform Tracker to iOS Files-style mobile file manager

**Supporting Libraries**: 8 files

### Files Created (New): 95 files
**Mobile Compositor System**: 10 files
- Floating 5+ FPS for AGG software rendering compatibility

**Mobile UI Components**: 35 files
- Status bar, home screen features, app switching

**Mobile Settings App**: 25 files
- True iOS-style mobile settings (not desktop preflet launcher)

**Mobile Power States**: 2 files
- Extend existing power_daemon

**Testing Infrastructure**: 15 files

**Developer Tools**: 8 files

### Build System Files: 34 files
**Jam Build Files**: 20 files
**Meson Build Files**: 14 files

## Implementation Strategy

### Phase 1: Core Infrastructure (8-12 weeks)
1. **Touch Input System**
   - Modify `src/servers/input/InputServer.cpp` for touch support
   - Create touch gesture filters in `src/add-ons/input_server/filters/`
   - Extend `src/kits/interface/View.cpp` with touch event methods

2. **Mobile Window Management**
   - **REMOVE** all desktop window behaviors from `src/kits/interface/Window.cpp`
   - **REPURPOSE** BWindow constructor to default to fullscreen
   - Transform `src/servers/app/Window.cpp` for mobile app lifecycle

3. **Layout System Enhancement**
   - Transform existing layouter system for responsive mobile design
   - Enhance `src/kits/interface/layouter/` for mobile constraints
   - Optimize text input system in `src/kits/interface/textview_support/`

### Phase 2: Mobile UI Components (10-16 weeks)
1. **Home Screen (Deskbar → Springboard)**
   - **REMOVE** desktop bar concepts from `src/apps/deskbar/BarApp.cpp`
   - **REPURPOSE** as fullscreen app launcher with icon grid
   - Transform existing team management into app management

2. **Mobile Controls**
   - Adapt existing controls (Button, ListView, ScrollView) for touch
   - Implement mobile-specific gesture support
   - Create touch-optimized input methods

3. **Mobile Compositor**
   - Build compositor system in `src/servers/app/compositor/`
   - Floating 5+ FPS for AGG software rendering
   - Layer management for mobile apps

### Phase 3: System Integration (8-12 weeks)
1. **Mobile Notification System**
   - Extend existing `src/servers/notification/NotificationServer.cpp`
   - Create mobile notification center app
   - Transform existing Deskbar switcher for app carousel

2. **Mobile Settings App**
   - Create proper mobile settings app (not desktop preflet launcher)
   - iOS-style grouped lists and navigation
   - Embedded settings panels, not separate windows

3. **Power Management Integration**
   - Extend existing `src/servers/power/power_daemon.cpp`
   - Connect mobile compositor to power events
   - Screen orientation support

### Phase 4: Mobile Applications (6-8 weeks)
1. **Mobile Mail Application**
   - Transform existing Mail app (`src/apps/mail/`) to iOS-style mobile email client
   - Touch-optimized email composition with swipe gestures
   - Mobile conversation view and attachment handling
   - Push notification integration for new emails

2. **Mobile Clock & Timer Widgets**
   - Transform existing Clock app (`src/apps/clock/`) to mobile world clock
   - iOS-style clock widgets for status bar and home screen
   - Mobile timer and stopwatch functionality
   - Notification integration for alarms

3. **Mobile App Switcher (Workspaces Transform)**
   - Transform existing Workspaces app (`src/apps/workspaces/`) to iOS-style app switcher
   - Recent apps with live preview thumbnails
   - Swipe gestures for app switching
   - Integration with mobile task management

4. **Mobile Edge Gesture System (Switcher Transform)**
   - Transform existing Switcher app (`src/apps/switcher/`) to mobile edge gestures
   - Screen edge triggers for quick actions and app access
   - Gesture-based navigation and shortcuts
   - Integration with mobile multitasking

5. **Mobile File Manager (Tracker Transform)**
   - Transform existing Tracker (`src/kits/tracker/`) to iOS Files-style mobile file manager
   - Touch-optimized file browsing with swipe gestures
   - Mobile file operations (copy, move, delete, share)
   - Integration with mobile apps and cloud storage

6. **Accessibility and Input Methods**
   - Virtual keyboard optimization
   - Screen reader support  
   - Dynamic type support

### Phase 5: Mobile File Management (4-6 weeks)
1. **Mobile File Manager (Tracker Transform)**
   - Transform existing Tracker to iOS Files-style mobile interface
   - Touch-optimized file browsing with gesture navigation
   - Mobile sharing and inter-app file operations
   - Cloud storage integration and sync features

### Phase 6: Developer Tools (4-6 weeks)
1. **Development Support**
   - Mobile-specific testing framework
   - Performance profiling tools

## Critical Mobile Features (QEMU-Ready)

- **Always Mobile**: No desktop mode preservation, mobile is the only mode
- **Always Fullscreen**: All windows default to fullscreen mobile apps
- **Always Touch-Enabled**: All input optimized for touch
- **Compositor System**: Software rendering with floating 5+ FPS for AGG compatibility
- **Springboard Home Screen**: Deskbar completely transformed to iOS-style launcher
- **Mobile App Lifecycle**: Always B_APP_WILL_SUSPEND/B_APP_DID_RESUME
- **Virtual Keyboard**: Always available and touch-optimized
- **iOS-Style Settings**: True mobile app, not desktop preference launcher
- **QEMU Compatible**: No special hardware requirements, uses existing Haiku networking

## Technical Considerations

### QEMU Compatibility
- Standard x86_64 architecture support
- Uses existing Haiku network stack
- No special mobile hardware requirements
- AGG software rendering compatible

### Performance Targets
- Smooth touch interactions
- 5+ FPS compositor for software rendering
- < 200ms app launch times
- Efficient memory usage

## Key Architectural Changes

1. **Remove Desktop Code**: Completely eliminate desktop-specific behaviors
2. **Repurpose Functions**: Transform existing function meanings to mobile semantics
3. **Extend Existing Infrastructure**: Use notification system, power management, input system
4. **Mobile-Only Defaults**: Everything defaults to mobile behavior, no conditional code
5. **Preserve Architecture**: Keep Haiku's mature component system intact

## Mobile Mail Application Transformation

### Existing Mail App Structure (23 files)
The Haiku Mail app (`src/apps/mail/`) is a mature email client with excellent architecture for mobile adaptation:

**Core Application Files:**
- `MailApp.cpp/.h` - Transform to mobile app lifecycle management
- `MailWindow.cpp/.h` - Convert to fullscreen mobile email interface
- `MailSupport.cpp/.h` - Extend with mobile email utilities

**Mobile-Optimized Components:**
- `AddressTextControl.cpp/.h` - Touch-friendly recipient entry with contact picker
- `Content.cpp/.h` - Mobile email content rendering with touch scrolling
- `Enclosures.cpp/.h` - Mobile attachment viewer with swipe gestures
- `Header.cpp/.h` - Compact mobile email header design
- `FindWindow.cpp/.h` - Mobile search interface with keyboard integration

**Contact Integration:**
- `People.cpp/.h` - Mobile contact picker and management
- `QueryList.cpp/.h` - Contact search optimization for mobile
- `QueryMenu.cpp/.h` - Touch-friendly contact selection

**Mobile-Enhanced Features:**
- `Prefs.cpp/.h` - Mobile email preferences (embedded in main settings app)
- `Settings.cpp/.h` - Mobile-specific email settings
- `Signature.cpp/.h` - Mobile signature management
- `Utilities.cpp/.h` - Mobile email utilities and helpers

### Mobile Mail Transformation Strategy

1. **Fullscreen Mobile Interface**
   - Remove desktop window decorations from `MailWindow.cpp`
   - Implement iOS-style navigation with back/forward gestures
   - Touch-optimized toolbar with essential email actions

2. **Mobile Email Composition**
   - Transform `AddressTextControl` for touch input with contact picker
   - Implement swipe gestures for email navigation
   - Mobile-friendly attachment handling with camera integration

3. **Conversation View**
   - Convert traditional email view to mobile conversation threading
   - Touch-friendly email selection and management
   - Swipe actions for delete/archive/reply

4. **Push Notifications**
   - Integrate with existing Haiku notification system
   - Real-time email notifications with preview
   - Badge count integration for unread messages

5. **Mobile-Specific Features**
   - Quick reply from notification center
   - Touch-optimized email search
   - Mobile sharing integration with other apps

This transformation leverages the existing robust Mail architecture while providing a modern mobile email experience comparable to iOS Mail app.

## Mobile Clock Application Transformation

### Existing Clock App Structure (6 files)
The Haiku Clock app (`src/apps/clock/`) is a simple but effective analog clock with excellent potential for mobile widgets:

**Core Application Files:**
- `clock.cpp/.h` - Transform to mobile clock widget system
- `cl_wind.cpp/.h` - Convert to mobile widget windows and status bar integration  
- `cl_view.cpp/.h` - Transform to touch-friendly clock views and time displays

### Mobile Clock Transformation Strategy

1. **Mobile World Clock Widget**
   - Transform `TOnscreenView` to responsive mobile clock widget
   - Support multiple time zones with swipe navigation
   - Touch-optimized analog/digital clock switching
   - Integration with home screen widget system

2. **Status Bar Clock Integration**
   - Convert existing clock rendering to status bar component
   - Real-time update with minimal battery impact
   - Touch interaction to open full Clock app

3. **Mobile Timer & Stopwatch**
   - Extend existing clock foundation with timer functionality
   - Touch-optimized time setting with scroll wheels
   - Visual and notification alerts for timers
   - Background timer support with system notifications

4. **Home Screen Widget**
   - Multiple clock face styles (9 existing faces + mobile-optimized)
   - Resizable widget for different home screen layouts
   - Quick access to alarms and world clocks
   - Live updating with efficient rendering

5. **Mobile-Specific Features**
   - Bedtime mode with dimmed display
   - Alarm integration with system notifications
   - Quick timer shortcuts (1min, 5min, etc.)
   - Voice control integration for hands-free operation

The Clock app's simple architecture and visual appeal make it perfect for mobile widgets while maintaining Haiku's efficient graphics rendering.

## Mobile App Switcher (Workspaces Transformation)

### Existing Workspaces App Structure (3 files)
The Haiku Workspaces app (`src/apps/workspaces/`) provides virtual desktop management with miniature workspace views - perfect foundation for mobile app switching:

**Core Application Files:**
- `Workspaces.cpp` - Transform to mobile app switcher with card-based interface
- `Workspaces.rdef` - Mobile app switcher resources and icons
- `Jamfile` - Build configuration for mobile app switcher

### Mobile App Switcher Transformation Strategy

1. **iOS-Style App Cards Interface**
   - Transform `WorkspacesView` to show running app thumbnails instead of workspaces
   - Card-based layout with live app previews
   - Touch-optimized card spacing and sizing
   - Smooth scrolling between app cards

2. **Gesture-Based App Switching**
   - Swipe up from bottom edge to reveal app switcher
   - Horizontal swipe to navigate between recent apps
   - Swipe up on cards to close/quit applications
   - Pinch gestures for overview vs detailed view

3. **Live App Previews**
   - Real-time app screenshots for each running application
   - Efficient preview generation using existing Haiku rendering
   - Memory-optimized thumbnail caching
   - Auto-update previews when apps change

4. **Mobile Task Management**
   - "Close All" functionality for memory management
   - App usage statistics and memory indicators
   - Background app identification and management
   - Integration with mobile power management

5. **Quick Access Features**
   - Recently used apps prominently displayed
   - Search functionality to find specific apps quickly
   - Favorites/pinned apps for instant access
   - Integration with home screen app launcher

### Technical Advantages
- **Existing Foundation**: Workspaces already handles multiple view states efficiently
- **Live Updates**: Built-in real-time updating system perfect for app previews
- **Memory Efficiency**: Haiku's lightweight architecture ideal for mobile task management
- **Touch Integration**: Simple view system easy to adapt for touch gestures

This transformation converts Haiku's workspace management into a modern mobile multitasking interface while preserving the efficient underlying architecture.

## Mobile Edge Gesture System (Switcher Transformation)

### Existing Switcher App Structure (14 files)
The Haiku Switcher app (`src/apps/switcher/`) provides edge-triggered application switching - perfect foundation for mobile gesture navigation:

**Core Application Files:**
- `Switcher.cpp/.h` - Transform to mobile gesture coordinator and edge detection
- `CaptureWindow.cpp/.h` - Mobile gesture capture and recognition system
- `PanelWindow.cpp/.h` - Mobile gesture response panels and overlays

**Mobile Gesture Views:**
- `ApplicationsView.cpp/.h` - Transform to mobile app quick access from edges
- `GroupListView.cpp/.h` - Mobile app grouping and organization
- `WindowsView.cpp/.h` - Mobile window management through gestures
- `LaunchButton.cpp/.h` - Touch-optimized app launch controls

**Build & Resources:**
- `Switcher.rdef` - Mobile gesture system resources and configurations
- `Jamfile` - Build configuration for mobile gesture system

### Mobile Edge Gesture Transformation Strategy

1. **iOS-Style Edge Gestures**
   - Transform edge detection (`kTopEdge`, `kBottomEdge`, etc.) to mobile gestures
   - Swipe from edges for system actions and app switching
   - Configurable gesture zones and sensitivity
   - Support for iPhone X-style home indicator gestures

2. **Quick Access Panels**
   - Transform `PanelWindow` to slide-out mobile panels
   - Left edge: Notification center and quick settings
   - Right edge: Recently used apps and shortcuts  
   - Bottom edge: Control center with system toggles
   - Top edge: Search and spotlight-like functionality

3. **Gesture-Based Navigation**
   - Back gesture (swipe from left edge) for app navigation
   - Home gesture (swipe up from bottom) to return to Springboard
   - App switcher gesture (swipe up and hold) for multitasking view
   - Quick switch (swipe along bottom edge) between recent apps

4. **Contextual Quick Actions**
   - Transform `ApplicationsView` for per-app quick actions
   - Long press on edges for contextual menus
   - Camera shortcut from lock screen (swipe from bottom right)
   - Flashlight shortcut from lock screen (swipe from bottom left)

5. **Adaptive Gesture Recognition**
   - Machine learning-based gesture prediction
   - User habit adaptation for personalized shortcuts
   - Accessibility support for different motor abilities
   - Gesture customization and user preferences

### Technical Advantages
- **Existing Edge Detection**: Switcher already handles screen edge interactions
- **Event Capture System**: Built-in `CaptureWindow` perfect for gesture recognition
- **Multi-Panel Architecture**: Existing panel system adapts well to mobile overlays
- **Performance Optimized**: Lightweight system ideal for real-time gesture response

This transformation creates a comprehensive mobile gesture navigation system using Haiku's existing edge-based application switcher as the foundation.

## Mobile File Manager (Tracker Transformation)

### Existing Tracker Structure (99 files)
The Haiku Tracker (`src/kits/tracker/`) is a comprehensive file manager with excellent architecture for mobile adaptation:

**Core File Management:**
- `Tracker.cpp/.h` - Transform to mobile file manager coordinator
- `ContainerWindow.cpp/.h` - Convert to mobile file browser windows
- `PoseView.cpp/.h` - Transform to touch-optimized file grid/list views
- `Pose.cpp/.h` - File item representation for mobile interface

**Mobile-Optimized Views:**
- `DeskWindow.cpp/.h` - Desktop integration for mobile home screen
- `DesktopPoseView.cpp/.h` - Mobile desktop file management
- `QueryPoseView.cpp/.h` - Mobile search results interface
- `VirtualDirectoryPoseView.cpp/.h` - Cloud storage and virtual folders

**File Operations & Navigation:**
- `FSUtils.cpp/.h` - Mobile file system utilities and operations
- `Navigator.cpp/.h` - Touch-friendly navigation controls
- `DirMenu.cpp/.h` - Mobile breadcrumb and folder navigation
- `MountMenu.cpp/.h` - Mobile storage device management

**Search & Organization:**
- `FindPanel.cpp/.h` - Transform to mobile file search interface
- `QueryContainerWindow.cpp/.h` - Mobile search results and filters
- `RecentItems.cpp/.h` - Recent files and quick access
- `FavoritesMenu.cpp/.h` - Mobile bookmarks and favorites

**File Information & Properties:**
- `infowindow/InfoWindow.cpp/.h` - Mobile file properties and details
- `infowindow/GeneralInfoView.cpp/.h` - File information for mobile
- `infowindow/FilePermissionsView.cpp/.h` - Mobile permissions interface
- `infowindow/AttributesView.cpp/.h` - Mobile metadata viewer

**Mobile Integration Features:**
- `FilePanel.cpp/.h` - Mobile file picker for app integration
- `OpenWithWindow.cpp/.h` - Mobile app chooser interface
- `Thumbnails.cpp/.h` - Mobile thumbnail generation and caching
- `IconCache.cpp/.h` - Efficient icon caching for mobile

### Mobile File Manager Transformation Strategy

1. **iOS Files-Style Interface**
   - Transform `PoseView` to card-based file browsing
   - Touch-optimized grid and list view switching
   - Swipe gestures for file operations (delete, share, move)
   - Pull-to-refresh for folder content updates

2. **Mobile Navigation System**
   - Transform `Navigator` to iOS-style breadcrumb navigation
   - Back/forward gestures for folder navigation
   - Sidebar with favorites, recent, and cloud storage
   - Search integration with natural language queries

3. **Touch-Optimized File Operations**
   - Long press for file selection and context menus
   - Multi-select with checkbox interface
   - Drag and drop between folders and apps
   - Swipe actions for common operations

4. **Mobile Sharing & Integration**
   - Transform `OpenWithWindow` to mobile app sharing sheet
   - Integration with mobile photo gallery and camera
   - Cloud storage sync (Google Drive, Dropbox, OneDrive)
   - Inter-app file sharing and document providers

5. **Quick Access Features**
   - Recent files widget for home screen
   - Spotlight-style file search from anywhere
   - Favorites and pinned folders for quick access
   - Smart folders for Photos, Downloads, Documents

### Technical Advantages
- **Comprehensive Architecture**: Tracker already handles all file management operations
- **Virtual Directory Support**: Built-in support for cloud storage integration
- **Advanced Search**: Existing query system perfect for mobile search
- **Thumbnail System**: Efficient thumbnail generation for mobile galleries
- **Extensible Design**: Plugin architecture adaptable for mobile file providers

This transformation converts Haiku's powerful desktop file manager into a modern mobile file management system comparable to iOS Files app while preserving the robust underlying architecture.

---

This plan transforms Haiku into a mobile-first operating system while preserving its architectural strengths and maintaining QEMU compatibility for development and testing.