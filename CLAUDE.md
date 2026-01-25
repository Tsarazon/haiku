- always doublecheck your decisions

# Mobile Haiku (KosmOS)

Мобильная операционная система на базе Haiku OS для ARM64, ARM, RISC-V и x86_64 устройств .

## Архитектура

Модульная iOS-подобная архитектура с заменой традиционных app_server и Interface Kit на современный графический стек:

- **Surface Kit** — управление буферами через area_id, RAII
- **Render Kit** — 2D рендеринг через ThorVG (KosmCanvas, KosmPainter, KosmPath)
- **Animation Kit** — анимации и переходы
- **Spektr UI Kit** — UI компоненты (элементы LVGL + Ark UI) с **KosmLayout** — система разметки (flexbox/grid)
- **Orbita** — композитор
- **Stancia** — лаунчер

## Ключевые решения

- ThorVG для векторной графики
- Exclusive-координаты (KosmPoint, KosmSize, KosmRect) вместо inclusive из Haiku
- Децентрализованная архитектура демонов (iOS-стиль)
- Сборка через xmake
- Философия "C++ with classes" в стиле Haiku

## Целевое железо

- На первом этапе Intel N150 планшеты (X86_64)

## Соглашения

- Пространство имён с префиксом Kosm*
- Космическая тематика в названиях компонентов
- Без ASCII-рамок

## Сборка

```bash
# Перейти в generated
cd /home/ruslan/haiku/generated

# Сборка nightly-anyboot ISO (1 ГБ)
/home/ruslan/haiku/buildtools/jam/jam0 -q -j8 @nightly-anyboot

# Полная пересборка с нуля
/home/ruslan/haiku/buildtools/jam/jam0 -a -q -j8 @nightly-anyboot

# С логгированием
/home/ruslan/haiku/buildtools/jam/jam0 -a -q -j8 @nightly-anyboot 2>&1 | tee /home/ruslan/haiku/build_log.txt
```

**Важно:**
- Использовать jam из `buildtools/jam/jam0`, НЕ системный jam
- Запускать из папки `generated`
- `-a` — пересборка всех целей
- `-q` — быстрый выход при ошибке
- `-j8` — 8 потоков

**Размеры образов** (настраиваются в `build/jam/`):
- `haiku-anyboot.iso` — 2 ГБ (BuildSetup:66)
- `haiku-nightly-anyboot.iso` — 1 ГБ (DefaultBuildProfiles:129)

