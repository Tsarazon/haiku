# Композитинг в app_server (софт) + интеграция с Accelerant (Haiku OS)

Документ объединяет предложенную ранее **архитектуру базового (CPU) композитора**, встроенного в `app_server`, и **точки интеграции с accelerant API**. Цель: иметь понятную, практичную и пошаговую архитектуру, которую можно сразу реализовывать.

---

## Ключевая идея

1. Добавить в `ServerWindow` слой `Surface` (shared mmap буфер, метаданные, damage).
2. Организовать `Compositor` в `app_server` как отдельный компонент/поток, который собирает видимые поверхности по z-order и выполняет CPU-composite (alpha-over) только по повреждённым регионам.
3. Использовать `DisplayBackend` — абстракцию над accelerant, которая предоставляет frame buffer, vsync, pageflip, blit и hw-cursor. Compositor рендерит в offscreen/scanout buffer и презентует через DisplayBackend.

---

## Компоненты (конкретно)

### Surface (в ServerWindow)
- Shared memory (mmap) для back buffer (формат RGBA8888, рекомендовано premult alpha).
- Поля: id, z, bounds, visible, alpha_mode, pending_damage (Region), age/seq, stride, width/height.
- Double buffering: `client_buf` (куда пишет приложение) + `compositor_buf` (использует compositor) — swap атомарно.

### Compositor
- Структуры: ordered list<Surface*> (z-order), Framebuffer target (через DisplayBackend), render_thread.
- Функции: collect_damage(), rasterize_dirty_rects(), composite_tile(), present().
- Менеджер повреждений: объединяет обновления от клиентов, разбивает на rect'ы / tiles.

### DisplayBackend (абстракция over Accelerant)
- Методы: `init()`, `set_mode()`, `get_framebuffer_info()`, `present(buffer_handle, rects)`, `page_flip(handle)`, `blit(handle, rects)`, `wait_vsync()`, `set_hw_cursor()`, `capabilities()`.
- Реализации: `AccelerantAdapter` (взаимодействие с реальным accelerant API), `FallbackBackend` (mmap-only + timer vsync).

### IPC / Синхронизация
- Клиенты: `POST_UPDATE(surface_id, damage_rects)` и опционально `SWAP_BUFFERS(surface_id)`.
- app_server: помечает pending_damage, при swap инкрементит age и делает `memcpy`/swap pointer.
- Frame counter и wait API: `GET_FRAME_COUNTER()` / `WAIT_FOR_FRAME(n)`.

---

## Рендер-пайплайн (render_thread)

1. Ждёт сигнала dirty (condition variable) или vsync.
2. Собирает `screen_damage = union(pending_damage всех видимых поверхности)`.
3. Если пусто — sleep/continue.
4. Разбивает `screen_damage` на rect'ы / tiles (например 128×128) и оптимизирует: merge соседних, приоритет large rects.
5. Для каждого rect (или tile):
   - Создаёт temporary tile buffer (или использует preallocated) — целевой буфер для compositing.
   - Проходит surfaces снизу вверх: если surface.bounds пересекают rect — выполнить software composite (pixel loop с premult alpha-over) в tile buffer.
   - После прохода записать tile в `scanout_candidate_buffer` (offscreen) либо напрямую в framebuffer при mmap-only стратегии.
6. Выполнить `DisplayBackend.present(scanout_candidate_buffer, rects)`:
   - Если поддерживается pageflip: вызвать page_flip и ждать completion.
   - Else if blit supported: вызвать blit для rect'ов.
   - Else memcpy повреждённых rect'ов в mmap'ed fb (желательно на vsync).
7. Инкрементировать `frame_counter`, очистить pending_damage для обработанных областей.

---

## Blending (software)

- Формат: `RGBA8888` с **premultiplied alpha** — предпочтительно. Если клиенты присылают straight alpha, преобразовывать при swap.
- Пиксельная формула (premult):
  - `out.rgb = src.rgb + dst.rgb * (1 - src.a)`
  - `out.a = src.a + dst.a * (1 - src.a)`
- Оптимизации: SIMD (SSE/AVX2/NEON), integer fixed-point ops, batch 4 пикселей.

---

## Damage tracking и оптимизация

- Клиенты обязаны присылать минимальные damage rect'ы.
- Compositor хранит `prev_bounds` и при движении окна union(old, new) как implicit damage.
- Tile-based rendering (128×128) уменьшает overhead при множестве маленьких поверхностей.
- Merge heuristics: ограничить количество rect'ов в одном кадре; если слишком много мелких, fall back to larger merged rect или full-screen update.

---

## VSync / timing / presentation

