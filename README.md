# RayClay

**Make the world a better place and build your JavaScript application in C - RayClay.**

It is undeniable that the unholy trinity (HTML, CSS, JS) produce some beautiful looking UIs. It is also undeniable that when you bundle your 5MB ToDo app with the entire 100MB Chromium browser engine I have to cry myself to sleep at night thinking about all the bloatware polluting unsuspecting consumers. No, your 200MB desktop application with 3 simple features is not a normal size.

At the same time, I fully recognise the lure of JavaScript applications because so many tools have made them so easy to spin up. And they do look good.

RayClay abstracts enough of the verbose nature of C away from the developer where UI can be written in similar LOC as JavaScript and you can even use Tailwind CSS syntax. Basically, you can build your JavaScript application (in C) but now you can actually ship it at 5MB.

> RayClay is a complete pure-C UI framework built by packaging together a bunch of well-maintained, open-source, high-performance libraries. RayClay aims to be capable of modern styling and design principles much like CSS.

> Out-of-the-box, the RayClay desktop application aims to behave like a modern web application in a modern browser.

> We are on the journey to v1.0; A successful RayClay v1.0 means that a developer can build a desktop, web, and mobile gui from the same source code. At this stage, my focus has largely been scoped to desktop because the driving force behind RayClay is to provide developers a way to build super easy good-looking desktop guis which I hope will reduce the number of JavaScript desktop applications polluting the world. But the ultimate goal for RayClay v1.0 is that a developer can make a gui once, then ship it as a desktop app, mobile app, and web app all from the same source code.

