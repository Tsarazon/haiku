# AGG→Blend2D Migration: Comprehensive Test Plan

## Executive Summary

This test plan covers the migration from Anti-Grain Geometry (AGG) to Blend2D rendering backend in Haiku's app_server. The plan addresses all critical components, provides detailed test scenarios, and establishes metrics for successful migration.

## Critical Files Analysis

### Primary Migration Targets
- **Painter.cpp** (2,140 lines) - Core rendering engine
- **AGGTextRenderer.cpp** (463 lines) - Text rendering subsystem
- **FontEngine.cpp** (696 lines) - FreeType integration
- **Drawing Modes** (40 files) - Blend mode implementations
- **BitmapPainter.cpp** - Bitmap scaling algorithms

---

## 1. UNIT TESTS

### 1.1 Painter.cpp Core Functionality

#### Test Category: Basic Rendering Operations
```cpp
// Test: PainterBasicShapes
// File: tests/painter/PainterBasicShapesTest.cpp

TEST_SUITE("Painter Basic Shapes") {
    TEST_CASE("Line Drawing") {
        // Input: Various line coordinates, pen sizes, colors
        BPoint start(10, 10);
        BPoint end(100, 100);
        rgb_color color = {255, 0, 0, 255};
        float penSize = 2.0f;

        // Expected: Exact pixel-perfect line rendering
        // Verification: Pixel-by-pixel comparison with reference
        REQUIRE(RenderLine(start, end, color, penSize) == referenceImage);
    }

    TEST_CASE("Rectangle Filling") {
        BRect rect(10, 10, 100, 100);
        rgb_color fillColor = {0, 255, 0, 255};

        // Test solid fills, gradient fills, pattern fills
        REQUIRE(FillRect(rect, fillColor) == expectedPixels);
    }

    TEST_CASE("Circle/Ellipse Rendering") {
        BPoint center(50, 50);
        float radiusX = 30, radiusY = 20;

        // Test perfect circle anti-aliasing
        REQUIRE(DrawEllipse(center, radiusX, radiusY) == referenceEllipse);
    }
}
```

#### Test Category: Complex Path Rendering
```cpp
TEST_SUITE("Complex Paths") {
    TEST_CASE("Bezier Curves") {
        // Input: Control points for cubic/quadratic beziers
        BPoint points[] = {{0,0}, {50,100}, {100,50}, {150,0}};

        // Expected: Smooth curve interpolation
        REQUIRE(DrawBezier(points, 4) == referenceBezier);
    }

    TEST_CASE("Polygon Clipping") {
        // Input: Complex polygons with self-intersections
        BPoint vertices[] = {{10,10}, {100,10}, {50,50}, {100,100}, {10,100}};
        BRegion clipRegion(BRect(25, 25, 75, 75));

        // Expected: Correct clipping behavior
        REQUIRE(ClipPolygon(vertices, 5, clipRegion) == clippedResult);
    }
}
```

#### Test Category: Transformation Matrix
```cpp
TEST_SUITE("Transformations") {
    TEST_CASE("Matrix Operations") {
        BAffineTransform transform;
        transform.ScaleBy(2.0, 2.0);
        transform.RotateBy(M_PI/4);
        transform.TranslateBy(50, 50);

        // Test matrix composition and application
        REQUIRE(ApplyTransform(shape, transform) == transformedShape);
    }

    TEST_CASE("Coordinate System Changes") {
        // Test view coordinate to device coordinate conversion
        REQUIRE(ViewToDevice(viewPoint) == devicePoint);
    }
}
```

**Coverage Requirements:**
- Line coverage: 95%
- Branch coverage: 90%
- Function coverage: 100%

**Pass Criteria:**
- All primitive shapes render identically
- Anti-aliasing quality matches AGG reference
- No memory leaks in transformation calculations
- Performance within 5% of AGG baseline

---

### 1.2 AGGTextRenderer.cpp Text Engine

