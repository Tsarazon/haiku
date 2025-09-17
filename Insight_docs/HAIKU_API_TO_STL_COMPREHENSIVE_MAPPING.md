# Comprehensive Haiku API to C++ STL Mapping Reference

This document provides a complete mapping between Haiku/BeOS API classes and their C++ Standard Template Library (STL) equivalents, organized by functional categories.

## Container Classes

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BList` | `std::vector<void*>` | Type-unsafe container | **Keep BeOS** - core API |
| `BObjectList<T>` | `std::vector<T*>` | Type-safe object container | **Keep BeOS** - extends BList |
| `BHashMap<K,V>` | `std::unordered_map<K,V>` | Hash table implementation | STL for internal use |
| `BHashSet<T>` | `std::unordered_set<T>` | Hash set implementation | STL for internal use |
| `BNodeInfo::attr_info` | `std::map<std::string, attr_info>` | Attribute storage | **Keep BeOS** - FS specific |

## String Classes

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BString` | `std::string` | UTF-8 string class | **Keep BeOS** - core API |
| `BStringView` | `std::string_view` | Non-owning string view | STL for internal use |
| `BStringList` | `std::vector<std::string>` | String container | **Keep BeOS** - extends BString |

## Smart Pointers and Memory Management

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BAutoDeleter<T>` | `std::unique_ptr<T>` | RAII wrapper for single objects | STL preferred internally |
| `ObjectDeleter<T>` | `std::unique_ptr<T, std::default_delete<T>>` | Object-specific deleter | STL preferred internally |
| `ArrayDeleter<T>` | `std::unique_ptr<T[], std::default_delete<T[]>>` | Array deleter | STL preferred internally |
| `CObjectDeleter<T>` | `std::unique_ptr<T, void(*)(T*)>` | C-style deleter | STL preferred internally |
| `BReference<T>` | `std::shared_ptr<T>` | Reference counting | STL for new code |

## Geometric and Graphics Primitives

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BPoint` | `std::pair<float, float>` or custom | 2D point | **Keep BeOS** - core graphics |
| `BRect` | `std::tuple<float,float,float,float>` or custom | Rectangle | **Keep BeOS** - core graphics |
| `BRegion` | `std::vector<BRect>` + algorithms | Region operations | **Keep BeOS** - complex logic |
| `BSize` | `std::pair<float, float>` | Dimensions | **Keep BeOS** - graphics API |
| `rgb_color` | `std::tuple<uint8,uint8,uint8,uint8>` | Color representation | **Keep BeOS** - core graphics |

## Application Framework

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BMessage` | No direct equivalent | Message passing system | **Keep BeOS** - core architecture |
| `BMessageRunner` | `std::chrono` + `std::function` | Timed message delivery | **Keep BeOS** - system integration |
| `BHandler` | Observer pattern with `std::function` | Message handling | **Keep BeOS** - core architecture |
| `BLooper` | `std::thread` + message queue | Thread with message loop | **Keep BeOS** - core architecture |
| `BApplication` | No equivalent | Application framework | **Keep BeOS** - system contract |

## File System and I/O

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BFile` | `std::fstream` | File operations | **Keep BeOS** - FS attributes |
| `BDirectory` | `std::filesystem::directory_iterator` | Directory traversal | **Keep BeOS** - attributes |
| `BPath` | `std::filesystem::path` | Path manipulation | **Keep BeOS** - BeOS semantics |
| `BNode` | `std::filesystem::file_status` | Node metadata | **Keep BeOS** - attributes |
| `BEntry` | `std::filesystem::directory_entry` | Directory entry | **Keep BeOS** - attributes |
| `BQuery` | No direct equivalent | Live query system | **Keep BeOS** - unique feature |
| `BVolume` | No equivalent | Volume information | **Keep BeOS** - system specific |

## GUI Framework

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BWindow` | No equivalent | Window management | **Keep BeOS** - core GUI |
| `BView` | No equivalent | View hierarchy | **Keep BeOS** - core GUI |
| `BScrollView` | No equivalent | Scrollable container | **Keep BeOS** - GUI component |
| `BMenu` | `std::vector<MenuItem>` + callbacks | Menu system | **Keep BeOS** - GUI component |
| `BMenuItem` | `std::pair<std::string, std::function>` | Menu item | **Keep BeOS** - GUI component |
| `BButton` | No equivalent | Button widget | **Keep BeOS** - GUI component |
| `BTextView` | No equivalent | Text editing | **Keep BeOS** - complex widget |

## Storage and Archival

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BFlattenable` | Custom serialization interface | Serialization protocol | **Keep BeOS** - BMessage integration |
| `BArchivable` | Serialization with `std::ostream` | Archive/unarchive | **Keep BeOS** - system standard |
| `BResources` | `std::map<int32, std::vector<uint8>>` | Resource management | **Keep BeOS** - FS integration |