Check out the [RayClay GitHub](https://github.com/impizulu/rayclay).

- **[RayClay cheatsheet](https://www.rayclay.dev/docs/cheatsheet)** - every public function in the API,
  one line each. The fastest way to find the call you want.
- **[RayClay template](https://github.com/impizulu/rayclay-template)** - a starter app with a custom
  titlebar, a collapsible sidebar and browser-style zoom. Clone it and start deleting.

---

## API

### Naming Conventions

| Category | Convention | Example |
|---|---|---|
| Public function or function-like macro | `rc` + PascalCase | `rcAppCreate`, `rcBox` |
| Public type | `RC_` + PascalCase | `RC_AppOptions` |
| Public constant, enum value, or value-like macro | `RC_` + UPPER_SNAKE | `RC_GROW`, `RC_PX` |
| Public callback type | `RC_` + PascalCase + `Callback` | `RC_LogCallback` |

Anything you call, pass or set is spelled `rc` or `RC_`. Names beginning `rci_` or
`RCI_` are internal — they can change in any release, so don't depend on them.

### Public Functions

All public functions and element macros are in the single `rayclay.h` you include. Full one-line reference: [`docs/cheatsheet.md`](docs/cheatsheet.md).

Start at the top of the ladder and descend only as far as you actually need:

```c
rcRunApp(NULL)                              // zero-config: a window + the welcome canvas
rcRunApp(&opts)                             // your layout and full RC_AppOptions control
rcAppCreate / rcRunFrame / rcAppDestroy     // you own the frame loop
```

| Module | Key symbols |
|---|---|
| Layout | `rcBox` `rcRow` `rcColumn` `rcSeparator` `rcMargin` `rcVirtualList` *(macros)* |
| Text | `rcTextL` `rcText` `rcTextC` *(macros)* · `rcLoadFont` `rcRegisterFont` `rcFont` |
| Widgets | `rcButton` `rcCheckbox` `rcToggle` `rcSlider` `rcProgress` `rcRadio` `rcCombo` |
| Text entry | `rcTextInput` `rcTextArea` *(macros over `rcTextInputEx`)* |
| Menus & modals | `rcBeginMenu` `rcMenuItem` `rcBeginContextMenu` `rcBeginModal` |
| Interaction reads | `rcIsHovered` `rcClicked` `rcIsFocused` `rcSetFocus` |
| Keyboard | `rcKeyDown` `rcKeyPressed` `rcKeyReleased` `rcModDown` |
| Scrolling | `rcScrollBy` `rcScrollToTop` `rcScrollToBottom` `rcScrollbar` `rcScrollDeltaX/Y` |
| Zoom | `rcAppZoom` `rcAppSetZoom` `rcAppZoomMode` `rcAppSetZoomMode` |
| Titlebar *(desktop-only)* | `rcTitlebar` `rcWindowControls` `rcWindowControlButton` |
| Theme | `rcStyleDark` `rcStyleLight` `rcGetStyle` `rcSetStyle` |
| Images & icons | `rcLoadImage` `rcLoadImageFromMemory` `rcUnloadImage` · `RC_IconCallback` |
| Clipboard | `rcClipboardRequest` `rcClipboardPoll` `rcClipboardSet` `rcSetClipboardImpl` |
| Cursor | `rcSetCursor` |
| Colour | `rcRgb` `rcRgba` `rcHex` `rcAlpha` |
| Utilities | `rcFormat` `rcStrCopy` · `rcArena*` |

### Element Macros

Containers, text and the text-entry fields are **macros**, not functions — designated initialisers with a brace body. Widgets, menus, modals and interaction reads are plain functions.

```c
rcColumn(.id = "sidebar", .bg = RC_SLATE_800, .p = 12, .gap = 8) {
    rcTextL("Inbox", .size = 18, .color = RC_WHITE);
    if (rcButton("send", "Send", RC_BTN_PRIMARY))
        send_message();
}
```

Sizing tokens: `RC_FIT`, `RC_GROW`, `RC_PX(n)`, `RC_PCT(n)`.

### Desktop-only surface

`rcTitlebar`, `rcWindowControls`, `rcWindowControlButton` and the `RC_ID_WINDOW_*` ids (`MINIMIZE` / `MAXIMIZE` / `CLOSE` / `DRAG` / `NODRAG`) compile on every target but emit nothing on the web, where the browser tab is the window chrome. Same source, no `#ifdef`.

---

## Developer Principles

To quote an extract from [learncpp.com](https://www.learncpp.com/cpp-tutorial/introduction-to-cpp-development/), typically, good solutions have the following characteristics:
- They are straightforward (not overly complicated or confusing).
- They are well documented (especially around any assumptions being made or limitations).
- They are built modularly, so parts can be reused or changed later without impacting other parts of the program.
- They can recover gracefully or give useful error messages when something unexpected happens.

To add a few of my own...

...non-negotiables:
- open source (free for the world to use and redistribute).
- os agnostic (can compile on any os).
- lightweight (or produces lightweight outputs).
- good 'big business' adopters (improves longevity).

...preferences:
- simple to use. not a headache to learn and setup.
- large community. lots of tutorials.
- capable of flexible, modern builds. does not feel/look outdated.

Why C? To quote the [sokol README](https://github.com/floooh/sokol#why-c):

- easier integration with other languages
- easier integration into other projects
- adds only minimal size overhead to executables

---

## Third Parties

| Name | GitHub | Site | Language | License | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Clay | [github.com - clay](https://github.com/nicbarker/clay) | [www.nicbarker.com - clay](https://www.nicbarker.com/clay) | C | zlib/libpng | Clay, short for C Layout, is a 2D flex-box style UI auto layout library in C by Nic Barker. It's sort of like CSS except in C. |
| glad | [github.com - glad](https://github.com/dav1dde/glad) | [glad.dav1d.de](https://glad.dav1d.de/) | C | MIT AND Apache-2.0 | Vulkan/GL/GLES/EGL/GLX/WGL Loader-Generator based on the official specifications for multiple languages. |
| GLFW | [github.com - glfw](https://github.com/glfw/glfw) | [glfw.org](https://www.glfw.org/) | C | zlib/libpng | GLFW (windowing and user inputs) creates the blank application window and listens for mouse clicks and keyboard presses. |
| nobar | *custom* | *custom* | C | MIT | header-only, backend agnostic titlebar removal library written in C. |
| Roboto | font | font | NA | OFL | font. |
| sokol | [github.com - sokol](https://github.com/floooh/sokol) | [floooh.github.io/sokol-html5](https://floooh.github.io/sokol-html5/) | C | zlib/libpng | Minimal cross-platform graphics abstraction |
| stb | [github.com - stb](https://github.com/nothings/stb) | NA | C | MIT / public domain | a collection of single-file header-file libraries for C/C++ in the public domain |

### Custom Third Party: Nobar - OS Titlebar Removal

> Custom lib built for RayClay, but can be used in other projects.

Goal: header-only, backend agnostic titlebar removal library written in C.

Works on desktop (Windows, Linux, macOS).

Criteria: The window must retain all decorations except for the titlebar. The window must maintain all of its OS-native decorations like resizing, window management, border, shadow, etc. The only decoration that should be removed is the titlebar.

Reason: Developer can make fully custom titlebar. But all window decorations remain intact so user can continue using full OS-native suite of features like window management etc. so user experience is not degraded.
