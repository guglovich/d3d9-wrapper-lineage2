[English version](README.md)

# d3d9 wrapper для Lineage 2

Прокси `d3d9.dll` для клиентов Lineage 2 на Unreal Engine 2 / Direct3D 9.  
Кидается в папку `system` — без установки, без зависимостей.  
Настройки в `d3d9_proxy.ini`, который создаётся автоматически при первом запуске.

Тестировалось на **The 1st Throne: The Kamael**. Совместимо с другими хрониками на том же движке.

---

## Возможности

**Анизотропная фильтрация x16 + LOD bias**  
Принудительно применяет AF на все семплеры и повышает резкость мипмапов. Текстуры вблизи заметно чище — без какого-либо постпроцессинга.

**Ограничитель FPS**  
Post-Present sleep с точностью ~0.1ms. CPU в холостую не крутится.

**Fog override**  
Режим 1: полный override — цвет и дистанции из INI.  
Режим 2: игровой цвет тумана, дистанции из INI.

**Frame latency**  
Вызов `IDirect3DDevice9Ex::SetMaximumFrameLatency(1)` — уменьшает очередь кадров GPU. Снижает input lag.  
На Wine + DXVK работает всегда. На нативном Windows зависит от драйвера — некоторые AMD-драйверы принимают вызов, но игнорируют.

**Хоткей**  
RightAlt включает/выключает все улучшения на лету. Заголовок окна показывает текущий статус.

---

## Совместимость

| Функция | Windows | Linux — Gallium Nine | Linux — DXVK |
|---|:---:|:---:|:---:|
| AF x16 + LOD bias | ✅ | ✅ | ✅ |
| FPS limiter | ✅ | ✅ | ✅ |
| Fog override | ✅ | ✅ | ✅ |
| Frame latency | ⚠️ драйвер | ✅ | ✅ |

---

## Установка

1. Скопировать `d3d9.dll` в папку `system` рядом с `l2.exe`
2. Запустить игру — `d3d9_proxy.ini` создастся автоматически
3. Отредактировать INI под себя, перезапустить

Совместим с ReShade при загрузке через `opengl32.dll` (стандартная схема для UE2 — конфликта имён нет).

---

## Скриншоты

*// сравнение визуала до/после*

---

## Сборка

### Linux (MinGW → Windows DLL)

```bash
i686-w64-mingw32-g++ -shared -o d3d9.dll d3d9_l2.cpp \
  -I/usr/i686-w64-mingw32/include \
  -ldxguid \
  -Wl,--enable-stdcall-fixup \
  -Wl,--add-stdcall-alias \
  -Wl,--kill-at \
  -static-libgcc \
  -static-libstdc++ \
  -std=c++17
```

### Windows (MinGW)

```bash
i686-w64-mingw32-g++ -shared -o d3d9.dll d3d9_l2.cpp \
  -ldxguid \
  -Wl,--kill-at \
  -static-libgcc \
  -static-libstdc++ \
  -std=c++17
```

### Windows (MSVC)

1. Создать новый **DLL** проект (x86 / Win32)
2. Добавить `d3d9_l2.cpp`
3. Linker → Input → Additional Dependencies: `d3d9.lib`, `dxguid.lib`
4. Собрать **Release x86**

---

## Для разработчиков

Полный vtable враппер `IDirect3D9` / `IDirect3DDevice9`. Все методы делегируются реальному устройству. Нет зависимостей кроме системного d3d9.

**Реализовано:**
- Перехват `SetRenderState` / `SetSamplerState` / `Present` / `Reset`
- `IDirect3DDevice9Ex` через QI на живом устройстве — не отдельный враппер. На DXVK всегда успешен независимо от типа `CreateDevice`. На нативном Windows возможен `E_NOINTERFACE` на старых драйверах; некоторые AMD принимают, но игнорируют. Оба случая обрабатываются gracefully.
- Детектор рендер-стейтов: fog параметры, lighting, количество семплеров, flare-кандидаты — дамп в лог при выходе или по хоткею
- Проба форматов depth-текстуры (`DF24` / `DF16` / `INTZ`) при старте
- `sampler_apply` покрывает 8 семплеров (детектор зафиксировал max 5 одновременных в L2 Interlude)

**Можно добавить поверх:**
- Перехват draw calls для анализа геометрии
- Замена шейдеров через `CreatePixelShader` / `CreateVertexShader`
- HUD-оверлей через `DrawPrimitiveUP` в `EndScene`
- Чтение depth buffer (на DXVK заблокировано по дизайну; работает на Gallium Nine и нативном Windows)
- Статистика кадров, логирование RT-переходов

Основан на [d3d9-depth-proxy](https://github.com/guglovich/d3d9-depth-proxy). Отличия: `IDirect3DDevice9Ex` через QI на живом устройстве вместо отдельного враппера; `sampler_apply` на 8 семплеров вместо 16.