## Threading and Synchronization

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BLocker` | `std::mutex` | Mutual exclusion | STL preferred for new code |
| `BAutolock` | `std::lock_guard` | RAII locking | STL preferred |
| `sem_id` | `std::counting_semaphore` | Semaphore (system call) | **Keep BeOS** - system integration |
| `thread_id` | `std::thread::id` | Thread identification | **Keep BeOS** - system types |

## Utility Classes

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BStopWatch` | `std::chrono::high_resolution_clock` | Time measurement | STL preferred |
| `BDataIO` | `std::iostream` | I/O abstraction | **Keep BeOS** - system integration |
| `BPositionIO` | `std::iostream` with seeking | Seekable I/O | **Keep BeOS** - extends BDataIO |
| `BUrl` | No direct equivalent | URL parsing | **Keep BeOS** - or use external library |

## Media Framework

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BMediaFile` | No direct equivalent | Media file handling | **Keep BeOS** - media framework |
| `BMediaTrack` | No direct equivalent | Media track access | **Keep BeOS** - media framework |
| `BSoundPlayer` | No direct equivalent | Audio playback | **Keep BeOS** - system integration |
| `BBuffer` | `std::vector<uint8>` for data | Media buffer | **Keep BeOS** - media framework |

## Network Classes

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| `BSocket` | No direct STL equivalent | Socket abstraction | **Keep BeOS** or modernize |
| `BHttpRequest` | External library (libcurl++) | HTTP client | Consider modern alternative |
| `BNetAddress` | `std::variant` + address structs | Network addressing | **Keep BeOS** - system types |

## Algorithm and Iterator Support

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| Manual loops over BList | `std::for_each`, `std::transform` | Iterator algorithms | STL for internal algorithms |
| BList sorting | `std::sort`, `std::stable_sort` | Sorting algorithms | STL for internal use |
| Custom find functions | `std::find`, `std::find_if` | Search algorithms | STL for internal use |

## Mathematical and Geometric Operations

| Haiku/BeOS API | STL Equivalent | Notes | Recommendation |
|---------------|----------------|--------|----------------|
| Manual BRect operations | `std::algorithm` functions | Rectangle math | STL for complex operations |
| Color space conversions | `std::transform` + custom functions | Color operations | STL + custom logic |

## Recommendations by Category

### 💚 Always Keep BeOS API (Public Interface)
- **Application Framework**: BApplication, BWindow, BView, BMessage
- **Core Types**: BString, BRect, BPoint, rgb_color
- **File System**: BFile, BDirectory, BPath (for attributes)
- **GUI Components**: All widgets and GUI classes

### 💛 Hybrid Approach (BeOS External, STL Internal)
- **Graphics Engines**: Keep BeOS drawing API, use STL for internal algorithms
- **Data Processing**: Keep BeOS containers for public API, STL for internal operations
- **Threading**: Keep BLooper/BHandler pattern, use STL sync primitives internally

### 💙 Prefer STL (Internal Implementation)
- **Memory Management**: std::unique_ptr, std::shared_ptr
- **Algorithms**: std::sort, std::find, std::transform
- **Internal Containers**: std::vector, std::unordered_map for implementation details
- **Time Operations**: std::chrono for new timing code

### ❌ Never Replace
- **BMessage**: Core to BeOS application architecture
- **BHandler/BLooper**: Fundamental message passing system
- **File Attributes**: BNode, BFile - unique BeOS file system features
- **BQuery**: Live query system has no STL equivalent

## Migration Strategy

### Phase 1: Internal STL Adoption
- Replace internal algorithms with STL equivalents
- Use STL smart pointers in new code
- Apply STL containers for internal data structures

### Phase 2: Hybrid Interfaces
- Create adapter classes that present BeOS API but use STL internally
- Gradually migrate complex internal logic to STL-based implementations

### Phase 3: Optional Modern APIs
- Provide optional modern C++ APIs alongside BeOS APIs
- Allow developers to choose between BeOS and modern approaches
- Maintain full backward compatibility

## Conclusion

The optimal approach is **selective modernization**:
- **Keep BeOS API** for public interfaces and unique features
- **Use STL internally** for algorithms and memory management  
- **Create hybrid classes** that bridge both worlds
- **Never break** BeOS application compatibility

This strategy preserves Haiku's identity while enabling modern development practices and library integration.