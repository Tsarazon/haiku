/*
 * Copyright 2001-2016, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */


/*!	Manages font families and styles */


#include "GlobalFontManager.h"

#include <new>

#include <Autolock.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "FontFamily.h"
#include "ServerConfig.h"
#include "ServerFont.h"


//#define TRACE_GLOBAL_FONT_MANAGER
#ifdef TRACE_GLOBAL_FONT_MANAGER
#	define FTRACE(x) debug_printf x
#else
#	define FTRACE(x) ;
#endif


// TODO: needs some more work for multi-user support

GlobalFontManager* gFontManager = NULL;
extern FT_Library gFreeTypeLibrary;


struct GlobalFontManager::font_directory {
	node_ref	directory;
	uid_t		user;
	gid_t		group;
	bool		scanned;
	BObjectList<FontStyle> styles;

	FontStyle* FindStyle(const node_ref& nodeRef) const;
};


struct GlobalFontManager::font_mapping {
	BString		family;
	BString		style;
	entry_ref	ref;
};


FontStyle*
GlobalFontManager::font_directory::FindStyle(const node_ref& nodeRef) const
{
	for (int32 i = styles.CountItems(); i-- > 0;) {
		FontStyle* style = styles.ItemAt(i);

		if (nodeRef == style->NodeRef())
			return style;
	}

	return NULL;
}


static status_t
set_entry(node_ref& nodeRef, const char* name, BEntry& entry)
{
	entry_ref ref;
	ref.device = nodeRef.device;
	ref.directory = nodeRef.node;

	status_t status = ref.set_name(name);
	if (status != B_OK)
		return status;

	return entry.SetTo(&ref);
}


//	#pragma mark -


//! Does basic set up so that directories can be scanned
GlobalFontManager::GlobalFontManager()
	: BLooper("GlobalFontManager"),
	fDirectories(10),
	fMappings(10),

	fDefaultPlainFont(NULL),
	fDefaultBoldFont(NULL),
	fDefaultFixedFont(NULL),

	fScanned(false)
{
	fInitStatus = FT_Init_FreeType(&gFreeTypeLibrary) == 0 ? B_OK : B_ERROR;
	if (fInitStatus == B_OK) {
		_AddSystemPaths();
		_AddUserPaths();
		_LoadRecentFontMappings();

		fInitStatus = _SetDefaultFonts();

		if (fInitStatus == B_OK) {
			// Precache the plain and bold fonts
			_PrecacheFontFile(fDefaultPlainFont.Get());
			_PrecacheFontFile(fDefaultBoldFont.Get());

			// Post a message so we scan the initial paths.
			PostMessage(B_PULSE);
		}
	}
}


//! Frees items allocated in the constructor and shuts down FreeType
GlobalFontManager::~GlobalFontManager()
{
	fDefaultPlainFont.Unset();
	fDefaultBoldFont.Unset();
	fDefaultFixedFont.Unset();

	_RemoveAllFonts();

	FT_Done_FreeType(gFreeTypeLibrary);
}


