/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Unified test suite — shared infrastructure, GUI, and main().
 */


#include "TestCommon.h"

#include <stdarg.h>
#include <time.h>


// Global state
FILE*	sTraceFile = NULL;
int		sPassCount = 0;
int		sFailCount = 0;
char	sLines[kMaxLines][256];
int		sLineCount = 0;


void
trace(const char* fmt, ...)
{
	if (sTraceFile == NULL)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(sTraceFile, fmt, ap);
	va_end(ap);
	fflush(sTraceFile);
}


void
trace_call(const char* func, status_t result)
{
	trace("    %s -> %s (0x%08lx)\n", func, strerror(result),
		(unsigned long)result);
}


void
trace_call_id(const char* func, int32 result)
{
	if (result >= 0)
		trace("    %s -> id=%ld\n", func, (long)result);
	else
		trace("    %s -> %s (0x%08lx)\n", func, strerror(result),
			(unsigned long)result);
}


void
log_line(const char* fmt, ...)
{
	if (sLineCount >= kMaxLines)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(sLines[sLineCount], sizeof(sLines[0]), fmt, ap);
	va_end(ap);
	sLineCount++;
}


void
reset_results()
{
	sPassCount = 0;
	sFailCount = 0;
	sLineCount = 0;
}


void
run_test(const TestEntry& entry)
{
	int p0 = sPassCount, f0 = sFailCount;
	bigtime_t t0 = system_time();
	entry.func();
	bigtime_t dt = system_time() - t0;
	int dp = sPassCount - p0;
	int df = sFailCount - f0;
	int total = dp + df;

	if (df == 0) {
		log_line("  PASS  %s  (%d checks, %lld us)",
			entry.name, total, (long long)dt);
		trace("  [%s] PASS: %s  (%d checks, %lld us)\n",
			entry.category, entry.name, total, (long long)dt);
	} else {
		log_line("  FAIL  %s  (%d/%d failed, %lld us)",
			entry.name, df, total, (long long)dt);
		trace("  [%s] FAIL: %s  (%d/%d failed, %lld us)\n",
			entry.category, entry.name, df, total, (long long)dt);
	}
}


void
open_trace(const char* filename)
{
	BPath path;
	if (find_directory(B_DESKTOP_DIRECTORY, &path) != B_OK)
		return;
	path.Append(filename);
	sTraceFile = fopen(path.Path(), "w");
	if (sTraceFile == NULL)
		return;

	time_t now = time(NULL);
	struct tm* t = localtime(&now);
	char dateBuf[64];
	strftime(dateBuf, sizeof(dateBuf), "%b %e %Y %H:%M:%S", t);

	trace("# KosmOS Unified Test Suite\n");
	trace("# date: %s\n", dateBuf);
	trace("# system_time at start: %lld us\n",
		(long long)system_time());
	trace("# main thread: %ld, team: %ld\n",
		(long)find_thread(NULL), (long)getpid());
	trace("#\n");
}


void
close_trace()
{
	if (sTraceFile != NULL) {
		fclose(sTraceFile);
		sTraceFile = NULL;
	}
}


// Run one full suite and log section headers
static void
_RunSuite(const TestSuite& suite)
{
	log_line("=== %s ===", suite.name);
	trace("\n# ========== %s ==========\n", suite.name);

	const char* prevCategory = NULL;
	for (int i = 0; i < suite.count; i++) {
		const TestEntry& e = suite.tests[i];
		if (prevCategory == NULL
			|| strcmp(prevCategory, e.category) != 0) {
			log_line("");
			log_line("--- %s ---", e.category);
			trace("\n# --- %s ---\n", e.category);
			prevCategory = e.category;
		}
		run_test(e);
	}
}


class ResultView : public BView {
public:
	ResultView()
		: BView("ResultView", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
			| B_FRAME_EVENTS)
	{
		SetViewColor(30, 30, 40);
		SetFont(be_fixed_font);
		SetFontSize(11.0f);
	}

	virtual void Draw(BRect updateRect)
	{
		float lineHeight = 14.0f;
		float y = 16.0f;

		for (int i = 0; i < sLineCount && i < kMaxLines; i++) {
			const char* line = sLines[i];

			if (strstr(line, "FAIL"))
				SetHighColor(255, 80, 80);
			else if (strstr(line, "PASS"))
				SetHighColor(80, 220, 80);
			else if (strstr(line, "==="))
				SetHighColor(255, 220, 100);
			else if (strstr(line, "---"))
				SetHighColor(130, 170, 255);
			else if (strstr(line, "STRESS") || strstr(line, "MOBILE")
				|| strstr(line, "Select"))
				SetHighColor(255, 180, 60);
			else
				SetHighColor(200, 200, 200);

			DrawString(line, BPoint(10, y));
			y += lineHeight;
		}
	}

	virtual void GetPreferredSize(float* width, float* height)
	{
		*width = 680.0f;
		*height = sLineCount * 14.0f + 20.0f;
	}