- Предпочтительно использовать `DisplayBackend.wait_vsync()` (реализовано через accelerant). Это даёт точный момент для memcpy/pageflip.
- Если accelerant поддерживает pageflip — использовать его: рендер в offscreen scanout buffer, затем page_flip(handle) и ждать flip-complete.
- Если только mmap — иметь двойную буферизацию в app_server и memcpy в fb на vsync.
- Иметь `max_fps` limiter и coalesce multiple updates per frame.

---

## Integrating with Accelerant — ключевые моменты

1. **Capability discovery** при инициализации: проверять support pageflip, hw-cursor, blit, vsync, multi-buffer.
2. **Выбор present path**:
   - Prefer: pageflip (scanout buffers) -> zero-copy present.
   - Else: accelerated blit (if available) for tiles/rects.
   - Else: memcpy to mmap'ed framebuffer.
3. **HW cursor**: использовать hw cursor через accelerant, fallback на софтверный если формат/размер/горячий цвет не поддерживаются.
4. **Synchronization**: accelerant flip-complete / vsync events использовать как подтверждение presentation (frame_counter++).
5. **Hotplug / mode change**: DisplayBackend должен обрабатывать смену режима, пересоздавать scanout buffers, перенастраивать fb pointers.
6. **Подготовка к HW-offload**: хранить per-surface metadata (premult, alpha_mode, strides) и буферы в формате, совместимом с accelerant (handles/FDs) для будущего использования hw blit/compose.

---

## Threading model

- `Main thread` (app_server): IPC, window management, surface registration.
- `Render thread` (Compositor): сбор damage, рендер и present.
- `Optional worker pool`: параллельная rasterization tiles для больших экранов/много ядер (add later).

---

## Minimal IPC набор

- `REGISTER_SURFACE(params)`, `UNREGISTER_SURFACE`.
- `POST_UPDATE(surface_id, damage_rects, seq_no)`.
- `SWAP_BUFFERS(surface_id)` — атомарный swap pointer/age++.
- `WAIT_FOR_FRAME(frame_no)` / `GET_FRAME_COUNTER()`.

---

## Порядок интеграции (пошагово)

1. Добавить структуру `Surface` и mmap буфер в `ServerWindow`.
2. Реализовать IPC `POST_UPDATE` и простое pending_damage accounting.
3. Создать `Compositor` с render_thread; начально перерисовывать весь экран при любом update (простая версия).
4. Добавить damage tracking: рендерить только повреждённые rect'ы (tile-based).
5. Создать `DisplayBackend` abstraction и `FallbackBackend` (mmap-only + timer vsync).
6. Подключить accelerant adapter: capability discovery, pageflip/blit/vsync.
7. Поддержать pageflip и hw cursor, тестировать hotplug и mode changes.

---

## Примерные C++ структуры (псевдо)

```cpp
struct Rect { int l,t,r,b; };
struct Region { vector<Rect> rects; };

enum AlphaMode { OPAQUE, PREMULT, STRAIGHT };

struct Surface {
  int id;
  int z;
  int width, height, stride;
  void* client_buf;      // mmap from client
  void* compositor_buf;  // pointer used by compositor (after swap)
  Region pending_damage;
  bool visible;
  AlphaMode alpha;
  uint32_t age; // increment on swap
  std::mutex lock;
};

struct DisplayBackend {
  struct Capabilities { bool pageflip, blit, vsync, hw_cursor; } caps;
  virtual bool init() = 0;
  virtual void get_framebuffer_info(void*& ptr, int& stride, int& format) = 0;
  virtual bool present(BufferHandle h, const vector<Rect>& rects) = 0;
  virtual void wait_vsync() = 0;
};

struct Compositor {
  vector<Surface*> surfaces; // sorted by z
  DisplayBackend* backend;
  std::thread render_thread;
  std::atomic<bool> running;
  void render_loop();
};
```

---

## Тесты и критерии первой версии

- Перемещение окна, resize, перекрытие — корректная отрисовка.
- Прозрачные окна (alpha) — корректный blending.
- Отсутствие tearing при наличии vsync/pageflip.
- Корректный fallback, если accelerant не поддерживает pageflip.

---

## Возможные расширения (после MVP)

- SIMD-оптимизированный blend, NEON/SSE/AVX.
- Многопоточный rasterization tiles.
- Аппаратный offload: передача tiles/surfaces в accelerant для hw-compose.
- Компрессия буферов и zero-copy через dma-buf/FD (если accelerant/OS позволяет).

---

Если нужно, могу: добавить здесь конкретный пример `DisplayBackend` заголовка и пример использования в render loop (C++),
или выписать точные функции accelerant API из исходников Haiku (я могу их найти и проставить имена).