#### Test Category: Font Rendering
```cpp
TEST_SUITE("Text Rendering") {
    TEST_CASE("Basic Glyph Rendering") {
        ServerFont font;
        font.SetSize(12.0f);
        font.SetFamily("DejaVu Sans");

        const char* text = "Hello World";
        BPoint baseline(10, 30);

        // Expected: Crisp glyph rendering with proper spacing
        REQUIRE(RenderText(text, baseline, font) == referenceGlyphs);
    }

    TEST_CASE("Unicode Support") {
        const char* unicodeText = "Тест 中文 العربية ಕನ್ನಡ";

        // Test complex script rendering
        REQUIRE(RenderUnicodeText(unicodeText) == unicodeReference);
    }

    TEST_CASE("Subpixel Rendering") {
        // Test RGB subpixel anti-aliasing
        rgb_color textColor = {0, 0, 0, 255};
        bool subpixelEnabled = true;

        REQUIRE(SubpixelText(text, textColor, subpixelEnabled) == subpixelRef);
    }
}
```

#### Test Category: Font Metrics
```cpp
TEST_SUITE("Font Metrics") {
    TEST_CASE("Character Advance") {
        // Test character width calculations
        float advance = GetCharAdvance('A', font);
        REQUIRE(advance == expectedAdvance);
    }

    TEST_CASE("Kerning Pairs") {
        // Test proper kerning between character pairs
        float kerning = GetKerning('A', 'V', font);
        REQUIRE(kerning == expectedKerning);
    }

    TEST_CASE("Line Metrics") {
        font_height height;
        GetFontHeight(font, &height);

        REQUIRE(height.ascent == expectedAscent);
        REQUIRE(height.descent == expectedDescent);
        REQUIRE(height.leading == expectedLeading);
    }
}
```

**Coverage Requirements:**
- Line coverage: 98%
- All font rendering paths tested
- Memory management verification

**Pass Criteria:**
- Identical glyph rendering to AGG
- Correct font metrics calculation
- Subpixel rendering quality maintained
- No memory leaks in glyph caching

---

### 1.3 FontEngine.cpp FreeType Integration

#### Test Category: Font Loading
```cpp
TEST_SUITE("Font Loading") {
    TEST_CASE("TrueType Font Loading") {
        const char* fontPath = "/system/fonts/DejaVuSans.ttf";

        // Test successful font file parsing
        FontEngine engine;
        REQUIRE(engine.LoadFont(fontPath) == B_OK);
        REQUIRE(engine.GetGlyphCount() > 0);
    }

    TEST_CASE("OpenType Font Features") {
        // Test advanced OpenType features
        REQUIRE(engine.SupportsFeature("liga") == true);
        REQUIRE(engine.ApplyLigatures(text) == ligatedText);
    }

    TEST_CASE("Font Family Detection") {
        REQUIRE(engine.GetFamilyName() == "DejaVu Sans");
        REQUIRE(engine.GetStyleName() == "Regular");
    }
}
```

#### Test Category: Glyph Rasterization
```cpp
TEST_SUITE("Glyph Rasterization") {
    TEST_CASE("Hinting Quality") {
        // Test font hinting at various sizes
        for (float size = 8.0f; size <= 72.0f; size += 1.0f) {
            font.SetSize(size);
            auto glyph = engine.RenderGlyph('a', font);
            REQUIRE(glyph.quality >= MINIMUM_QUALITY_THRESHOLD);
        }
    }

    TEST_CASE("Bitmap vs Vector Rendering") {
        // Compare bitmap and vector glyph rendering
        auto bitmapGlyph = engine.RenderBitmapGlyph('A', font);
        auto vectorGlyph = engine.RenderVectorGlyph('A', font);

        REQUIRE(CompareGlyphQuality(bitmapGlyph, vectorGlyph) < TOLERANCE);
    }
}
```

**Coverage Requirements:**
- All FreeType code paths tested
- Font format compatibility verified
- Memory management audited

**Pass Criteria:**
- All system fonts load correctly
- Glyph quality maintained across sizes
- FreeType integration stable
- No memory corruption

---

## 2. INTEGRATION TESTS

### 2.1 Painter ↔ TextRenderer Integration