void
GlobalFontManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				return;

			switch (opcode) {
				case B_ENTRY_CREATED:
				{
					const char* name;
					node_ref nodeRef;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("directory", &nodeRef.node) != B_OK
						|| message->FindString("name", &name) != B_OK)
						break;

					// TODO: make this better (possible under Haiku)
					snooze(100000);
						// let the font be written completely before trying to open it

					BEntry entry;
					if (set_entry(nodeRef, name, entry) != B_OK)
						break;

					if (entry.IsDirectory()) {
						// a new directory to watch for us
						_AddPath(entry);
					} else {
						// a new font
						font_directory* directory = _FindDirectory(nodeRef);
						if (directory == NULL) {
							// unknown directory? how come?
							break;
						}

						_AddFont(*directory, entry);
					}
					break;
				}

				case B_ENTRY_MOVED:
				{
					// has the entry been moved into a monitored directory or has
					// it been removed from one?
					const char* name;
					node_ref nodeRef;
					uint64 fromNode;
					uint64 node;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("to directory", &nodeRef.node) != B_OK
						|| message->FindInt64("from directory", (int64 *)&fromNode) != B_OK
						|| message->FindInt64("node", (int64 *)&node) != B_OK
						|| message->FindString("name", &name) != B_OK)
						break;

					font_directory* directory = _FindDirectory(nodeRef);

					BEntry entry;
					if (set_entry(nodeRef, name, entry) != B_OK)
						break;

					if (directory != NULL) {
						// something has been added to our watched font directories

						// test, if the source directory is one of ours as well
						nodeRef.node = fromNode;
						font_directory* fromDirectory = _FindDirectory(nodeRef);

						if (entry.IsDirectory()) {
							if (fromDirectory == NULL) {
								// there is a new directory to watch for us
								_AddPath(entry);
								FTRACE(("new directory moved in"));
							} else {
								// A directory from our watched directories has
								// been renamed or moved within the watched
								// directories - we only need to update the
								// path names of the styles in that directory
								nodeRef.node = node;
								directory = _FindDirectory(nodeRef);
								if (directory != NULL) {
									for (int32 i = 0; i < directory->styles.CountItems(); i++) {
										FontStyle* style = directory->styles.ItemAt(i);
										style->UpdatePath(directory->directory);
									}
								}
								FTRACE(("directory renamed"));
							}
						} else {
							if (fromDirectory != NULL) {
								// find style in source and move it to the target
								nodeRef.node = node;
								FontStyle* style;
								while ((style = fromDirectory->FindStyle(nodeRef)) != NULL) {
									fromDirectory->styles.RemoveItem(style, false);
									directory->styles.AddItem(style);
									style->UpdatePath(directory->directory);
								}
								FTRACE(("font moved"));
							} else {
								FTRACE(("font added: %s\n", name));
								_AddFont(*directory, entry);
							}
						}
					} else {
						// and entry has been removed from our font directories
						if (entry.IsDirectory()) {
							if (entry.GetNodeRef(&nodeRef) == B_OK
								&& (directory = _FindDirectory(nodeRef)) != NULL)
								_RemoveDirectory(directory);
						} else {
							// remove font style from directory
							_RemoveStyle(nodeRef.device, fromNode, node);
						}
					}
					break;
				}

				case B_ENTRY_REMOVED:
				{
					node_ref nodeRef;
					uint64 directoryNode;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("directory", (int64 *)&directoryNode) != B_OK
						|| message->FindInt64("node", &nodeRef.node) != B_OK)
						break;

					font_directory* directory = _FindDirectory(nodeRef);
					if (directory != NULL) {
						// the directory has been removed, so we remove it as well
						_RemoveDirectory(directory);
					} else {
						// remove font style from directory
						_RemoveStyle(nodeRef.device, directoryNode, nodeRef.node);
					}
					break;
				}
			}
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}

	// Scan fonts here if we need to, preventing other threads from having to do so.
	_ScanFontsIfNecessary();
}


uint32
GlobalFontManager::Revision()
{
	BAutolock locker(this);

	_ScanFontsIfNecessary();

	return FontManager::Revision();
}


void
GlobalFontManager::SaveRecentFontMappings()
{
}


void
GlobalFontManager::_AddDefaultMapping(const char* family, const char* style,
	const char* path)
{
	font_mapping* mapping = new (std::nothrow) font_mapping;
	if (mapping == NULL)
		return;

	mapping->family = family;
	mapping->style = style;
	BEntry entry(path);

	if (entry.GetRef(&mapping->ref) != B_OK
		|| !entry.Exists()
		|| !fMappings.AddItem(mapping))
		delete mapping;
}


bool
GlobalFontManager::_LoadRecentFontMappings()
{
	// default known mappings
	// TODO: load them for real, and use these as a fallback

	BPath ttfontsPath;
	if (find_directory(B_BEOS_FONTS_DIRECTORY, &ttfontsPath) == B_OK) {
		ttfontsPath.Append("ttfonts");

		BPath veraFontPath = ttfontsPath;
		veraFontPath.Append("NotoSans-Regular.ttf");
		_AddDefaultMapping("Noto Sans", "Book", veraFontPath.Path());

		veraFontPath.SetTo(ttfontsPath.Path());
		veraFontPath.Append("NotoSans-Bold.ttf");
		_AddDefaultMapping("Noto Sans", "Bold", veraFontPath.Path());

		veraFontPath.SetTo(ttfontsPath.Path());
		veraFontPath.Append("NotoSansMono-Regular.ttf");
		_AddDefaultMapping("Noto Sans Mono", "Regular", veraFontPath.Path());

		return true;
	}

	return false;
}


