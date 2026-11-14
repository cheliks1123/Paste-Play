# StreamPlayer

Нативное окно Windows для веб-плеера **Paste & Play**. Внутри — тот же
`index.html`, что и в референсе: интерфейс не перерисовывается в C++, поэтому
вид и поведение совпадают один в один.

## Как это устроено

```
StreamPlayer.exe
├── ресурсы (RCDATA): assets/index.html, assets/media/demo.mp4, poster.jpg
├── при первом запуске распаковка в %LOCALAPPDATA%\StreamPlayer\app\
├── WebView2: папка отдаётся как https://app.local/
└── окно: страница на весь размер, своя тёмная рамка, F11 / fullscreen
```

Почему через виртуальный хост, а не `file://`:

| | `file://` (как в референсе) | `https://app.local` (здесь) |
|---|---|---|
| кнопка «вставить из буфера» | часто запрещена | работает (secure context) |
| `localStorage` (история, настройки) | ограничен | работает |
| iframe-плееры (YouTube, VK, Rutube, Vimeo) | работают | работают |
| Twitch (`parent=`) | не работает, нет домена | домен есть (`app.local`) |

## Сборка (GitHub Actions)

1. Залей папку `StreamPlayer` в репозиторий на GitHub.
2. Вкладка **Actions** → workflow **build** (запускается на push или вручную).
3. Внизу страницы запуска → **Artifacts** → `StreamPlayer-x64` → `StreamPlayer.exe`.

Сборка сама скачивает WebView2 SDK с nuget.org (только на раннере). В exe
встроен статический загрузчик WebView2 — отдельные DLL не нужны.

## Требования

- Windows 10/11 x64.
- WebView2 Runtime: в Windows 11 уже есть; на Windows 10 обычно приходит с Edge.
  Если его нет — программа покажет сообщение.

## Что умеет (как в референсе)

- Вкладки **смотреть · история · сервисы**, вставка ссылки, Ctrl+V сразу запускает.
- YouTube, Vimeo, Rutube, VK Video, Dailymotion, Twitch, OK.ru, прямые файлы
  `.mp4 .webm .m3u8 .mpd`.
- Свои контролы плеера: пауза, ±10 сек, звук, повтор, скорость, PiP, фullscreen.
- Перетаскивание ссылки или файла в окно.
- Горячие клавиши страницы (Space, ←/→, J/L, ↑/↓, M, F, P, 0–9).

Дополнительно от оболочки:

- `F11` — окно на весь экран;
- переход в fullscreen у видео разворачивает само окно;
- `window.open` («открыть в источнике») уходит в системный браузер;
- тёмная рамка окна и тёмный фон WebView2 (нет белой вспышки).

## Где хранятся данные

- Интерфейс: `%LOCALAPPDATA%\StreamPlayer\app` (распаковывается из exe).
- Профиль браузера (куки, кэш, localStorage): `%LOCALAPPDATA%\StreamPlayer\WebView2`.

Удаление папки `%LOCALAPPDATA%\StreamPlayer` = полный сброс.

## Как менять интерфейс

1. Замени `assets/index.html` (и `assets/media/*`, если нужно).
2. Увеличь `kAssetsVersion` в `src/main.cpp` — иначе останется старая распаковка.
3. Собери заново.

## Ограничения

- DRM-сервисы (Netflix и подобные) могут не работать — зависит от сервиса.
- Twitch может отказать в `parent=app.local` — это не публичный домен.
- hls.js для `.m3u8` грузится с CDN, без интернета HLS не заработает.

## Структура

```
CMakeLists.txt              сборка (MSVC, C++17, WebView2 SDK с nuget)
src/main.cpp                окно, распаковка ресурсов, WebView2
src/appfiles.rc.in          шаблон .rc со ссылками на assets/
assets/index.html           сам плеер (референс)
assets/media/demo.mp4       демо-ролик
assets/media/poster.jpg     постер
.github/workflows/build.yml CI: сборка + артефакт
```