#### Test Category: Text Drawing Pipeline
```cpp
TEST_SUITE("Text Drawing Integration") {
    TEST_CASE("View Text Rendering") {
        BView view;
        view.SetFont(&font);
        view.SetHighColor(textColor);

        // Test complete text rendering pipeline
        view.DrawString("Integration Test", BPoint(10, 30));

        // Verify painter and text renderer coordination
        REQUIRE(view.GetPixel(15, 25) == expectedPixelColor);
    }

    TEST_CASE("Clipped Text Rendering") {
        BRegion clipRegion(BRect(0, 0, 50, 50));
        view.ConstrainClippingRegion(&clipRegion);
        view.DrawString("Clipped Text Test", BPoint(10, 30));

        // Verify proper text clipping
        REQUIRE(TextIsClippedCorrectly(clipRegion));
    }
}
```

### 2.2 Drawing Modes Integration

#### Test Category: Blend Mode Compatibility
```cpp
TEST_SUITE("Drawing Modes Integration") {
    TEST_CASE("All Blend Modes") {
        drawing_mode modes[] = {
            B_OP_COPY, B_OP_OVER, B_OP_ERASE, B_OP_INVERT,
            B_OP_ADD, B_OP_SUBTRACT, B_OP_BLEND, B_OP_MIN,
            B_OP_MAX, B_OP_SELECT, B_OP_ALPHA
        };

        for (auto mode : modes) {
            painter.SetDrawingMode(mode);
            painter.FillRect(testRect, testColor);

            // Compare with AGG reference output
            REQUIRE(CompareWithReference(mode) < BLEND_TOLERANCE);
        }
    }

    TEST_CASE("Alpha Composition") {
        // Test various alpha values and compositions
        for (uint8 alpha = 0; alpha <= 255; alpha += 5) {
            rgb_color color = {255, 0, 0, alpha};
            painter.SetHighColor(color);
            painter.FillRect(rect, color);

            REQUIRE(VerifyAlphaBlending(alpha));
        }
    }
}
```

### 2.3 Bitmap Integration

#### Test Category: Bitmap Drawing
```cpp
TEST_SUITE("Bitmap Integration") {
    TEST_CASE("Bitmap Scaling") {
        BBitmap source(BRect(0, 0, 99, 99), B_RGB32);
        BRect destRect(0, 0, 199, 199); // 2x scale

        painter.DrawBitmap(&source, destRect);

        // Verify scaling algorithm quality
        REQUIRE(MeasureScalingQuality(destRect) >= MIN_SCALING_QUALITY);
    }

    TEST_CASE("Color Space Conversion") {
        // Test different bitmap color spaces
        color_space spaces[] = {B_RGB32, B_RGBA32, B_RGB16, B_GRAY8};

        for (auto space : spaces) {
            BBitmap bitmap(BRect(0, 0, 50, 50), space);
            painter.DrawBitmap(&bitmap, BPoint(0, 0));

            REQUIRE(VerifyColorSpaceHandling(space));
        }
    }
}
```

**Pass Criteria:**
- All integration points work seamlessly
- No data corruption between components
- Performance within 10% of baseline
- Memory usage stable

---

## 3. VISUAL REGRESSION TESTS

### 3.1 Reference Image Generation

#### Test Infrastructure
```cpp
class VisualRegressionTest {
private:
    static const char* REFERENCE_PATH = "/tests/references/";
    static const char* OUTPUT_PATH = "/tests/output/";
    static const double PIXEL_TOLERANCE = 0.02; // 2% difference

public:
    bool CompareWithReference(const char* testName, BBitmap* output) {
        BString refPath(REFERENCE_PATH);
        refPath << testName << ".png";

        BBitmap reference;
        if (LoadPNG(refPath.String(), &reference) != B_OK) {
            // Generate new reference if doesn't exist
            return GenerateReference(testName, output);
        }

        return CompareImages(&reference, output, PIXEL_TOLERANCE);
    }
};
```

#### Test Category: UI Component Rendering
```cpp
TEST_SUITE("Visual Regression") {
    TEST_CASE("Button Rendering") {
        BButton button("Test Button");
        button.ResizeTo(100, 30);

        BBitmap* rendered = RenderControl(&button);
        REQUIRE(visualTest.CompareWithReference("button_normal", rendered));
    }

    TEST_CASE("Complex Window") {
        // Render complete window with multiple controls
        auto window = CreateTestWindow();
        BBitmap* rendered = RenderWindow(window);

        REQUIRE(visualTest.CompareWithReference("complex_window", rendered));
    }

    TEST_CASE("Text Rendering Samples") {
        // Test various fonts, sizes, and styles
        const char* samples[] = {
            "Normal text",
            "Bold text",
            "Italic text",
            "Mixed 中文 العربية text"
        };

        for (const char* sample : samples) {
            auto rendered = RenderTextSample(sample);
            REQUIRE(visualTest.CompareWithReference(sample, rendered));
        }
    }
}
```

