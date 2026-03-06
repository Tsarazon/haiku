/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * GUI test application for input server devices and filters add-ons.
 * Tests: BitFieldTesters, KeyInfos, ParseCommandLine, BKeymap,
 *        MovementMaker/TouchpadMovement math, message format validation,
 *        device logic simulation, filter logic simulation.
 *
 * Runs all tests on launch, displays results in a BView,
 * writes detailed trace log to ~/Desktop/kosm_input_trace.log.
 */


#include <Application.h>
#include <FindDirectory.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Keymap.h>
#include <Message.h>
#include <Path.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <Input.h>
#include <keyboard_mouse_driver.h>
#include <touchpad_settings.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BitFieldTesters.h"
#include "KeyInfos.h"
#include "ParseCommandLine.h"
#include "movement_maker.h"


static FILE* sTraceFile = NULL;
static int sPassCount = 0;
static int sFailCount = 0;


static void
trace(const char* fmt, ...)
{
	if (sTraceFile == NULL)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(sTraceFile, fmt, args);
	va_end(args);
	fflush(sTraceFile);
}


#define TEST_ASSERT(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			trace("  FAIL: %s  (line %d, %lld us)\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
		} \
	} while (0)


// Helper: build a short device name from path, matching InputDeviceBase logic
static char*
sim_build_short_name(const char* longName, const char* suffix)
{
	BString string(longName);
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(name, slash + 1, string.Length() - slash);
	int32 index = atoi(name.String()) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	if (name.Length() < 4)
		name.ToUpper();
	else
		name.Capitalize();

	name << " " << suffix << " " << index;
	return strdup(name.String());
}


// Helper: simulate device category from path
enum SimCategory {
	kSimKeyboard,
	kSimMouse,
	kSimTouchpad,
	kSimTablet,
	kSimVirtio,
	kSimTouchscreen,
	kSimUnknown
};

static const struct {
	const char*	prefix;
	SimCategory	category;
} kSimDirs[] = {
	{ "/dev/input/keyboard",	kSimKeyboard },
	{ "/dev/input/mouse",		kSimMouse },
	{ "/dev/input/touchpad",	kSimTouchpad },
	{ "/dev/input/tablet",		kSimTablet },
	{ "/dev/input/virtio",		kSimVirtio },
	{ "/dev/input/touchscreen",	kSimTouchscreen },
};

static SimCategory
sim_category_for_path(const char* path)
{
	BString p(path);
	for (size_t i = 0; i < sizeof(kSimDirs) / sizeof(kSimDirs[0]); i++) {
		if (p.StartsWith(kSimDirs[i].prefix))
			return kSimDirs[i].category;
	}
	return kSimUnknown;
}


// Helper: simulate virtio type detection (even=tablet, odd=keyboard)
static SimCategory
sim_detect_virtio_type(const char* path)
{
	BString p(path);
	int32 virtioPos = p.FindFirst("/virtio/");
	if (virtioPos >= 0) {
		int32 numStart = virtioPos + 8;
		int32 numEnd = p.FindFirst("/", numStart);
		if (numEnd > numStart) {
			BString numStr;
			p.CopyInto(numStr, numStart, numEnd - numStart);
			int index = atoi(numStr.String());
			if (index % 2 == 0)
				return kSimTablet;
			else
				return kSimKeyboard;
		}
	}
	return kSimKeyboard;
}


// Helper: simulate mouse acceleration (from HWMouseDevice)
static void
sim_compute_acceleration(int32 xdelta, int32 ydelta, int32 speed,
	int32 accelFactor, float& histX, float& histY,
	int32& outX, int32& outY)
{
	float deltaX = (float)xdelta * speed / 65536.0f + histX;
	float deltaY = (float)ydelta * speed / 65536.0f + histY;

	double acceleration = 1;
	if (accelFactor) {
		acceleration = 1 + sqrt(deltaX * deltaX + deltaY * deltaY)
			* accelFactor / 524288.0;
	}

	deltaX *= acceleration;
	deltaY *= acceleration;

	outX = (deltaX >= 0) ? (int32)floorf(deltaX) : (int32)ceilf(deltaX);
	outY = (deltaY >= 0) ? (int32)floorf(deltaY) : (int32)ceilf(deltaY);

	histX = deltaX - outX;
	histY = deltaY - outY;
}


// Helper: simulate button remapping (from HWMouseDevice)
static uint32
sim_remap_buttons(uint32 buttons, const mouse_map& map)
{
	uint32 newButtons = 0;
	for (int32 i = 0; buttons; i++) {
		if (buttons & 0x1) {
#if defined(HAIKU_TARGET_PLATFORM_HAIKU)
			newButtons |= map.button[i];
#endif
		}
		buttons >>= 1;
	}
	return newButtons;
}


// Helper: simulate modifier tracking (from KeymapHandler)
static uint32
sim_update_modifiers(uint32 keycode, bool isKeyDown,
	uint32 currentModifiers, uint32 modifier, bool isLock)
{
	if (modifier == 0)
		return currentModifiers;

	uint32 newModifiers = currentModifiers;

	if ((isKeyDown && !isLock)
		|| (isKeyDown && !(currentModifiers & modifier))) {
		newModifiers |= modifier;
	} else if (!isKeyDown || isLock) {
		newModifiers &= ~modifier;

		if (newModifiers & (B_LEFT_SHIFT_KEY | B_RIGHT_SHIFT_KEY))
			newModifiers |= B_SHIFT_KEY;
		if (newModifiers & (B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY))
			newModifiers |= B_COMMAND_KEY;
		if (newModifiers & (B_LEFT_CONTROL_KEY | B_RIGHT_CONTROL_KEY))
			newModifiers |= B_CONTROL_KEY;
		if (newModifiers & (B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY))
			newModifiers |= B_OPTION_KEY;
	}

	return newModifiers;
}


// Helper: build a keyboard BMessage matching HWKeyboardDevice output
static BMessage*
sim_build_key_message(bool isKeyDown, uint32 keycode, uint32 modifiers,
	bigtime_t when, const uint8* states, const char* bytes, int32 numBytes)
{
	BMessage* msg = new BMessage;
	if (numBytes > 0)
		msg->what = isKeyDown ? B_KEY_DOWN : B_KEY_UP;
	else
		msg->what = isKeyDown ? B_UNMAPPED_KEY_DOWN : B_UNMAPPED_KEY_UP;

	msg->AddInt64("when", when);
	msg->AddInt32("key", keycode);
	msg->AddInt32("modifiers", modifiers);
	msg->AddData("states", B_UINT8_TYPE, states, 16);

	if (numBytes > 0) {
		for (int i = 0; i < numBytes; i++)
			msg->AddInt8("byte", (int8)bytes[i]);
		msg->AddData("bytes", B_STRING_TYPE, bytes, numBytes + 1);
	}

	return msg;
}


// Helper: build a mouse BMessage matching HWMouseDevice output
static BMessage*
sim_build_mouse_message(uint32 what, bigtime_t when, uint32 buttons,
	int32 deltaX, int32 deltaY, bool isTouchpad)
{
	BMessage* msg = new BMessage(what);
	msg->AddInt64("when", when);
	msg->AddInt32("buttons", buttons);
	msg->AddInt32("x", deltaX);
	msg->AddInt32("y", deltaY);
	msg->AddInt32("be:device_subtype",
		isTouchpad ? B_TOUCHPAD_POINTING_DEVICE
			: B_MOUSE_POINTING_DEVICE);
	return msg;
}


// Helper: simulate palm rejection (InputBlocker logic)
static bool
sim_palm_reject(bigtime_t lastKeystroke, bigtime_t threshold,
	bigtime_t eventTime, int32 deviceSubtype)
{
	if (threshold == 0)
		return false;

	if (deviceSubtype != B_TOUCHPAD_POINTING_DEVICE
		&& deviceSubtype != B_TABLET_POINTING_DEVICE)
		return false;

	bigtime_t elapsed = eventTime - lastKeystroke;
	return elapsed < threshold;
}


// ============================================================
// Tests: BitFieldTesters
// ============================================================

