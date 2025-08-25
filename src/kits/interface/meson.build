# Haiku Interface Kit - Meson Build Configuration
# This builds interface_kit.o matching the exact Jam build system output

# Interface Kit sources (exactly from interface/Jamfile)
interface_kit_sources = [
    # Main interface sources
    'AboutWindow.cpp',
    'AbstractLayout.cpp',
    'AbstractLayoutItem.cpp',
    'AbstractSpinner.cpp',
    'AffineTransform.cpp',
    'Alert.cpp',
    'Alignment.cpp',
    'BMCPrivate.cpp',
    'Bitmap.cpp',
    'Box.cpp',
    'Button.cpp',
    'CardLayout.cpp',
    'CardView.cpp',
    'ChannelControl.cpp',
    'ChannelSlider.cpp',
    'CheckBox.cpp',
    'ColorControl.cpp',
    'ColorConversion.cpp',
    'ColorMenuItem.cpp',
    'Control.cpp',
    'ControlLook.cpp',
    'DecimalSpinner.cpp',
    'DecorInfo.cpp',
    'Deskbar.cpp',
    'Dragger.cpp',
    'Font.cpp',
    'Gradient.cpp',
    'GradientConic.cpp',
    'GradientDiamond.cpp',
    'GradientLinear.cpp',
    'GradientRadial.cpp',
    'GradientRadialFocus.cpp',
    'GraphicsDefs.cpp',
    'GridLayout.cpp',
    'GridLayoutBuilder.cpp',
    'GridView.cpp',
    'GroupLayout.cpp',
    'GroupLayoutBuilder.cpp',
    'GroupView.cpp',
    'HaikuControlLook.cpp',
    'Icon.cpp',
    'Input.cpp',
    'InterfaceDefs.cpp',
    'Layout.cpp',
    'LayoutContext.cpp',
    'LayoutItem.cpp',
    'LayoutUtils.cpp',
    'ListItem.cpp',
    'ListView.cpp',
    'Menu.cpp',
    'MenuBar.cpp',
    'MenuField.cpp',
    'MenuItem.cpp',
    'MenuItemPrivate.cpp',
    'MenuPrivate.cpp',
    'MenuWindow.cpp',
    'OptionControl.cpp',
    'OptionPopUp.cpp',
    'OutlineListView.cpp',
    'Picture.cpp',
    'PictureButton.cpp',
    'PictureDataWriter.cpp',
    'PicturePlayer.cpp',
    'Point.cpp',
    'Polygon.cpp',
    'PopUpMenu.cpp',
    'PrintJob.cpp',
    'PrivateScreen.cpp',
    'RadioButton.cpp',
    'Rect.cpp',
    'Region.cpp',
    'RegionSupport.cpp',
    'Screen.cpp',
    'ScrollBar.cpp',
    'ScrollView.cpp',
    'SeparatorItem.cpp',
    'SeparatorView.cpp',
    'Shape.cpp',
    'Shelf.cpp',
    'Size.cpp',
    'Slider.cpp',
    'SpaceLayoutItem.cpp',
    'Spinner.cpp',
    'SplitLayout.cpp',
    'SplitLayoutBuilder.cpp',
    'SplitView.cpp',
    'StatusBar.cpp',
    'StringItem.cpp',
    'StringView.cpp',
    'TabView.cpp',
    'TextControl.cpp',
    'TextInput.cpp',
    'TextView.cpp',
    'ToolTip.cpp',
    'ToolTipManager.cpp',
    'TwoDimensionalLayout.cpp',
    'View.cpp',
    'ViewLayoutItem.cpp',
    'ViewPort.cpp',
    'Window.cpp',
    'WindowStack.cpp',
    'ZombieReplicantView.cpp',
    
    # BTextView support (from textview_support subdirectory)
    'textview_support/InlineInput.cpp',
    'textview_support/LineBuffer.cpp',
    'textview_support/StyleBuffer.cpp',
    'textview_support/TextGapBuffer.cpp',
    'textview_support/UndoBuffer.cpp',
    'textview_support/WidthBuffer.cpp',
    
    # Layouter (from layouter subdirectory)
    'layouter/CollapsingLayouter.cpp',
    'layouter/ComplexLayouter.cpp',
    'layouter/LayoutOptimizer.cpp',
    'layouter/Layouter.cpp',
    'layouter/OneElementLayouter.cpp',
    'layouter/SimpleLayouter.cpp',
    
    # Shared sources (from ../shared)
    '../shared/DragTrackingFilter.cpp',
    '../shared/HSL.cpp',
]

# Interface kit defines (matching Jamfile conditions)
interface_kit_defines = [
    '-DZSTD_ENABLED',
    '-DHAIKU_TARGET_PLATFORM_HAIKU',
]

# Add optional defines based on build options
if get_option('run_without_registrar') == true
    interface_kit_defines += ['-DRUN_WITHOUT_REGISTRAR']
endif

if get_option('run_without_app_server') != 0
    interface_kit_defines += ['-DRUN_WITHOUT_APP_SERVER']
endif

message('Building Interface Kit with ' + interface_kit_sources.length().to_string() + ' source files')

# Create interface kit object library
interface_kit_obj = static_library('interface_kit_obj',
    interface_kit_sources,
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('textview_support'),
        include_directories('layouter')
    ],
    cpp_args: [
        '-O2',                           # Optimization (matching Jam)
        '-fno-strict-aliasing',          # Standard Haiku flag
        '-fno-implicit-templates',       # KEY: Prevent template instantiation bloat
        '-fno-implicit-inline-templates' # KEY: Prevent inline template bloat
    ] + interface_kit_defines,
    pic: true,                           # Use Meson's pic instead of manual -fPIC
    dependencies: [
        # Build packages provide headers via global include paths
    ],
    install: false
)

# Output directory (matching generated_meson structure)
interface_kit_output_dir = haiku_config['kits_output_dir'] + '/interface'

# Create merged object file interface_kit.o (matching Jam MergeObject)
interface_kit_merged = custom_target('interface_kit',
    input: interface_kit_obj,
    output: 'interface_kit.o',
    command: [
        'sh', '-c',
        'mkdir -p ' + interface_kit_output_dir + ' && /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o ' + interface_kit_output_dir + '/interface_kit.o @INPUT@.p/*.o'
    ],
    build_by_default: true,
    install: false
)

message('Interface Kit configured successfully - output: interface_kit.o')