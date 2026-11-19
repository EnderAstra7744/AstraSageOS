FroggyCode 2.0.0 - Froggy, the pixel frog of AstraSageOS (a native C program).
Start: as froggy (or the desktop icon). Esc closes the settings (a second Esc quits).

Layout: left bar (chat / projects / plugins), top bar (new chat, history, settings), bottom dock (Terminal + Console), right history panel.
Froggy is rule based - not an AI model. It lists/reads your files, writes example programs (hello.as, counter.as, window.as ...),
runs AST commands, shows installed ASPI packages you tick as plugins, and looks unknown things up on Wikipedia when you allow web search.
If it cannot find an answer it asks you and saves your answer in its memory (Settings > Memory).
Every action asks first: "Her zaman" (always) or "Sadece Bu Sefer" (only this chat); "always" can be revoked in Settings.
Limits: chat, memory and settings files are limited to 4096 bytes each (the OS file limit), the web lookup needs a network (as net dhcp).