### 3.2 Dynamic Content Testing

#### Test Category: Animation Rendering
```cpp
TEST_SUITE("Animation Visual Tests") {
    TEST_CASE("Smooth Gradients") {
        // Test gradient rendering quality
        BGradientLinear gradient;
        gradient.AddColor({255, 0, 0, 255}, 0.0f);
        gradient.AddColor({0, 0, 255, 255}, 1.0f);

        painter.FillRect(BRect(0, 0, 200, 100), gradient);

        REQUIRE(VerifyGradientSmooth());
    }

    TEST_CASE("Alpha Transparency") {
        // Test complex alpha compositions
        for (int layer = 0; layer < 10; layer++) {
            rgb_color color = {255, 0, 0, 25}; // Semi-transparent
            painter.FillRect(BRect(layer*10, layer*10, 50+layer*10, 50+layer*10), color);
        }

        REQUIRE(VerifyAlphaComposition());
    }
}
```

**Pass Criteria:**
- 98% pixel accuracy compared to AGG references
- No visual artifacts or glitches
- Anti-aliasing quality maintained
- Color accuracy preserved

---

## 4. PERFORMANCE BENCHMARKS

### 4.1 Rendering Performance

#### Benchmark Infrastructure
```cpp
class RenderingBenchmark {
private:
    bigtime_t startTime, endTime;

public:
    void StartTimer() { startTime = system_time(); }
    void EndTimer() { endTime = system_time(); }
    bigtime_t GetElapsedMicros() { return endTime - startTime; }

    double GetFPS(int frameCount) {
        return (frameCount * 1000000.0) / GetElapsedMicros();
    }
};
```

#### Test Category: Basic Operations
```cpp
TEST_SUITE("Performance Benchmarks") {
    TEST_CASE("Line Drawing Performance") {
        RenderingBenchmark bench;
        const int LINE_COUNT = 10000;

        bench.StartTimer();
        for (int i = 0; i < LINE_COUNT; i++) {
            painter.StrokeLine(BPoint(0, i%100), BPoint(100, i%100));
        }
        bench.EndTimer();

        bigtime_t agg_baseline = 50000; // microseconds
        bigtime_t blend2d_time = bench.GetElapsedMicros();

        // Blend2D should be within 105% of AGG performance
        REQUIRE(blend2d_time <= agg_baseline * 1.05);
    }

    TEST_CASE("Rectangle Fill Performance") {
        const int RECT_COUNT = 5000;

        bench.StartTimer();
        for (int i = 0; i < RECT_COUNT; i++) {
            painter.FillRect(BRect(i%50, i%50, 50+i%50, 50+i%50));
        }
        bench.EndTimer();

        REQUIRE(bench.GetElapsedMicros() <= RECT_FILL_BASELINE * 1.05);
    }
}
```

#### Test Category: Complex Scenes
```cpp
TEST_SUITE("Complex Scene Performance") {
    TEST_CASE("GUI Redraw Performance") {
        // Simulate typical GUI update
        auto scene = CreateComplexGUIScene(); // 100+ widgets

        bench.StartTimer();
        RenderScene(scene);
        bench.EndTimer();

        // Target: 60 FPS for complex scenes
        REQUIRE(bench.GetFPS(1) >= 60.0);
    }

    TEST_CASE("Large Bitmap Scaling") {
        BBitmap largeBitmap(BRect(0, 0, 2047, 2047), B_RGB32);
        BRect destRect(0, 0, 4095, 4095);

        bench.StartTimer();
        painter.DrawBitmap(&largeBitmap, destRect);
        bench.EndTimer();

        REQUIRE(bench.GetElapsedMicros() <= BITMAP_SCALE_BASELINE * 1.10);
    }
}
```

### 4.2 Memory Performance