status_t
GlobalFontManager::_AddMappedFont(const char* familyName, const char* styleName)
{
	FTRACE(("_AddMappedFont(family = \"%s\", style = \"%s\")\n",
		familyName, styleName));

	for (int32 i = 0; i < fMappings.CountItems(); i++) {
		font_mapping* mapping = fMappings.ItemAt(i);

		if (mapping->family == familyName) {
			if (styleName != NULL && mapping->style != styleName)
				continue;

			BEntry entry(&mapping->ref);
			if (entry.InitCheck() != B_OK)
				continue;

			// find parent directory

			node_ref nodeRef;
			nodeRef.device = mapping->ref.device;
			nodeRef.node = mapping->ref.directory;
			font_directory* directory = _FindDirectory(nodeRef);
			if (directory == NULL) {
				// unknown directory, maybe this is a user font - try
				// to create the missing directory
				BPath path(&entry);
				if (path.GetParent(&path) != B_OK
					|| _CreateDirectories(path.Path()) != B_OK
					|| (directory = _FindDirectory(nodeRef)) == NULL)
					continue;
			}

			return _AddFont(*directory, entry);
		}
	}

	return B_ENTRY_NOT_FOUND;
}


FontStyle*
GlobalFontManager::_GetDefaultStyle(const char* familyName, const char* styleName,
	const char* fallbackFamily, const char* fallbackStyle,
	uint16 fallbackFace)
{
	// try to find a matching font
	FontStyle* style = GetStyle(familyName, styleName);
	if (style == NULL) {
		style = GetStyle(fallbackFamily, fallbackStyle);
		if (style == NULL) {
			style = FindStyleMatchingFace(fallbackFace);
			if (style == NULL && FamilyAt(0) != NULL)
				style = FamilyAt(0)->StyleAt(0);
		}
	}

	return style;
}


/*!	\brief Sets the fonts that will be used when you create an empty
		ServerFont without specifying a style, as well as the default
		Desktop fonts if there are no settings available.
*/
status_t
GlobalFontManager::_SetDefaultFonts()
{
	FontStyle* style = NULL;

	// plain font
	style = _GetDefaultStyle(DEFAULT_PLAIN_FONT_FAMILY, DEFAULT_PLAIN_FONT_STYLE,
		FALLBACK_PLAIN_FONT_FAMILY, FALLBACK_PLAIN_FONT_STYLE, B_REGULAR_FACE);
	if (style == NULL)
		return B_ERROR;

	fDefaultPlainFont.SetTo(new (std::nothrow) ServerFont(*style,
		DEFAULT_FONT_SIZE));
	if (!fDefaultPlainFont.IsSet())
		return B_NO_MEMORY;

	// bold font
	style = _GetDefaultStyle(DEFAULT_BOLD_FONT_FAMILY, DEFAULT_BOLD_FONT_STYLE,
		FALLBACK_BOLD_FONT_FAMILY, FALLBACK_BOLD_FONT_STYLE, B_BOLD_FACE);

	fDefaultBoldFont.SetTo(new (std::nothrow) ServerFont(*style,
		DEFAULT_FONT_SIZE));
	if (!fDefaultBoldFont.IsSet())
		return B_NO_MEMORY;

	// fixed font
	style = _GetDefaultStyle(DEFAULT_FIXED_FONT_FAMILY, DEFAULT_FIXED_FONT_STYLE,
		FALLBACK_FIXED_FONT_FAMILY, FALLBACK_FIXED_FONT_STYLE, B_REGULAR_FACE);

	fDefaultFixedFont.SetTo(new (std::nothrow) ServerFont(*style,
		DEFAULT_FONT_SIZE));
	if (!fDefaultFixedFont.IsSet())
		return B_NO_MEMORY;

	fDefaultFixedFont->SetSpacing(B_FIXED_SPACING);

	return B_OK;
}


/*!	\brief Removes the style from the font directory.

	It doesn't necessary delete the font style, if it's still
	in use, though.
*/
void
GlobalFontManager::_RemoveStyle(font_directory& directory, FontStyle* style)
{
	FTRACE(("font removed: %s\n", style->Name()));

	directory.styles.RemoveItem(style);

	_RemoveFont(style->Family()->ID(), style->ID());
}