	void Refresh()
	{
		float w, h;
		GetPreferredSize(&w, &h);
		ResizeTo(w, h);
		Invalidate();
	}
};


class TestWindow : public BWindow {
public:
	TestWindow()
		: BWindow(BRect(60, 40, 810, 900), "KosmOS Test Suite",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		BView* root = new BView(Bounds(), "root", B_FOLLOW_ALL, 0);
		root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		AddChild(root);

		float buttonY = 8.0f;
		float buttonX = 10.0f;
		float buttonW = 100.0f;
		float buttonH = 28.0f;
		float gap = 8.0f;

		BButton* rayBtn = new BButton(
			BRect(buttonX, buttonY,
				buttonX + buttonW, buttonY + buttonH),
			"ray", "Ray",
			new BMessage(kMsgRunRay), B_FOLLOW_LEFT | B_FOLLOW_TOP);
		root->AddChild(rayBtn);
		buttonX += buttonW + gap;

		BButton* mutexBtn = new BButton(
			BRect(buttonX, buttonY,
				buttonX + buttonW, buttonY + buttonH),
			"mutex", "Mutex",
			new BMessage(kMsgRunMutex), B_FOLLOW_LEFT | B_FOLLOW_TOP);
		root->AddChild(mutexBtn);
		buttonX += buttonW + gap;

		BButton* surfaceBtn = new BButton(
			BRect(buttonX, buttonY,
				buttonX + buttonW + 10, buttonY + buttonH),
			"surface", "Surface",
			new BMessage(kMsgRunSurface),
			B_FOLLOW_LEFT | B_FOLLOW_TOP);
		root->AddChild(surfaceBtn);
		buttonX += buttonW + 10 + gap;

		BButton* allBtn = new BButton(
			BRect(buttonX, buttonY,
				buttonX + buttonW, buttonY + buttonH),
			"all", "All",
			new BMessage(kMsgRunAll), B_FOLLOW_LEFT | B_FOLLOW_TOP);
		root->AddChild(allBtn);

		float scrollTop = buttonY + buttonH + 10.0f;
		BRect scrollFrame(0, scrollTop,
			Bounds().Width() - B_V_SCROLL_BAR_WIDTH,
			Bounds().Height());

		fResultView = new ResultView();
		fResultView->ResizeTo(scrollFrame.Width(), scrollFrame.Height());

		fScrollView = new BScrollView("scroll", fResultView,
			B_FOLLOW_ALL, 0, false, true);
		fScrollView->MoveTo(0, scrollTop);
		fScrollView->ResizeTo(Bounds().Width(),
			Bounds().Height() - scrollTop);
		root->AddChild(fScrollView);
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgRunRay:
				_Run(get_ray_test_suite);
				break;
			case kMsgRunMutex:
				_Run(get_mutex_test_suite);
				break;
			case kMsgRunSurface:
				_Run(get_surface_test_suite);
				break;
			case kMsgRunAll:
				_RunAll();
				break;
			default:
				BWindow::MessageReceived(msg);
				break;
		}
	}

private:
	typedef TestSuite (*suite_getter_t)();

	void _Run(suite_getter_t getter)
	{
		reset_results();
		close_trace();
		open_trace("kosm_test_trace.log");

		bigtime_t t0 = system_time();
		TestSuite suite = getter();
		_RunSuite(suite);
		bigtime_t totalTime = system_time() - t0;

		_LogSummary(totalTime);
		close_trace();
		fResultView->Refresh();
	}

	void _RunAll()
	{
		reset_results();
		close_trace();
		open_trace("kosm_test_trace.log");

		bigtime_t t0 = system_time();

		TestSuite suites[] = {
			get_ray_test_suite(),
			get_mutex_test_suite(),
			get_surface_test_suite(),
		};

		for (int i = 0; i < 3; i++) {
			log_line("");
			_RunSuite(suites[i]);
		}

		bigtime_t totalTime = system_time() - t0;

		_LogSummary(totalTime);
		close_trace();
		fResultView->Refresh();
	}

	void _LogSummary(bigtime_t totalTime)
	{
		log_line("");
		log_line("=== Results: %d passed, %d failed (%lld us total) ===",
			sPassCount, sFailCount, (long long)totalTime);

		trace("\n# ================================\n");
		trace("# FINAL: %d passed, %d failed\n", sPassCount, sFailCount);
		trace("# total time: %lld us\n", (long long)totalTime);
		trace("# ================================\n");

		log_line("Trace: ~/Desktop/kosm_test_trace.log");
	}

	ResultView*		fResultView;
	BScrollView*	fScrollView;
};


class KosmTestSuiteApp : public BApplication {
public:
	KosmTestSuiteApp()
		: BApplication("application/x-vnd.KosmOS-TestSuite")
	{
	}

	virtual void ReadyToRun()
	{
		(new TestWindow())->Show();
	}
};


int
main()
{
	KosmTestSuiteApp app;
	app.Run();
	return 0;
}