#### Test Category: Memory Usage
```cpp
TEST_SUITE("Memory Performance") {
    TEST_CASE("Memory Leak Detection") {
        size_t initial_memory = GetMemoryUsage();

        // Perform 1000 rendering operations
        for (int i = 0; i < 1000; i++) {
            PerformRandomRenderingOperation();
        }

        // Force garbage collection
        ForceCleanup();

        size_t final_memory = GetMemoryUsage();

        // Memory usage should return to baseline ±5%
        REQUIRE(abs(final_memory - initial_memory) <= initial_memory * 0.05);
    }

    TEST_CASE("Large Object Handling") {
        // Test handling of large rendering objects
        auto largeObject = CreateLargeRenderObject(100000); // 100k vertices

        size_t before = GetMemoryUsage();
        RenderLargeObject(largeObject);
        DestroyLargeObject(largeObject);
        size_t after = GetMemoryUsage();

        REQUIRE(after <= before * 1.02); // Max 2% memory growth
    }
}
```

**Performance Targets:**
- Basic operations: Within 105% of AGG performance
- Complex scenes: Maintain 60 FPS
- Memory usage: No leaks, ±5% baseline
- Startup time: No regression

---

## 5. COMPATIBILITY TESTS

### 5.1 BeAPI Regression Tests

#### Test Category: BeAPI Compliance
```cpp
TEST_SUITE("BeAPI Compatibility") {
    TEST_CASE("BView Drawing Methods") {
        BView view;

        // Test all BView drawing methods work identically
        view.MovePenTo(10, 10);
        view.DrawChar('A');
        view.DrawString("Test");
        view.FillRect(BRect(0, 0, 50, 50));
        view.StrokeRect(BRect(10, 10, 60, 60));
        view.FillEllipse(BRect(20, 20, 70, 70));
        view.StrokeEllipse(BRect(30, 30, 80, 80));

        // Verify identical behavior to AGG implementation
        REQUIRE(VerifyAPICompatibility());
    }

    TEST_CASE("Drawing State Persistence") {
        BView view;

        // Test state preservation across operations
        view.SetHighColor({255, 0, 0, 255});
        view.SetLowColor({0, 255, 0, 255});
        view.SetPenSize(3.0f);
        view.SetDrawingMode(B_OP_BLEND);

        // Perform operations and verify state maintained
        view.DrawString("Test");
        REQUIRE(view.HighColor().red == 255);
        REQUIRE(view.PenSize() == 3.0f);
        REQUIRE(view.DrawingMode() == B_OP_BLEND);
    }
}
```

#### Test Category: Color Space Handling
```cpp
TEST_SUITE("Color Space Compatibility") {
    TEST_CASE("All Color Spaces") {
        color_space spaces[] = {
            B_RGB32, B_RGBA32, B_RGB24, B_RGB16, B_RGB15,
            B_RGBA15, B_CMAP8, B_GRAY8, B_GRAY1,
            B_RGB32_BIG, B_RGBA32_BIG
        };

        for (auto space : spaces) {
            BBitmap bitmap(BRect(0, 0, 100, 100), space);

            // Test bitmap creation and rendering
            REQUIRE(bitmap.IsValid());
            REQUIRE(RenderBitmap(&bitmap) == B_OK);
            REQUIRE(VerifyColorSpaceAccuracy(space));
        }
    }
}
```

### 5.2 Application Compatibility

#### Test Category: Real Application Testing
```cpp
TEST_SUITE("Application Compatibility") {
    TEST_CASE("StyledEdit Rendering") {
        // Launch StyledEdit and verify text rendering
        auto app = LaunchApplication("application/x-vnd.Haiku-StyledEdit");

        // Open document and verify rendering
        app->OpenDocument("/system/data/samples/sample.txt");
        auto screenshot = CaptureApplicationWindow(app);

        REQUIRE(CompareWithReference("stylededit_text", screenshot));
    }

    TEST_CASE("WebPositive Page Rendering") {
        // Test complex web page rendering
        auto webpositive = LaunchApplication("application/x-vnd.Haiku-WebPositive");
        webpositive->Navigate("file:///system/data/test_page.html");

        auto rendered = CaptureApplicationWindow(webpositive);
        REQUIRE(CompareWithReference("webpositive_test", rendered));
    }
}
```