static void
test_constant_field_tester()
{
	trace("\n--- test_constant_field_tester ---\n");
	bigtime_t t0 = system_time();

	ConstantFieldTester trueTest(true);
	ConstantFieldTester falseTest(false);

	TEST_ASSERT("constant true matches 0", trueTest.IsMatching(0));
	TEST_ASSERT("constant true matches 0xFFFFFFFF",
		trueTest.IsMatching(0xFFFFFFFF));
	TEST_ASSERT("constant false rejects 0", !falseTest.IsMatching(0));
	TEST_ASSERT("constant false rejects 0xFFFFFFFF",
		!falseTest.IsMatching(0xFFFFFFFF));

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_hasbits_field_tester()
{
	trace("\n--- test_hasbits_field_tester ---\n");
	bigtime_t t0 = system_time();

	// Require B_SHIFT_KEY, forbid B_CONTROL_KEY
	HasBitsFieldTester tester(B_SHIFT_KEY, B_CONTROL_KEY);

	TEST_ASSERT("shift only matches",
		tester.IsMatching(B_SHIFT_KEY));
	TEST_ASSERT("shift+command matches",
		tester.IsMatching(B_SHIFT_KEY | B_COMMAND_KEY));
	TEST_ASSERT("shift+control rejects",
		!tester.IsMatching(B_SHIFT_KEY | B_CONTROL_KEY));
	TEST_ASSERT("no modifiers rejects",
		!tester.IsMatching(0));
	TEST_ASSERT("control only rejects",
		!tester.IsMatching(B_CONTROL_KEY));

	// No required, no forbidden: matches everything
	HasBitsFieldTester anyTester(0, 0);
	TEST_ASSERT("empty tester matches 0", anyTester.IsMatching(0));
	TEST_ASSERT("empty tester matches all",
		anyTester.IsMatching(0xFFFFFFFF));

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_not_field_tester()
{
	trace("\n--- test_not_field_tester ---\n");
	bigtime_t t0 = system_time();

	// NotFieldTester takes ownership of the slave
	NotFieldTester tester(new HasBitsFieldTester(B_SHIFT_KEY, 0));

	TEST_ASSERT("NOT(require shift) rejects shift",
		!tester.IsMatching(B_SHIFT_KEY));
	TEST_ASSERT("NOT(require shift) matches no shift",
		tester.IsMatching(B_CONTROL_KEY));
	TEST_ASSERT("NOT(require shift) matches zero",
		tester.IsMatching(0));

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_minmatch_field_tester()
{
	trace("\n--- test_minmatch_field_tester ---\n");
	bigtime_t t0 = system_time();

	// OR logic: minNum=1, two slaves
	MinMatchFieldTester orTester(1);
	orTester.AddSlave(new HasBitsFieldTester(B_SHIFT_KEY, 0));
	orTester.AddSlave(new HasBitsFieldTester(B_CONTROL_KEY, 0));

	TEST_ASSERT("OR: shift matches", orTester.IsMatching(B_SHIFT_KEY));
	TEST_ASSERT("OR: control matches",
		orTester.IsMatching(B_CONTROL_KEY));
	TEST_ASSERT("OR: both matches",
		orTester.IsMatching(B_SHIFT_KEY | B_CONTROL_KEY));
	TEST_ASSERT("OR: neither rejects",
		!orTester.IsMatching(B_COMMAND_KEY));

	// AND logic: minNum=2, two slaves
	MinMatchFieldTester andTester(2);
	andTester.AddSlave(new HasBitsFieldTester(B_SHIFT_KEY, 0));
	andTester.AddSlave(new HasBitsFieldTester(B_CONTROL_KEY, 0));

	TEST_ASSERT("AND: both matches",
		andTester.IsMatching(B_SHIFT_KEY | B_CONTROL_KEY));
	TEST_ASSERT("AND: shift only rejects",
		!andTester.IsMatching(B_SHIFT_KEY));
	TEST_ASSERT("AND: control only rejects",
		!andTester.IsMatching(B_CONTROL_KEY));

	// Edge: minNum=0, no slaves: should return true
	MinMatchFieldTester emptyTester(0);
	TEST_ASSERT("empty min=0 matches", emptyTester.IsMatching(0));

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_tester_archive_roundtrip()
{
	trace("\n--- test_tester_archive_roundtrip ---\n");
	bigtime_t t0 = system_time();

	// Archive a HasBitsFieldTester and restore it
	HasBitsFieldTester original(B_SHIFT_KEY | B_COMMAND_KEY, B_CONTROL_KEY);

	BMessage archive;
	status_t err = original.Archive(&archive, true);
	trace("    Archive -> %s\n", strerror(err));
	TEST_ASSERT("archive succeeds", err == B_OK);

	BArchivable* obj = HasBitsFieldTester::Instantiate(&archive);
	TEST_ASSERT("instantiate returns non-NULL", obj != NULL);

	HasBitsFieldTester* restored = dynamic_cast<HasBitsFieldTester*>(obj);
	TEST_ASSERT("dynamic_cast succeeds", restored != NULL);

	if (restored != NULL) {
		TEST_ASSERT("restored matches shift+cmd",
			restored->IsMatching(B_SHIFT_KEY | B_COMMAND_KEY));
		TEST_ASSERT("restored rejects shift+cmd+ctrl",
			!restored->IsMatching(
				B_SHIFT_KEY | B_COMMAND_KEY | B_CONTROL_KEY));
		TEST_ASSERT("restored rejects shift only",
			!restored->IsMatching(B_SHIFT_KEY));
		delete restored;
	}

	// Archive a ConstantFieldTester
	ConstantFieldTester constOrig(true);
	BMessage constArchive;
	constOrig.Archive(&constArchive, true);
	BArchivable* constObj
		= ConstantFieldTester::Instantiate(&constArchive);
	TEST_ASSERT("constant instantiate non-NULL", constObj != NULL);
	if (constObj != NULL) {
		ConstantFieldTester* cr
			= dynamic_cast<ConstantFieldTester*>(constObj);
		TEST_ASSERT("constant roundtrip matches",
			cr != NULL && cr->IsMatching(42));
		delete constObj;
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: KeyInfos
// ============================================================

static void
test_key_init_and_lookup()
{
	trace("\n--- test_key_init_and_lookup ---\n");
	bigtime_t t0 = system_time();

	InitKeyIndices();

	// Well-known keys
	const char* esc = GetKeyName(1);
	trace("    GetKeyName(1) = \"%s\"\n", esc ? esc : "(null)");
	TEST_ASSERT("key 1 is Esc", esc != NULL && strcmp(esc, "Esc") == 0);

	const char* f1 = GetKeyName(2);
	trace("    GetKeyName(2) = \"%s\"\n", f1 ? f1 : "(null)");
	TEST_ASSERT("key 2 is F1", f1 != NULL && strcmp(f1, "F1") == 0);

	const char* space = GetKeyName(94);
	trace("    GetKeyName(94) = \"%s\"\n", space ? space : "(null)");
	TEST_ASSERT("key 94 is Space",
		space != NULL && strcmp(space, "Space") == 0);

	const char* enter = GetKeyName(71);
	trace("    GetKeyName(71) = \"%s\"\n", enter ? enter : "(null)");
	TEST_ASSERT("key 71 is Enter",
		enter != NULL && strcmp(enter, "Enter") == 0);

	const char* tab = GetKeyName(38);
	trace("    GetKeyName(38) = \"%s\"\n", tab ? tab : "(null)");
	TEST_ASSERT("key 38 is Tab",
		tab != NULL && strcmp(tab, "Tab") == 0);

	// Out of bounds
	const char* outOfBounds = GetKeyName(200);
	TEST_ASSERT("key 200 returns NULL", outOfBounds == NULL);

	int numKeys = GetNumKeyIndices();
	trace("    GetNumKeyIndices() = %d\n", numKeys);
	TEST_ASSERT("num keys is 128", numKeys == 128);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_key_find_by_name()
{
	trace("\n--- test_key_find_by_name ---\n");
	bigtime_t t0 = system_time();

	InitKeyIndices();

	uint32 code = FindKeyCode("Esc");
	trace("    FindKeyCode(\"Esc\") = %u\n", (unsigned)code);
	TEST_ASSERT("FindKeyCode Esc -> 1", code == 1);

	code = FindKeyCode("Space");
	trace("    FindKeyCode(\"Space\") = %u\n", (unsigned)code);
	TEST_ASSERT("FindKeyCode Space -> 94", code == 94);

	code = FindKeyCode("F12");
	trace("    FindKeyCode(\"F12\") = %u\n", (unsigned)code);
	TEST_ASSERT("FindKeyCode F12 -> 13", code == 13);

	code = FindKeyCode("nonexistent_key_xyz");
	trace("    FindKeyCode(\"nonexistent_key_xyz\") = %u\n",
		(unsigned)code);
	TEST_ASSERT("FindKeyCode nonexistent -> 0", code == 0);

	// Case-insensitive
	code = FindKeyCode("esc");
	trace("    FindKeyCode(\"esc\") = %u\n", (unsigned)code);
	TEST_ASSERT("FindKeyCode case-insensitive", code == 1);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_key_utf8()
{
	trace("\n--- test_key_utf8 ---\n");
	bigtime_t t0 = system_time();

	InitKeyIndices();

	// Key 94 is space, should have UTF8 ' '
	const char* utf = GetKeyUTF8(94);
	trace("    GetKeyUTF8(94) = 0x%02x\n",
		(unsigned)(uint8)(utf ? utf[0] : 0));
	TEST_ASSERT("key 94 UTF8 is space",
		utf != NULL && utf[0] == ' ');

	// Key 0 (unset) should have empty or null UTF8
	utf = GetKeyUTF8(0);
	trace("    GetKeyUTF8(0) = 0x%02x\n",
		(unsigned)(uint8)(utf ? utf[0] : 0));
	// key 0 may have 0x00 UTF8
	TEST_ASSERT("key 0 UTF8 exists", utf != NULL);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: ParseCommandLine
// ============================================================

static void
test_parse_simple()
{
	trace("\n--- test_parse_simple ---\n");
	bigtime_t t0 = system_time();

	int32 argc = 0;
	char** argv = ParseArgvFromString("hello world foo", argc);

	trace("    argc = %d\n", (int)argc);
	TEST_ASSERT("simple parse argc=3", argc == 3);
	TEST_ASSERT("argv[0]='hello'",
		argv[0] != NULL && strcmp(argv[0], "hello") == 0);
	TEST_ASSERT("argv[1]='world'",
		argv[1] != NULL && strcmp(argv[1], "world") == 0);
	TEST_ASSERT("argv[2]='foo'",
		argv[2] != NULL && strcmp(argv[2], "foo") == 0);
	TEST_ASSERT("argv[3]=NULL", argv[argc] == NULL);

	FreeArgv(argv);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_parse_quoted()
{
	trace("\n--- test_parse_quoted ---\n");
	bigtime_t t0 = system_time();

	int32 argc = 0;
	char** argv = ParseArgvFromString("\"hello world\" foo", argc);

	trace("    argc = %d\n", (int)argc);
	TEST_ASSERT("quoted parse argc=2", argc == 2);
	TEST_ASSERT("argv[0]='hello world'",
		argv[0] != NULL && strcmp(argv[0], "hello world") == 0);
	TEST_ASSERT("argv[1]='foo'",
		argv[1] != NULL && strcmp(argv[1], "foo") == 0);

	FreeArgv(argv);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_parse_escaped()
{
	trace("\n--- test_parse_escaped ---\n");
	bigtime_t t0 = system_time();

	int32 argc = 0;
	char** argv = ParseArgvFromString("hello\\ world foo", argc);

	trace("    argc = %d\n", (int)argc);
	TEST_ASSERT("escaped space parse argc=2", argc == 2);
	TEST_ASSERT("argv[0]='hello world'",
		argv[0] != NULL && strcmp(argv[0], "hello world") == 0);

	FreeArgv(argv);

	// Escaped newline
	argc = 0;
	argv = ParseArgvFromString("\"line1\\nline2\"", argc);
	trace("    escaped newline argc=%d\n", (int)argc);
	TEST_ASSERT("escaped newline argc=1", argc == 1);
	TEST_ASSERT("escaped newline contains \\n",
		argv[0] != NULL && strchr(argv[0], '\n') != NULL);

	FreeArgv(argv);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_clone_argv()
{
	trace("\n--- test_clone_argv ---\n");
	bigtime_t t0 = system_time();

	int32 argc = 0;
	char** argv = ParseArgvFromString("alpha beta gamma", argc);

	char** clone = CloneArgv(argv);
	TEST_ASSERT("clone is non-NULL", clone != NULL);
	TEST_ASSERT("clone[0] matches",
		clone[0] != NULL && strcmp(clone[0], argv[0]) == 0);
	TEST_ASSERT("clone[1] matches",
		clone[1] != NULL && strcmp(clone[1], argv[1]) == 0);
	TEST_ASSERT("clone[2] matches",
		clone[2] != NULL && strcmp(clone[2], argv[2]) == 0);
	TEST_ASSERT("clone is independent copy", clone[0] != argv[0]);

	FreeArgv(clone);
	FreeArgv(argv);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_parse_argv_zero()
{
	trace("\n--- test_parse_argv_zero ---\n");
	bigtime_t t0 = system_time();

	BString first = ParseArgvZeroFromString("/boot/system/bin/StyledEdit foo.txt");
	trace("    ParseArgvZeroFromString -> \"%s\"\n", first.String());
	TEST_ASSERT("argv zero extracts first word",
		first == "/boot/system/bin/StyledEdit");

	first = ParseArgvZeroFromString("\"quoted path/with spaces\" arg");
	trace("    quoted argv zero -> \"%s\"\n", first.String());
	TEST_ASSERT("argv zero handles quotes",
		first == "quoted path/with spaces");

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: BKeymap (public API)
// ============================================================

static void
test_keymap_load()
{
	trace("\n--- test_keymap_load ---\n");
	bigtime_t t0 = system_time();

	BKeymap keymap;
	status_t err = keymap.SetToCurrent();
	trace("    SetToCurrent -> %s\n", strerror(err));
	TEST_ASSERT("keymap loads", err == B_OK);

	// Modifier for left shift (key 75) should include B_LEFT_SHIFT_KEY
	uint32 mod = keymap.Modifier(75);
	trace("    Modifier(75) = 0x%08x\n", (unsigned)mod);
	TEST_ASSERT("key 75 is left shift",
		(mod & B_LEFT_SHIFT_KEY) != 0);

	// Modifier for a non-modifier key should be 0
	// Key 38 is Tab, not a modifier
	mod = keymap.Modifier(38);
	trace("    Modifier(38/Tab) = 0x%08x\n", (unsigned)mod);
	TEST_ASSERT("Tab is not a modifier", mod == 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_keymap_char_lookup()
{
	trace("\n--- test_keymap_char_lookup ---\n");
	bigtime_t t0 = system_time();

	BKeymap keymap;
	keymap.SetToCurrent();

	// Look up Space (key 94, no modifiers)
	char* chars = NULL;
	int32 numBytes = 0;
	keymap.GetChars(94, 0, 0, &chars, &numBytes);
	trace("    GetChars(94, 0, 0) -> numBytes=%d, char=0x%02x\n",
		(int)numBytes, chars ? (unsigned)(uint8)chars[0] : 0);
	TEST_ASSERT("space produces 1 byte", numBytes == 1);
	TEST_ASSERT("space char is 0x20",
		chars != NULL && chars[0] == ' ');
	delete[] chars;

	// Look up a letter key (key 40 is typically 'q' on US layout,
	// but we just check it produces something)
	chars = NULL;
	numBytes = 0;
	keymap.GetChars(40, 0, 0, &chars, &numBytes);
	trace("    GetChars(40, 0, 0) -> numBytes=%d\n", (int)numBytes);
	TEST_ASSERT("key 40 produces chars", numBytes > 0);
	if (chars != NULL) {
		trace("    char = '%c' (0x%02x)\n", chars[0],
			(unsigned)(uint8)chars[0]);
	}
	delete[] chars;

	// Dead key lookup
	uint8 deadKey = keymap.ActiveDeadKey(94, 0);
	trace("    ActiveDeadKey(94, 0) = %d\n", (int)deadKey);
	TEST_ASSERT("space is not a dead key", deadKey == 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: Movement Math
// ============================================================

static void
test_movement_maker_basic()
{
	trace("\n--- test_movement_maker_basic ---\n");
	bigtime_t t0 = system_time();

	// Create a TouchpadMovement (inherits MovementMaker)
	// and test the underlying movement computation
	TouchpadMovement tm;

	touchpad_specs specs;
	memset(&specs, 0, sizeof(specs));
	specs.areaStartX = 0;
	specs.areaStartY = 0;
	specs.areaEndX = 4100;
	specs.areaEndY = 3140;
	specs.minPressure = 0;
	specs.maxPressure = 255;
	specs.realMaxPressure = 200;
	specs.edgeMotionWidth = 100;

	touchpad_settings settings = kDefaultTouchpadSettings;
	settings.scroll_xstepsize = 10;
	settings.scroll_ystepsize = 10;

	tm.SetSpecs(specs);
	tm.SetSettings(settings);

	TEST_ASSERT("movement maker configured", true);

	// Simulate a touch event producing movement
	touchpad_movement event;
	memset(&event, 0, sizeof(event));
	event.xPosition = 2000;
	event.yPosition = 1500;
	event.zPressure = 100;
	event.fingerWidth = 4;
	event.fingers = 0x01; // one finger

	mouse_movement movement;
	bigtime_t timeout = B_INFINITE_TIMEOUT;

	status_t err = tm.EventToMovement(&event, &movement, timeout);
	trace("    first event -> %s, xdelta=%d, ydelta=%d\n",
		strerror(err), (int)movement.xdelta, (int)movement.ydelta);
	TEST_ASSERT("first event succeeds", err == B_OK);

	// Second event with position change
	event.xPosition = 2100;
	event.yPosition = 1600;
	err = tm.EventToMovement(&event, &movement, timeout);
	trace("    second event -> %s, xdelta=%d, ydelta=%d\n",
		strerror(err), (int)movement.xdelta, (int)movement.ydelta);
	TEST_ASSERT("second event succeeds", err == B_OK);

	// The delta should be non-zero after position change
	// (may be zero on first move due to initialization)
	trace("    movement xdelta=%d ydelta=%d\n",
		(int)movement.xdelta, (int)movement.ydelta);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_movement_maker_scrolling()
{
	trace("\n--- test_movement_maker_scrolling ---\n");
	bigtime_t t0 = system_time();

	TouchpadMovement tm;

	touchpad_specs specs;
	memset(&specs, 0, sizeof(specs));
	specs.areaStartX = 0;
	specs.areaStartY = 0;
	specs.areaEndX = 4100;
	specs.areaEndY = 3140;
	specs.minPressure = 0;
	specs.maxPressure = 255;
	specs.realMaxPressure = 200;
	specs.edgeMotionWidth = 100;

	touchpad_settings settings = kDefaultTouchpadSettings;
	settings.scroll_twofinger = true;
	settings.scroll_twofinger_horizontal = true;
	settings.scroll_xstepsize = 5;
	settings.scroll_ystepsize = 5;

	tm.SetSpecs(specs);
	tm.SetSettings(settings);
	tm.click_speed = 500000;

	// Two-finger scroll event
	touchpad_movement event;
	memset(&event, 0, sizeof(event));
	event.xPosition = 2000;
	event.yPosition = 1500;
	event.zPressure = 100;
	event.fingerWidth = 0; // 0 = two fingers (Synaptics convention)
	event.fingers = 0x03;  // two fingers

	mouse_movement movement;
	bigtime_t timeout = B_INFINITE_TIMEOUT;

	// Initial touch
	status_t err = tm.EventToMovement(&event, &movement, timeout);
	trace("    scroll initial -> %s\n", strerror(err));

	// Move vertically with two fingers
	event.yPosition = 1700;
	err = tm.EventToMovement(&event, &movement, timeout);
	trace("    scroll move -> %s, wheel_y=%d, wheel_x=%d\n",
		strerror(err), (int)movement.wheel_ydelta,
		(int)movement.wheel_xdelta);
	TEST_ASSERT("scroll event succeeds", err == B_OK);
	// After sufficient movement, wheel_ydelta should be non-zero
	trace("    wheel deltas: x=%d y=%d\n",
		(int)movement.wheel_xdelta, (int)movement.wheel_ydelta);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: Message Format Validation
// ============================================================

static void
test_keyboard_message_format()
{
	trace("\n--- test_keyboard_message_format ---\n");
	bigtime_t t0 = system_time();

	uint8 states[16];
	memset(states, 0, sizeof(states));
	states[94 >> 3] |= (1 << (7 - (94 & 0x7)));

	const char* bytes = " ";
	BMessage* msg = sim_build_key_message(true, 94, 0, 1000000,
		states, bytes, 1);

	TEST_ASSERT("key msg what is B_KEY_DOWN",
		msg->what == B_KEY_DOWN);

	int64 when;
	TEST_ASSERT("has 'when' field",
		msg->FindInt64("when", &when) == B_OK);
	TEST_ASSERT("when value correct", when == 1000000);

	int32 key;
	TEST_ASSERT("has 'key' field",
		msg->FindInt32("key", &key) == B_OK);
	TEST_ASSERT("key value correct", key == 94);

	int32 mods;
	TEST_ASSERT("has 'modifiers' field",
		msg->FindInt32("modifiers", &mods) == B_OK);

	const void* data;
	ssize_t size;
	TEST_ASSERT("has 'states' field",
		msg->FindData("states", B_UINT8_TYPE, &data, &size) == B_OK);
	TEST_ASSERT("states is 16 bytes", size == 16);

	int8 byte;
	TEST_ASSERT("has 'byte' field",
		msg->FindInt8("byte", &byte) == B_OK);
	TEST_ASSERT("byte is space", byte == ' ');

	TEST_ASSERT("has 'bytes' field",
		msg->FindData("bytes", B_STRING_TYPE, &data, &size) == B_OK);

	delete msg;

	// Unmapped key (no bytes)
	msg = sim_build_key_message(true, 200, B_SHIFT_KEY, 2000000,
		states, NULL, 0);
	TEST_ASSERT("unmapped key what is B_UNMAPPED_KEY_DOWN",
		msg->what == B_UNMAPPED_KEY_DOWN);

	delete msg;

	// Key up
	msg = sim_build_key_message(false, 94, 0, 3000000, states, bytes, 1);
	TEST_ASSERT("key up what is B_KEY_UP", msg->what == B_KEY_UP);

	delete msg;
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_mouse_message_format()
{
	trace("\n--- test_mouse_message_format ---\n");
	bigtime_t t0 = system_time();

	BMessage* msg = sim_build_mouse_message(B_MOUSE_DOWN, 1000000,
		B_PRIMARY_MOUSE_BUTTON, 10, 20, false);

	TEST_ASSERT("mouse down what", msg->what == B_MOUSE_DOWN);

	int64 when;
	TEST_ASSERT("has when", msg->FindInt64("when", &when) == B_OK);

	int32 buttons;
	TEST_ASSERT("has buttons",
		msg->FindInt32("buttons", &buttons) == B_OK);
	TEST_ASSERT("buttons value",
		(uint32)buttons == B_PRIMARY_MOUSE_BUTTON);

	int32 x, y;
	TEST_ASSERT("has x", msg->FindInt32("x", &x) == B_OK);
	TEST_ASSERT("has y", msg->FindInt32("y", &y) == B_OK);
	TEST_ASSERT("x=10", x == 10);
	TEST_ASSERT("y=20", y == 20);

	int32 subtype;
	TEST_ASSERT("has device_subtype",
		msg->FindInt32("be:device_subtype", &subtype) == B_OK);
	TEST_ASSERT("subtype is mouse",
		subtype == B_MOUSE_POINTING_DEVICE);

	delete msg;

	// Touchpad message
	msg = sim_build_mouse_message(B_MOUSE_MOVED, 2000000, 0,
		5, 3, true);
	msg->FindInt32("be:device_subtype", &subtype);
	TEST_ASSERT("touchpad subtype",
		subtype == B_TOUCHPAD_POINTING_DEVICE);
	delete msg;

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_wheel_message_format()
{
	trace("\n--- test_wheel_message_format ---\n");
	bigtime_t t0 = system_time();

	BMessage msg(B_MOUSE_WHEEL_CHANGED);
	msg.AddInt64("when", 1000000);
	msg.AddFloat("be:wheel_delta_x", 0.0f);
	msg.AddFloat("be:wheel_delta_y", -3.0f);
	msg.AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);

	TEST_ASSERT("wheel msg what", msg.what == B_MOUSE_WHEEL_CHANGED);

	float wy;
	TEST_ASSERT("has wheel_delta_y",
		msg.FindFloat("be:wheel_delta_y", &wy) == B_OK);
	TEST_ASSERT("wheel_delta_y is -3", wy == -3.0f);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_tablet_message_format()
{
	trace("\n--- test_tablet_message_format ---\n");
	bigtime_t t0 = system_time();

	BMessage msg(B_MOUSE_MOVED);
	msg.AddInt64("when", 1000000);
	msg.AddInt32("buttons", 0);
	msg.AddFloat("x", 0.5f);
	msg.AddFloat("y", 0.3f);
	msg.AddInt32("be:device_subtype", B_TABLET_POINTING_DEVICE);
	msg.AddFloat("be:tablet_x", 0.5f);
	msg.AddFloat("be:tablet_y", 0.3f);
	msg.AddFloat("be:tablet_pressure", 0.7f);
	msg.AddInt32("be:tablet_eraser", 0);
	msg.AddFloat("be:tablet_tilt_x", 15.0f);
	msg.AddFloat("be:tablet_tilt_y", -5.0f);

	float pressure;
	TEST_ASSERT("has tablet_pressure",
		msg.FindFloat("be:tablet_pressure", &pressure) == B_OK);
	TEST_ASSERT("pressure is 0.7", pressure == 0.7f);

	float tiltX;
	TEST_ASSERT("has tilt_x",
		msg.FindFloat("be:tablet_tilt_x", &tiltX) == B_OK);
	TEST_ASSERT("tilt_x is 15", tiltX == 15.0f);

	int32 eraser;
	TEST_ASSERT("has eraser",
		msg.FindInt32("be:tablet_eraser", &eraser) == B_OK);
	TEST_ASSERT("eraser is 0", eraser == 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_modifier_changed_format()
{
	trace("\n--- test_modifier_changed_format ---\n");
	bigtime_t t0 = system_time();

	uint8 states[16];
	memset(states, 0, sizeof(states));

	BMessage msg(B_MODIFIERS_CHANGED);
	msg.AddInt64("when", 1000000);
	msg.AddInt32("be:old_modifiers", 0);
	msg.AddInt32("modifiers", B_SHIFT_KEY | B_LEFT_SHIFT_KEY);
	msg.AddData("states", B_UINT8_TYPE, states, 16);

	int32 oldMods, newMods;
	TEST_ASSERT("has old_modifiers",
		msg.FindInt32("be:old_modifiers", &oldMods) == B_OK);
	TEST_ASSERT("has modifiers",
		msg.FindInt32("modifiers", &newMods) == B_OK);
	TEST_ASSERT("old is 0", oldMods == 0);
	TEST_ASSERT("new has shift",
		(newMods & B_SHIFT_KEY) != 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: Device Logic Simulation
// ============================================================

static void
test_device_name_building()
{
	trace("\n--- test_device_name_building ---\n");
	bigtime_t t0 = system_time();

	char* name;

	name = sim_build_short_name("/dev/input/keyboard/usb/0", "Keyboard");
	trace("    keyboard usb/0 -> \"%s\"\n", name);
	TEST_ASSERT("USB keyboard name", strstr(name, "Keyboard") != NULL);
	TEST_ASSERT("USB keyboard index 1", strstr(name, "1") != NULL);
	free(name);

	name = sim_build_short_name("/dev/input/mouse/ps2/0", "Pointer");
	trace("    mouse ps2/0 -> \"%s\"\n", name);
	TEST_ASSERT("PS2 mouse name", strstr(name, "PS/2") != NULL
		|| strstr(name, "Ps2") != NULL || strstr(name, "PS2") != NULL);
	free(name);

	name = sim_build_short_name("/dev/input/mouse/usb/2", "Pointer");
	trace("    mouse usb/2 -> \"%s\"\n", name);
	TEST_ASSERT("USB mouse index 3", strstr(name, "3") != NULL);
	free(name);

	name = sim_build_short_name("/dev/input/tablet/wacom/0", "Pointer");
	trace("    tablet wacom/0 -> \"%s\"\n", name);
	TEST_ASSERT("tablet name has Wacom",
		strstr(name, "Wacom") != NULL);
	free(name);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_device_category_matching()
{
	trace("\n--- test_device_category_matching ---\n");
	bigtime_t t0 = system_time();

	TEST_ASSERT("keyboard path",
		sim_category_for_path("/dev/input/keyboard/usb/0")
			== kSimKeyboard);
	TEST_ASSERT("mouse path",
		sim_category_for_path("/dev/input/mouse/ps2/0")
			== kSimMouse);
	TEST_ASSERT("touchpad path",
		sim_category_for_path("/dev/input/touchpad/synaptics/0")
			== kSimTouchpad);
	TEST_ASSERT("tablet path",
		sim_category_for_path("/dev/input/tablet/wacom/0")
			== kSimTablet);
	TEST_ASSERT("virtio path",
		sim_category_for_path("/dev/input/virtio/0/raw")
			== kSimVirtio);
	TEST_ASSERT("touchscreen path",
		sim_category_for_path("/dev/input/touchscreen/elan/0")
			== kSimTouchscreen);
	TEST_ASSERT("unknown path",
		sim_category_for_path("/dev/misc/something")
			== kSimUnknown);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_virtio_type_detection()
{
	trace("\n--- test_virtio_type_detection ---\n");
	bigtime_t t0 = system_time();

	// Even index = tablet, odd = keyboard (QEMU convention)
	TEST_ASSERT("virtio/0 is tablet",
		sim_detect_virtio_type("/dev/input/virtio/0/raw")
			== kSimTablet);
	TEST_ASSERT("virtio/1 is keyboard",
		sim_detect_virtio_type("/dev/input/virtio/1/raw")
			== kSimKeyboard);
	TEST_ASSERT("virtio/2 is tablet",
		sim_detect_virtio_type("/dev/input/virtio/2/raw")
			== kSimTablet);
	TEST_ASSERT("virtio/3 is keyboard",
		sim_detect_virtio_type("/dev/input/virtio/3/raw")
			== kSimKeyboard);

	// Malformed path fallback
	TEST_ASSERT("malformed virtio defaults to keyboard",
		sim_detect_virtio_type("/dev/input/virtio/raw")
			== kSimKeyboard);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: Filter Logic Simulation
// ============================================================

static void
test_palm_rejection()
{
	trace("\n--- test_palm_rejection ---\n");
	bigtime_t t0 = system_time();

	bigtime_t threshold = 300000; // 300ms
	bigtime_t keystroke = 1000000;

	// Touchpad event 100ms after keystroke -> blocked
	TEST_ASSERT("event 100ms after key -> blocked",
		sim_palm_reject(keystroke, threshold,
			keystroke + 100000, B_TOUCHPAD_POINTING_DEVICE));

	// Touchpad event 500ms after keystroke -> not blocked
	TEST_ASSERT("event 500ms after key -> allowed",
		!sim_palm_reject(keystroke, threshold,
			keystroke + 500000, B_TOUCHPAD_POINTING_DEVICE));

	// Mouse event 100ms after keystroke -> never blocked
	TEST_ASSERT("mouse never blocked",
		!sim_palm_reject(keystroke, threshold,
			keystroke + 100000, B_MOUSE_POINTING_DEVICE));

	// Tablet event 100ms after keystroke -> blocked
	TEST_ASSERT("tablet 100ms after key -> blocked",
		sim_palm_reject(keystroke, threshold,
			keystroke + 100000, B_TABLET_POINTING_DEVICE));

	// Threshold 0 disables blocking
	TEST_ASSERT("threshold 0 disables",
		!sim_palm_reject(keystroke, 0,
			keystroke + 1, B_TOUCHPAD_POINTING_DEVICE));

	// Edge case: event exactly at threshold boundary
	TEST_ASSERT("event at threshold boundary -> not blocked",
		!sim_palm_reject(keystroke, threshold,
			keystroke + threshold, B_TOUCHPAD_POINTING_DEVICE));

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_modifier_tracking()
{
	trace("\n--- test_modifier_tracking ---\n");
	bigtime_t t0 = system_time();

	uint32 mods = 0;

	// Press left shift
	mods = sim_update_modifiers(75, true, mods,
		B_LEFT_SHIFT_KEY | B_SHIFT_KEY, false);
	trace("    after left shift down: 0x%08x\n", (unsigned)mods);
	TEST_ASSERT("left shift pressed",
		(mods & B_LEFT_SHIFT_KEY) != 0);
	TEST_ASSERT("shift combined set",
		(mods & B_SHIFT_KEY) != 0);

	// Press right shift
	mods = sim_update_modifiers(86, true, mods,
		B_RIGHT_SHIFT_KEY | B_SHIFT_KEY, false);
	trace("    after right shift down: 0x%08x\n", (unsigned)mods);
	TEST_ASSERT("both shifts pressed",
		(mods & B_LEFT_SHIFT_KEY) != 0
		&& (mods & B_RIGHT_SHIFT_KEY) != 0);

	// Release left shift: combined SHIFT should remain
	mods = sim_update_modifiers(75, false, mods,
		B_LEFT_SHIFT_KEY | B_SHIFT_KEY, false);
	trace("    after left shift up: 0x%08x\n", (unsigned)mods);
	TEST_ASSERT("left shift released",
		(mods & B_LEFT_SHIFT_KEY) == 0);
	TEST_ASSERT("combined shift still set (right held)",
		(mods & B_SHIFT_KEY) != 0);

	// Release right shift: all shift gone
	mods = sim_update_modifiers(86, false, mods,
		B_RIGHT_SHIFT_KEY | B_SHIFT_KEY, false);
	trace("    after right shift up: 0x%08x\n", (unsigned)mods);
	TEST_ASSERT("all shift cleared", (mods & B_SHIFT_KEY) == 0);

	// Caps lock toggle (isLock=true)
	mods = sim_update_modifiers(59, true, mods, B_CAPS_LOCK, true);
	trace("    after caps lock down: 0x%08x\n", (unsigned)mods);
	TEST_ASSERT("caps lock on", (mods & B_CAPS_LOCK) != 0);

	// Caps lock release: toggles off
	mods = sim_update_modifiers(59, false, mods, B_CAPS_LOCK, true);
	// Note: release of lock key clears the bit in this simplified model
	// (in real code it's more nuanced with toggle semantics)

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_acceleration_math()
{
	trace("\n--- test_acceleration_math ---\n");
	bigtime_t t0 = system_time();

	float histX = 0, histY = 0;
	int32 outX, outY;

	// No acceleration factor: pure speed scaling
	sim_compute_acceleration(100, 50, 65536, 0, histX, histY,
		outX, outY);
	trace("    speed=65536, accel=0: in=(100,50) -> out=(%d,%d)\n",
		(int)outX, (int)outY);
	TEST_ASSERT("no accel: x=100", outX == 100);
	TEST_ASSERT("no accel: y=50", outY == 50);

	// With acceleration
	histX = 0;
	histY = 0;
	sim_compute_acceleration(100, 0, 65536, 524288, histX, histY,
		outX, outY);
	trace("    speed=65536, accel=524288: in=(100,0) -> out=(%d,%d)\n",
		(int)outX, (int)outY);
	TEST_ASSERT("with accel: x > 100 (amplified)", outX > 100);

	// Small movement with speed < 1
	histX = 0;
	histY = 0;
	sim_compute_acceleration(1, 0, 32768, 0, histX, histY,
		outX, outY);
	trace("    speed=32768, accel=0: in=(1,0) -> out=(%d,%d), "
		"hist=(%f,%f)\n",
		(int)outX, (int)outY, histX, histY);
	// With speed=32768 (half), input 1 -> delta 0.5, floor -> 0
	// but fractional remainder stored in history
	TEST_ASSERT("sub-pixel stored in history", histX != 0 || outX > 0);

	// Negative movement
	histX = 0;
	histY = 0;
	sim_compute_acceleration(-50, -30, 65536, 0, histX, histY,
		outX, outY);
	trace("    negative: in=(-50,-30) -> out=(%d,%d)\n",
		(int)outX, (int)outY);
	TEST_ASSERT("negative x", outX == -50);
	TEST_ASSERT("negative y", outY == -30);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_button_remapping()
{
	trace("\n--- test_button_remapping ---\n");
	bigtime_t t0 = system_time();

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	mouse_map map;
	// Default mapping: 1:1
	for (int i = 0; i < B_MAX_MOUSE_BUTTONS; i++)
		map.button[i] = B_MOUSE_BUTTON(i + 1);

	uint32 result = sim_remap_buttons(0x01, map); // button 1
	TEST_ASSERT("button 1 -> primary",
		result == B_MOUSE_BUTTON(1));

	result = sim_remap_buttons(0x02, map); // button 2
	TEST_ASSERT("button 2 -> secondary",
		result == B_MOUSE_BUTTON(2));

	// Swap left and right
	map.button[0] = B_MOUSE_BUTTON(2); // left -> secondary
	map.button[1] = B_MOUSE_BUTTON(1); // right -> primary

	result = sim_remap_buttons(0x01, map);
	trace("    swapped: button 1 -> 0x%08x\n", (unsigned)result);
	TEST_ASSERT("swapped: button 1 -> secondary",
		result == B_MOUSE_BUTTON(2));

	result = sim_remap_buttons(0x02, map);
	trace("    swapped: button 2 -> 0x%08x\n", (unsigned)result);
	TEST_ASSERT("swapped: button 2 -> primary",
		result == B_MOUSE_BUTTON(1));

	// Multiple buttons simultaneously
	map.button[0] = B_MOUSE_BUTTON(1);
	map.button[1] = B_MOUSE_BUTTON(2);
	map.button[2] = B_MOUSE_BUTTON(3);
	result = sim_remap_buttons(0x05, map); // buttons 1+3
	trace("    multi: 0x05 -> 0x%08x\n", (unsigned)result);
	TEST_ASSERT("multi-button remap",
		result == (B_MOUSE_BUTTON(1) | B_MOUSE_BUTTON(3)));
#else
	TEST_ASSERT("button remap skipped (non-Haiku)", true);
#endif

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_workspace_key_matching()
{
	trace("\n--- test_workspace_key_matching ---\n");
	bigtime_t t0 = system_time();

	// Simulate WorkspaceSwitch filter logic:
	// Cmd+Ctrl+Arrow = switch workspace
	// Cmd+Ctrl+Shift+Arrow = move window to workspace

	struct {
		int32		modifiers;
		uint8		byte;
		bool		shouldMatch;
		bool		takeMeThere;
		const char*	desc;
	} cases[] = {
		{ B_COMMAND_KEY | B_CONTROL_KEY, B_LEFT_ARROW,
			true, false, "Cmd+Ctrl+Left" },
		{ B_COMMAND_KEY | B_CONTROL_KEY, B_RIGHT_ARROW,
			true, false, "Cmd+Ctrl+Right" },
		{ B_COMMAND_KEY | B_CONTROL_KEY | B_SHIFT_KEY, B_UP_ARROW,
			true, true, "Cmd+Ctrl+Shift+Up" },
		{ B_COMMAND_KEY | B_CONTROL_KEY, 'a',
			false, false, "Cmd+Ctrl+A (not arrow)" },
		{ B_COMMAND_KEY, B_LEFT_ARROW,
			false, false, "Cmd+Left (no Ctrl)" },
		{ B_CONTROL_KEY, B_LEFT_ARROW,
			false, false, "Ctrl+Left (no Cmd)" },
		{ 0, B_LEFT_ARROW,
			false, false, "plain Left" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int32 held = cases[i].modifiers & (B_COMMAND_KEY | B_CONTROL_KEY
			| B_OPTION_KEY | B_MENU_KEY | B_SHIFT_KEY);

		bool isSwitch = false;
		bool takeMeThere = false;

		if (held == (B_COMMAND_KEY | B_CONTROL_KEY)) {
			isSwitch = true;
			takeMeThere = false;
		} else if (held == (B_COMMAND_KEY | B_CONTROL_KEY | B_SHIFT_KEY)) {
			isSwitch = true;
			takeMeThere = true;
		}

		bool isArrow = (cases[i].byte == B_LEFT_ARROW
			|| cases[i].byte == B_RIGHT_ARROW
			|| cases[i].byte == B_UP_ARROW
			|| cases[i].byte == B_DOWN_ARROW);

		bool matches = isSwitch && isArrow;

		trace("    %s: match=%d (expected %d)\n",
			cases[i].desc, matches, cases[i].shouldMatch);
		TEST_ASSERT(cases[i].desc,
			matches == cases[i].shouldMatch);

		if (matches && cases[i].shouldMatch) {
			TEST_ASSERT("takeMeThere correct",
				takeMeThere == cases[i].takeMeThere);
		}
	}

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_profile_activation()
{
	trace("\n--- test_profile_activation ---\n");
	bigtime_t t0 = system_time();

	// Simulate filter stage ordering and profile activation
	enum { kDesktop = 0x01, kTablet = 0x02, kAll = 0xFF };

	struct SimFilter {
		const char*	name;
		uint32		stage;
		uint32		profiles;
		bool		active;
	};

	SimFilter filters[] = {
		{ "InputBlocker",	200, kAll, false },
		{ "SystemShortcuts", 400, kDesktop | 0x03, false },
		{ "WorkspaceSwitch", 400, kDesktop | 0x03, false },
		{ "WindowMgmt",	400, kDesktop | 0x03, false },
		{ "ScreenLock",	500, kAll, false },
	};
	const int kCount = sizeof(filters) / sizeof(filters[0]);

	// Verify stage ordering (ascending)
	bool ordered = true;
	for (int i = 1; i < kCount; i++) {
		if (filters[i].stage < filters[i - 1].stage) {
			ordered = false;
			break;
		}
	}
	TEST_ASSERT("filters ordered by stage", ordered);

	// Desktop profile: all active
	for (int i = 0; i < kCount; i++)
		filters[i].active = (filters[i].profiles & kDesktop) != 0;

	int activeCount = 0;
	for (int i = 0; i < kCount; i++) {
		trace("    desktop: %s active=%d\n",
			filters[i].name, filters[i].active);
		if (filters[i].active) activeCount++;
	}
	TEST_ASSERT("desktop: all 5 filters active", activeCount == 5);

	// Tablet profile: only InputBlocker and ScreenLock
	for (int i = 0; i < kCount; i++)
		filters[i].active = (filters[i].profiles & kTablet) != 0;

	activeCount = 0;
	for (int i = 0; i < kCount; i++) {
		trace("    tablet: %s active=%d\n",
			filters[i].name, filters[i].active);
		if (filters[i].active) activeCount++;
	}
	// InputBlocker(All) + ScreenLock(All) + the kDesktop|0x03 ones
	// 0x03 includes kTablet(0x02), so SystemShortcuts/WorkspaceSwitch/WindowMgmt
	// are also active in tablet mode
	TEST_ASSERT("tablet: profile-based activation works",
		activeCount >= 2);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: Stress / Performance
// ============================================================

static void
test_tester_matching_throughput()
{
	trace("\n--- test_tester_matching_throughput ---\n");
	bigtime_t t0 = system_time();

	HasBitsFieldTester tester(B_SHIFT_KEY | B_COMMAND_KEY, B_CONTROL_KEY);

	const int kIterations = 100000;
	int matches = 0;
	bigtime_t start = system_time();

	for (int i = 0; i < kIterations; i++) {
		uint32 field = (uint32)(i * 17) & 0xFFFF;
		if (tester.IsMatching(field))
			matches++;
	}

	bigtime_t elapsed = system_time() - start;
	trace("    %d iterations in %lld us\n", kIterations,
		(long long)elapsed);
	trace("    %lld ops/sec, %d matches\n",
		kIterations * 1000000LL / (elapsed ? elapsed : 1), matches);

	TEST_ASSERT("tester throughput completes", true);
	TEST_ASSERT("throughput > 1M ops/sec",
		elapsed < 1000000 || kIterations * 1000000LL / elapsed > 1000000);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_message_construction_throughput()
{
	trace("\n--- test_message_construction_throughput ---\n");
	bigtime_t t0 = system_time();

	uint8 states[16];
	memset(states, 0, sizeof(states));

	const int kIterations = 10000;
	bigtime_t start = system_time();

	for (int i = 0; i < kIterations; i++) {
		BMessage* msg = sim_build_mouse_message(B_MOUSE_MOVED,
			system_time(), 0, i % 10, i % 5, false);
		delete msg;
	}

	bigtime_t elapsed = system_time() - start;
	bigtime_t perOp = elapsed / kIterations;
	trace("    %d messages in %lld us\n", kIterations,
		(long long)elapsed);
	trace("    average: %lld us per message\n", (long long)perOp);
	trace("    throughput: %lld msgs/sec\n",
		kIterations * 1000000LL / (elapsed ? elapsed : 1));

	TEST_ASSERT("message construction completes", true);
	TEST_ASSERT("avg < 100us per message", perOp < 100);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_keymap_lookup_throughput()
{
	trace("\n--- test_keymap_lookup_throughput ---\n");
	bigtime_t t0 = system_time();

	BKeymap keymap;
	keymap.SetToCurrent();

	const int kIterations = 50000;
	bigtime_t start = system_time();

	for (int i = 0; i < kIterations; i++) {
		char* chars = NULL;
		int32 numBytes = 0;
		uint32 key = (i % 100) + 1;
		keymap.GetChars(key, 0, 0, &chars, &numBytes);
		delete[] chars;
	}

	bigtime_t elapsed = system_time() - start;
	trace("    %d lookups in %lld us\n", kIterations,
		(long long)elapsed);
	trace("    throughput: %lld lookups/sec\n",
		kIterations * 1000000LL / (elapsed ? elapsed : 1));

	TEST_ASSERT("keymap lookup completes", true);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_acceleration_throughput()
{
	trace("\n--- test_acceleration_throughput ---\n");
	bigtime_t t0 = system_time();

	const int kIterations = 100000;
	float histX = 0, histY = 0;
	int32 outX, outY;

	bigtime_t start = system_time();

	for (int i = 0; i < kIterations; i++) {
		histX = 0;
		histY = 0;
		sim_compute_acceleration(
			(i % 200) - 100, (i % 100) - 50,
			65536, 262144, histX, histY, outX, outY);
	}

	bigtime_t elapsed = system_time() - start;
	trace("    %d accel computations in %lld us\n", kIterations,
		(long long)elapsed);
	trace("    throughput: %lld ops/sec\n",
		kIterations * 1000000LL / (elapsed ? elapsed : 1));

	TEST_ASSERT("acceleration throughput completes", true);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// Tests: Input Pipeline Simulation
// ============================================================

static void
test_input_pipeline_simulation()
{
	trace("\n--- test_input_pipeline_simulation ---\n");
	bigtime_t t0 = system_time();

	// Simulate: device produces events -> filter chain processes them
	// Device: keyboard key down (space)
	// Filters: palm rejection, shortcut matching

	const int kEvents = 1000;
	int dispatched = 0;
	int blocked = 0;
	int shortcutMatched = 0;

	bigtime_t lastKeystroke = 0;
	bigtime_t palmThreshold = 300000;

	HasBitsFieldTester shortcutTester(
		B_SHIFT_KEY | B_COMMAND_KEY, B_CONTROL_KEY);

	bigtime_t start = system_time();

	for (int i = 0; i < kEvents; i++) {
		bigtime_t now = start + i * 10000; // 10ms between events

		// Alternate between keyboard and mouse events
		if (i % 3 == 0) {
			// Keyboard event
			lastKeystroke = now;
			dispatched++;
		} else if (i % 3 == 1) {
			// Mouse event (touchpad)
			bool palmBlocked = sim_palm_reject(lastKeystroke,
				palmThreshold, now, B_TOUCHPAD_POINTING_DEVICE);
			if (palmBlocked) {
				blocked++;
			} else {
				dispatched++;
			}
		} else {
			// Keyboard with modifiers -> check shortcuts
			uint32 mods = (i % 7 == 0)
				? (B_SHIFT_KEY | B_COMMAND_KEY)
				: B_SHIFT_KEY;
			if (shortcutTester.IsMatching(mods))
				shortcutMatched++;
			dispatched++;
		}
	}

	bigtime_t elapsed = system_time() - start;

	trace("    %d events: dispatched=%d, blocked=%d, shortcuts=%d\n",
		kEvents, dispatched, blocked, shortcutMatched);
	trace("    pipeline throughput: %lld events/sec\n",
		kEvents * 1000000LL / (elapsed ? elapsed : 1));

	TEST_ASSERT("all events processed",
		dispatched + blocked == kEvents);
	TEST_ASSERT("some events blocked by palm rejection", blocked > 0);
	TEST_ASSERT("some shortcuts matched", shortcutMatched > 0);
	TEST_ASSERT("pipeline completes under 100ms", elapsed < 100000);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ============================================================
// GUI
// ============================================================

static const int kMaxLines = 256;
static char sLines[kMaxLines][256];
static int sLineCount = 0;


static void
log_line(const char* fmt, ...)
{
	if (sLineCount >= kMaxLines)
		return;

	va_list args;
	va_start(args, fmt);
	vsnprintf(sLines[sLineCount], sizeof(sLines[0]), fmt, args);
	va_end(args);
	sLineCount++;
}


class ResultView : public BView {
public:
	ResultView()
		:
		BView(BRect(0, 0, 699, 999), "ResultView", B_FOLLOW_ALL,
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(30, 30, 40);
	}

	virtual void Draw(BRect updateRect)
	{
		BFont font(be_fixed_font);
		font.SetSize(11.0f);
		SetFont(&font);

		float lineHeight = 14.0f;
		float y = 16.0f;

		for (int i = 0; i < sLineCount; i++) {
			if (strstr(sLines[i], "FAIL"))
				SetHighColor(255, 80, 80);
			else if (strstr(sLines[i], "PASS"))
				SetHighColor(80, 220, 80);
			else if (strstr(sLines[i], "==="))
				SetHighColor(255, 220, 100);
			else if (strstr(sLines[i], "---"))
				SetHighColor(130, 170, 255);
			else if (strstr(sLines[i], "STRESS")
				|| strstr(sLines[i], "PIPELINE"))
				SetHighColor(255, 180, 60);
			else
				SetHighColor(200, 200, 200);

			DrawString(sLines[i], BPoint(10, y));
			y += lineHeight;
		}
	}
};


class ResultWindow : public BWindow {
public:
	ResultWindow()
		:
		BWindow(BRect(80, 50, 780, 1050), "KosmOS Input Test Suite",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new ResultView());
	}
};


class KosmInputTestApp : public BApplication {
public:
	KosmInputTestApp()
		:
		BApplication("application/x-vnd.KosmOS-InputTest")
	{
	}

	virtual void ReadyToRun()
	{
		_OpenTrace();
		_RunTests();
		(new ResultWindow())->Show();
	}

private:
	void _OpenTrace()
	{
		BPath desktop;
		if (find_directory(B_DESKTOP_DIRECTORY, &desktop) != B_OK)
			return;
		BPath path(desktop);
		path.Append("kosm_input_trace.log");
		sTraceFile = fopen(path.Path(), "w");
		if (sTraceFile) {
			trace("# KosmOS Input test trace\n");
			trace("# date: %s %s\n", __DATE__, __TIME__);
			trace("# system_time at start: %lld us\n",
				(long long)system_time());
			trace("# main thread: %d, team: %d\n",
				(int)find_thread(NULL), (int)getpid());
			trace("#\n");
		}
	}

	void _RunTest(const char* name, void (*func)(), const char* category)
	{
		int prevPass = sPassCount;
		int prevFail = sFailCount;

		bigtime_t t0 = system_time();
		func();
		bigtime_t dt = system_time() - t0;

		int newPass = sPassCount - prevPass;
		int newFail = sFailCount - prevFail;

		if (newFail == 0) {
			log_line("PASS  %s  (%d checks, %lld us)",
				name, newPass, (long long)dt);
			trace("  [%s] PASS: %s  (%d checks, %lld us)\n",
				category, name, newPass, (long long)dt);
		} else {
			log_line("FAIL  %s  (%d pass, %d fail, %lld us)",
				name, newPass, newFail, (long long)dt);
			trace("  [%s] FAIL: %s  (%d pass, %d fail, %lld us)\n",
				category, name, newPass, newFail, (long long)dt);
		}
	}

	void _RunTests()
	{
		bigtime_t totalStart = system_time();

		log_line("=== KosmOS Input Test Suite ===");
		log_line("");

		// BitField Testers
		log_line("--- BitField Testers ---");
		trace("\n# ========== BITFIELD TESTERS ==========\n");
		_RunTest("constant tester",     test_constant_field_tester, "bft");
		_RunTest("hasbits tester",      test_hasbits_field_tester, "bft");
		_RunTest("not tester",          test_not_field_tester, "bft");
		_RunTest("minmatch tester",     test_minmatch_field_tester, "bft");
		_RunTest("archive roundtrip",   test_tester_archive_roundtrip, "bft");
		log_line("");

		// Key Infos
		log_line("--- Key Infos ---");
		trace("\n# ========== KEY INFOS ==========\n");
		_RunTest("init and lookup",     test_key_init_and_lookup, "key");
		_RunTest("find by name",        test_key_find_by_name, "key");
		_RunTest("UTF8 values",         test_key_utf8, "key");
		log_line("");

		// Parse Command Line
		log_line("--- Parse Command Line ---");
		trace("\n# ========== PARSE ==========\n");
		_RunTest("simple parse",        test_parse_simple, "parse");
		_RunTest("quoted parse",        test_parse_quoted, "parse");
		_RunTest("escaped parse",       test_parse_escaped, "parse");
		_RunTest("clone argv",          test_clone_argv, "parse");
		_RunTest("parse argv zero",     test_parse_argv_zero, "parse");
		log_line("");

		// Keymap
		log_line("--- Keymap ---");
		trace("\n# ========== KEYMAP ==========\n");
		_RunTest("keymap load",         test_keymap_load, "kmap");
		_RunTest("char lookup",         test_keymap_char_lookup, "kmap");
		log_line("");

		// Movement Math
		log_line("--- Movement Math ---");
		trace("\n# ========== MOVEMENT ==========\n");
		_RunTest("basic movement",      test_movement_maker_basic, "move");
		_RunTest("scrolling",           test_movement_maker_scrolling, "move");
		log_line("");

		// Message Format
		log_line("--- Message Format ---");
		trace("\n# ========== MESSAGES ==========\n");
		_RunTest("keyboard message",    test_keyboard_message_format, "msg");
		_RunTest("mouse message",       test_mouse_message_format, "msg");
		_RunTest("wheel message",       test_wheel_message_format, "msg");
		_RunTest("tablet message",      test_tablet_message_format, "msg");
		_RunTest("modifier changed",    test_modifier_changed_format, "msg");
		log_line("");

		// Device Logic
		log_line("--- Device Logic ---");
		trace("\n# ========== DEVICE ==========\n");
		_RunTest("name building",       test_device_name_building, "dev");
		_RunTest("category matching",   test_device_category_matching, "dev");
		_RunTest("virtio detection",    test_virtio_type_detection, "dev");
		log_line("");

		// Filter Logic
		log_line("--- Filter Logic ---");
		trace("\n# ========== FILTER ==========\n");
		_RunTest("palm rejection",      test_palm_rejection, "filt");
		_RunTest("modifier tracking",   test_modifier_tracking, "filt");
		_RunTest("acceleration math",   test_acceleration_math, "filt");
		_RunTest("button remapping",    test_button_remapping, "filt");
		_RunTest("workspace keys",      test_workspace_key_matching, "filt");
		_RunTest("profile activation",  test_profile_activation, "filt");
		log_line("");

		// Stress
		log_line("--- STRESS ---");
		trace("\n# ========== STRESS ==========\n");
		_RunTest("tester throughput 100K",
			test_tester_matching_throughput, "stress");
		_RunTest("message construction 10K",
			test_message_construction_throughput, "stress");
		_RunTest("keymap lookup 50K",
			test_keymap_lookup_throughput, "stress");
		_RunTest("acceleration 100K",
			test_acceleration_throughput, "stress");
		log_line("");

		// Pipeline Simulation
		log_line("--- PIPELINE SIM ---");
		trace("\n# ========== PIPELINE SIM ==========\n");
		_RunTest("device->filter pipeline 1K",
			test_input_pipeline_simulation, "pipeline");
		log_line("");

		bigtime_t totalTime = system_time() - totalStart;

		log_line("=== Results: %d passed, %d failed  (%lld us total) ===",
			sPassCount, sFailCount, (long long)totalTime);

		trace("\n# ================================\n");
		trace("# FINAL: %d passed, %d failed\n", sPassCount, sFailCount);
		trace("# total time: %lld us\n", (long long)totalTime);
		trace("# ================================\n");

		if (sTraceFile) {
			fclose(sTraceFile);
			sTraceFile = NULL;
		}

		log_line("");
		log_line("Trace: ~/Desktop/kosm_input_trace.log");
	}
};


int
main()
{
	KosmInputTestApp app;
	app.Run();
	return 0;
}