void
GlobalFontManager::_RemoveStyle(dev_t device, uint64 directoryNode, uint64 node)
{
	// remove font style from directory
	node_ref nodeRef;
	nodeRef.device = device;
	nodeRef.node = directoryNode;

	font_directory* directory = _FindDirectory(nodeRef);
	if (directory != NULL) {
		// find style in directory and remove it
		nodeRef.node = node;
		FontStyle* style;
		while ((style = directory->FindStyle(nodeRef)) != NULL)
			_RemoveStyle(*directory, style);
	}
}


/*!	\brief Counts the number of font families available
	\return The number of unique font families currently available
*/
int32
GlobalFontManager::CountFamilies()
{
	_ScanFontsIfNecessary();

	return FontManager::CountFamilies();
}


/*!	\brief Counts the number of styles available in a font family
	\param family Name of the font family to scan
	\return The number of font styles currently available for the font family
*/
int32
GlobalFontManager::CountStyles(const char* familyName)
{
	_ScanFontsIfNecessary();

	FontFamily* family = GetFamily(familyName);
	if (family)
		return family->CountStyles();

	return 0;
}


/*!	\brief Counts the number of styles available in a font family
	\param family Name of the font family to scan
	\return The number of font styles currently available for the font family
*/
int32
GlobalFontManager::CountStyles(uint16 familyID)
{
	_ScanFontsIfNecessary();

	FontFamily* family = GetFamily(familyID);
	if (family)
		return family->CountStyles();

	return 0;
}


FontStyle*
GlobalFontManager::GetStyle(uint16 familyID, uint16 styleID) const
{
	return FontManager::GetStyle(familyID, styleID);
}


/*!	\brief Retrieves the FontStyle object that comes closest to the one
		specified.

	\param family The font's family or NULL in which case \a familyID is used
	\param style The font's style or NULL in which case \a styleID is used
	\param familyID will only be used if \a family is NULL (or empty)
	\param styleID will only be used if \a family and \a style are NULL (or empty)
	\param face is used to specify the style if both \a style is NULL or empty
		and styleID is 0xffff.

	\return The FontStyle having those attributes or NULL if not available
*/
FontStyle*
GlobalFontManager::GetStyle(const char* familyName, const char* styleName,
	uint16 familyID, uint16 styleID, uint16 face)
{
	ASSERT(IsLocked());

	if (styleID != 0xffff && (familyName == NULL || !familyName[0])
		&& (styleName == NULL || !styleName[0])) {
		return GetStyle(familyID, styleID);
	}

	// find family

	FontFamily* family;
	if (familyName != NULL && familyName[0])
		family = GetFamily(familyName);
	else
		family = GetFamily(familyID);

	if (family == NULL)
		return NULL;

	// find style

	if (styleName != NULL && styleName[0]) {
		FontStyle* fontStyle = family->GetStyle(styleName);
		if (fontStyle != NULL)
			return fontStyle;

		// before we fail, we try the mappings for a match
		if (_AddMappedFont(family->Name(), styleName) == B_OK) {
			fontStyle = family->GetStyle(styleName);
			if (fontStyle != NULL)
				return fontStyle;
		}

		_ScanFonts();
		return family->GetStyle(styleName);
	}

	// try to get from face
	return family->GetStyleMatchingFace(face);
}


void
GlobalFontManager::_PrecacheFontFile(const ServerFont* font)
{
	if (font == NULL)
		return;

	size_t bufferSize = 32768;
	uint8* buffer = new (std::nothrow) uint8[bufferSize];
	if (buffer == NULL) {
		// We don't care. Pre-caching doesn't make sense anyways when there
		// is not enough RAM...
		return;
	}

	BFile file(font->Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		delete[] buffer;
		return;
	}

	while (true) {
		// We just want the file in the kernel file cache...
		ssize_t read = file.Read(buffer, bufferSize);
		if (read < (ssize_t)bufferSize)
			break;
	}

	delete[] buffer;
}