**Pass Criteria:**
- 100% BeAPI behavioral compatibility
- All system applications render correctly
- No functional regressions
- Color accuracy maintained

---

## 6. STRESS TESTS

### 6.1 Resource Exhaustion Tests

#### Test Category: Memory Stress
```cpp
TEST_SUITE("Stress Tests") {
    TEST_CASE("Memory Exhaustion") {
        std::vector<BBitmap*> bitmaps;

        // Allocate bitmaps until near memory limit
        while (GetAvailableMemory() > MEMORY_THRESHOLD) {
            auto bitmap = new BBitmap(BRect(0, 0, 1023, 1023), B_RGB32);
            if (bitmap->IsValid()) {
                bitmaps.push_back(bitmap);
                painter.DrawBitmap(bitmap, BPoint(0, 0));
            } else {
                delete bitmap;
                break;
            }
        }

        // Verify system remains stable
        REQUIRE(SystemStable());

        // Cleanup
        for (auto bitmap : bitmaps) {
            delete bitmap;
        }
    }

    TEST_CASE("Rapid Allocation/Deallocation") {
        // Test rapid object lifecycle
        for (int i = 0; i < 100000; i++) {
            auto painter = new Painter();
            painter->AttachToBuffer(&buffer);
            painter->FillRect(BRect(0, 0, 10, 10));
            delete painter;
        }

        REQUIRE(NoMemoryLeaks());
    }
}
```

#### Test Category: Concurrent Access
```cpp
TEST_SUITE("Concurrency Stress") {
    TEST_CASE("Multi-threaded Rendering") {
        const int THREAD_COUNT = 8;
        std::vector<std::thread> threads;

        for (int i = 0; i < THREAD_COUNT; i++) {
            threads.emplace_back([i]() {
                Painter painter;
                painter.AttachToBuffer(&threadBuffers[i]);

                // Perform intensive rendering
                for (int j = 0; j < 10000; j++) {
                    painter.FillRect(BRect(j%100, j%100, (j%100)+50, (j%100)+50));
                }
            });
        }

        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }

        REQUIRE(NoDataCorruption());
        REQUIRE(AllBuffersValid());
    }
}
```

### 6.2 Edge Case Testing

#### Test Category: Boundary Conditions
```cpp
TEST_SUITE("Edge Case Tests") {
    TEST_CASE("Extreme Coordinates") {
        // Test rendering at coordinate limits
        BPoint extremePoints[] = {
            {-32768, -32768},
            {32767, 32767},
            {0, -32768},
            {32767, 0}
        };

        for (auto point : extremePoints) {
            REQUIRE_NOTHROW(painter.StrokeLine(BPoint(0, 0), point));
        }
    }

    TEST_CASE("Zero-Sized Objects") {
        // Test handling of degenerate objects
        REQUIRE_NOTHROW(painter.FillRect(BRect(10, 10, 10, 10))); // Zero area
        REQUIRE_NOTHROW(painter.StrokeEllipse(BRect(5, 5, 5, 5))); // Zero size
    }

    TEST_CASE("Extreme Transformations") {
        BAffineTransform extreme;
        extreme.ScaleBy(10000.0, 0.0001); // Extreme aspect ratio
        extreme.RotateBy(M_PI * 1000);    // Many rotations

        painter.SetTransform(extreme, 0, 0);
        REQUIRE_NOTHROW(painter.FillRect(BRect(0, 0, 10, 10)));
    }
}
```

**Pass Criteria:**
- System remains stable under stress
- No crashes or data corruption
- Graceful handling of resource exhaustion
- Thread safety maintained

---

## 7. TEST EXECUTION MATRIX

### 7.1 Automated Test Schedule

| Test Category | Frequency | Duration | Automation |
|---------------|-----------|----------|------------|
| Unit Tests | Every commit | 15 min | Full |
| Integration Tests | Daily | 45 min | Full |
| Visual Regression | Weekly | 2 hours | Semi-auto |
| Performance Benchmarks | Weekly | 1 hour | Full |
| Compatibility Tests | Before release | 4 hours | Semi-auto |
| Stress Tests | Monthly | 8 hours | Full |