void
GlobalFontManager::_AddSystemPaths()
{
	BPath path;
	if (find_directory(B_SYSTEM_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());

	// We don't scan these in test mode to help shave off some startup time
#if !TEST_MODE
	if (find_directory(B_SYSTEM_NONPACKAGED_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());
#endif
}


void
GlobalFontManager::_AddUserPaths()
{
#if !TEST_MODE
	// TODO: avoids user fonts in safe mode
	BPath path;
	if (find_directory(B_USER_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());
	if (find_directory(B_USER_NONPACKAGED_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());
#endif
}


void
GlobalFontManager::_ScanFontsIfNecessary()
{
	if (!fScanned)
		_ScanFonts();
}


//! Scans all currently known font directories
void
GlobalFontManager::_ScanFonts()
{
	if (fScanned)
		return;

	for (int32 i = fDirectories.CountItems(); i-- > 0;) {
		font_directory* directory = fDirectories.ItemAt(i);

		if (directory->scanned)
			continue;

		_ScanFontDirectory(*directory);
	}

	fScanned = true;
}


/*!	\brief Adds the FontFamily/FontStyle that is represented by this path.
*/
status_t
GlobalFontManager::_AddFont(font_directory& directory, BEntry& entry)
{
	node_ref nodeRef;
	status_t status = entry.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	BPath path;
	status = entry.GetPath(&path);
	if (status < B_OK)
		return status;

	FT_Face face;
	FT_Error error = FT_New_Face(gFreeTypeLibrary, path.Path(), -1, &face);
	if (error != 0)
		return B_ERROR;
	FT_Long count = face->num_faces;
	FT_Done_Face(face);

	for (FT_Long i = 0; i < count; i++) {
		FT_Error error = FT_New_Face(gFreeTypeLibrary, path.Path(), -(i + 1), &face);
		if (error != 0)
			return B_ERROR;
		uint32 variableCount = (face->style_flags & 0x7fff0000) >> 16;
		FT_Done_Face(face);

		uint32 j = variableCount == 0 ? 0 : 1;
		do {
			FT_Long faceIndex = i | (j << 16);
			error = FT_New_Face(gFreeTypeLibrary, path.Path(), faceIndex, &face);
			if (error != 0)
				return B_ERROR;

			uint16 familyID, styleID;
			status = FontManager::_AddFont(face, nodeRef, path.Path(), familyID, styleID);
			if (status == B_NAME_IN_USE) {
				status = B_OK;
				j++;
				continue;
			}
			if (status < B_OK)
				return status;
			directory.styles.AddItem(GetStyle(familyID, styleID));
			j++;
		} while (j <= variableCount);
	}

	return B_OK;
}


GlobalFontManager::font_directory*
GlobalFontManager::_FindDirectory(node_ref& nodeRef)
{
	for (int32 i = fDirectories.CountItems(); i-- > 0;) {
		font_directory* directory = fDirectories.ItemAt(i);

		if (directory->directory == nodeRef)
			return directory;
	}

	return NULL;
}


void
GlobalFontManager::_RemoveDirectory(font_directory* directory)
{
	FTRACE(("FontManager: Remove directory (%" B_PRIdINO ")!\n",
		directory->directory.node));

	fDirectories.RemoveItem(directory, false);

	// TODO: remove styles from this directory!

	watch_node(&directory->directory, B_STOP_WATCHING, this);
	delete directory;
}


status_t
GlobalFontManager::_AddPath(const char* path)
{
	BEntry entry;
	status_t status = entry.SetTo(path);
	if (status != B_OK)
		return status;

	return _AddPath(entry);
}


status_t
GlobalFontManager::_AddPath(BEntry& entry, font_directory** _newDirectory)
{
	node_ref nodeRef;
	status_t status = entry.GetNodeRef(&nodeRef);
	if (status != B_OK)
		return status;

	// check if we are already know this directory

	font_directory* directory = _FindDirectory(nodeRef);
	if (directory != NULL) {
		if (_newDirectory)
			*_newDirectory = directory;
		return B_OK;
	}

	// it's a new one, so let's add it

	directory = new (std::nothrow) font_directory;
	if (directory == NULL)
		return B_NO_MEMORY;

	struct stat stat;
	status = entry.GetStat(&stat);
	if (status != B_OK) {
		delete directory;
		return status;
	}

	directory->directory = nodeRef;
	directory->user = stat.st_uid;
	directory->group = stat.st_gid;
	directory->scanned = false;

	status = watch_node(&nodeRef, B_WATCH_DIRECTORY, this);
	if (status != B_OK) {
		// we cannot watch this directory - while this is unfortunate,
		// it's not a critical error
		printf("could not watch directory %" B_PRIdDEV ":%" B_PRIdINO "\n",
			nodeRef.device, nodeRef.node);
			// TODO: should go into syslog()
	} else {
		BPath path(&entry);
		FTRACE(("FontManager: now watching: %s\n", path.Path()));
	}

	fDirectories.AddItem(directory);

	if (_newDirectory)
		*_newDirectory = directory;

	fScanned = false;
	return B_OK;
}


/*!	\brief Creates all unknown font_directories of the specified path - but
		only if one of its parent directories is already known.

	This method is used to create the font_directories for font_mappings.
	It recursively walks upwards in the directory hierarchy until it finds
	a known font_directory (or hits the root directory, in which case it
	bails out).
*/
status_t
GlobalFontManager::_CreateDirectories(const char* path)
{
	FTRACE(("_CreateDirectories(path = %s)\n", path));

	if (!strcmp(path, "/")) {
		// we walked our way up to the root
		return B_ENTRY_NOT_FOUND;
	}

	BEntry entry;
	status_t status = entry.SetTo(path);
	if (status != B_OK)
		return status;

	node_ref nodeRef;
	status = entry.GetNodeRef(&nodeRef);
	if (status != B_OK)
		return status;

	// check if we are already know this directory

	font_directory* directory = _FindDirectory(nodeRef);
	if (directory != NULL)
		return B_OK;

	// We don't know this one yet - keep walking the path upwards
	// and try to find a match.

	BPath parent(path);
	status = parent.GetParent(&parent);
	if (status != B_OK)
		return status;

	status = _CreateDirectories(parent.Path());
	if (status != B_OK)
		return status;

	// We have our match, create sub directory

	return _AddPath(path);
}


/*!	\brief Scan a folder for all valid fonts
	\param directoryPath Path of the folder to scan.
*/
status_t
GlobalFontManager::_ScanFontDirectory(font_directory& fontDirectory)
{
	// This bad boy does all the real work. It loads each entry in the
	// directory. If a valid font file, it adds both the family and the style.

	if (fontDirectory.scanned)
		return B_OK;

	BDirectory directory;
	status_t status = directory.SetTo(&fontDirectory.directory);
	if (status != B_OK)
		return status;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory()) {
			// scan this directory recursively
			font_directory* newDirectory;
			if (_AddPath(entry, &newDirectory) == B_OK && newDirectory != NULL
				&& !newDirectory->scanned) {
				_ScanFontDirectory(*newDirectory);
			}

			continue;
		}

// TODO: Commenting this out makes my "Unicode glyph lookup"
// work with our default fonts. The real fix is to select the
// Unicode char map (if supported), and/or adjust the
// utf8 -> glyph-index mapping everywhere to handle other
// char maps. We could also ignore fonts that don't support
// the Unicode lookup as a temporary "solution".
#if 0
		FT_CharMap charmap = _GetSupportedCharmap(face);
		if (!charmap) {
		    FT_Done_Face(face);
		    continue;
    	}

		face->charmap = charmap;
#endif

		_AddFont(fontDirectory, entry);
			// takes over ownership of the FT_Face object
	}

	fontDirectory.scanned = true;
	return B_OK;
}


/*!	\brief Locates a FontFamily object by name
	\param name The family to find
	\return Pointer to the specified family or NULL if not found.
*/
FontFamily*
GlobalFontManager::GetFamily(const char* name)
{
	if (name == NULL)
		return NULL;

	FontFamily* family = _FindFamily(name);
	if (family != NULL)
		return family;

	if (fScanned)
		return NULL;

	// try font mappings before failing
	if (_AddMappedFont(name) == B_OK)
		return _FindFamily(name);

	_ScanFonts();
	return _FindFamily(name);
}


FontFamily*
GlobalFontManager::GetFamily(uint16 familyID) const
{
	return FontManager::GetFamily(familyID);
}


const ServerFont*
GlobalFontManager::DefaultPlainFont() const
{
	return fDefaultPlainFont.Get();
}


const ServerFont*
GlobalFontManager::DefaultBoldFont() const
{
	return fDefaultBoldFont.Get();
}


const ServerFont*
GlobalFontManager::DefaultFixedFont() const
{
	return fDefaultFixedFont.Get();
}