### 7.2 Test Environment Matrix

| Environment | OS Version | Architecture | GPU | Purpose |
|-------------|------------|--------------|-----|---------|
| Test-1 | Haiku R1/B4 | x86_64 | Intel | Primary testing |
| Test-2 | Haiku R1/B4 | x86_64 | AMD | GPU compatibility |
| Test-3 | Haiku R1/B4 | x86_64 | NVIDIA | GPU compatibility |
| Test-4 | Haiku R1/B4 | x86 | Intel | 32-bit testing |
| Test-VM | Haiku nightly | x86_64 | Software | Regression testing |

### 7.3 Test Data Requirements

#### Sample Content
- **Fonts**: Complete Unicode coverage (100+ fonts)
- **Images**: Various formats (PNG, JPEG, BMP, GIF)
- **Color Spaces**: All supported formats
- **Reference Images**: AGG-generated baselines (1000+ images)

#### Performance Baselines
```cpp
// Baseline performance targets (AGG measurements)
const struct {
    const char* operation;
    bigtime_t baseline_micros;
    double tolerance;
} PERFORMANCE_BASELINES[] = {
    {"line_drawing_1000", 5000, 1.05},
    {"rect_fill_1000", 3000, 1.05},
    {"text_render_paragraph", 2000, 1.10},
    {"bitmap_scale_2x", 8000, 1.10},
    {"gradient_fill_complex", 12000, 1.15}
};
```

---

## 8. MIGRATION VALIDATION CRITERIA

### 8.1 Functional Requirements

**MUST PASS:**
- [ ] 100% BeAPI compatibility maintained
- [ ] All drawing operations produce identical output
- [ ] Text rendering quality preserved
- [ ] Font metrics accuracy maintained
- [ ] Color accuracy within 1% tolerance
- [ ] Anti-aliasing quality equivalent
- [ ] All 40 drawing modes work correctly
- [ ] Memory usage stable (±5% baseline)

### 8.2 Performance Requirements

**MUST MEET:**
- [ ] Basic operations within 105% of AGG performance
- [ ] Complex scenes maintain 60 FPS
- [ ] Application launch time no regression
- [ ] Memory footprint within 110% of baseline
- [ ] No memory leaks detected
- [ ] Thread safety maintained

### 8.3 Quality Requirements

**MUST ACHIEVE:**
- [ ] 98% visual regression test pass rate
- [ ] Zero crashes in stress testing
- [ ] All edge cases handled gracefully
- [ ] System stability under load
- [ ] Graceful degradation on resource exhaustion

---

## 9. RISK MITIGATION

### 9.1 High-Risk Areas

1. **Complex Path Rendering**: Bezier curves, self-intersecting polygons
2. **Subpixel Text Rendering**: Quality and performance critical
3. **Gradient Fills**: Complex gradient algorithms
4. **Bitmap Scaling**: Quality vs performance trade-offs
5. **Drawing Mode Compatibility**: Exact blend mode matching

### 9.2 Mitigation Strategies

- **Parallel Implementation**: Keep AGG fallback during transition
- **Incremental Migration**: Migrate components individually
- **Extensive Testing**: Focus on identified high-risk areas
- **Performance Monitoring**: Continuous benchmarking
- **User Feedback**: Beta testing with real applications

---

## 10. DELIVERABLES AND TIMELINE

### 10.1 Test Deliverables

1. **Test Suite** (Week 1-2)
   - Complete unit test coverage
   - Integration test framework
   - Performance benchmark suite

2. **Reference Data** (Week 2-3)
   - AGG baseline captures
   - Performance baselines
   - Visual regression references

3. **Automation Infrastructure** (Week 3-4)
   - CI/CD integration
   - Automated test execution
   - Result reporting system

4. **Migration Validation** (Week 4-6)
   - Full test execution
   - Results analysis
   - Migration sign-off

### 10.2 Success Metrics

- **Test Coverage**: >95% line coverage
- **Pass Rate**: >98% test pass rate
- **Performance**: Within 105% of baseline
- **Quality**: Zero visual regressions
- **Stability**: Zero crashes in stress tests

This comprehensive test plan ensures the AGG→Blend2D migration maintains full compatibility while potentially improving performance and maintainability.