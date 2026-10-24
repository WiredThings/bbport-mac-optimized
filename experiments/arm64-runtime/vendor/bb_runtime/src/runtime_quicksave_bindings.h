/* SPDX-License-Identifier: GPL-2.0-or-later
 * Single-key menu/checkpoint shortcuts, shared by both architectures and the launcher. */
#pragma once
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { SDL_Scancode save, load; } QuickBindings;
static inline SDL_Scancode shortcut_from_env(const char *name, SDL_Scancode fallback) {
    const char *text=getenv(name);
    if (!text || !*text) return fallback;
    char *end=NULL;
    const long code=strtol(text,&end,10);
    if (!end || *end || code<SDL_SCANCODE_A || code>=SDL_SCANCODE_LCTRL ||
        code==SDL_SCANCODE_CAPSLOCK || code==SDL_SCANCODE_NUMLOCKCLEAR ||
        code==SDL_SCANCODE_ESCAPE || code==SDL_SCANCODE_F9 ||
        !*SDL_GetScancodeName((SDL_Scancode)code)) {
        fprintf(stderr,"Shortcut: %s is invalid or reserved\n",name);
        return SDL_SCANCODE_UNKNOWN;
    }
    return (SDL_Scancode)code;
}
static inline int menu_shortcut_configured(void) {
    const char *text=getenv("BB_MENU_SCANCODE");
    return text && *text;
}
static inline SDL_Scancode menu_shortcut_code(void) {
#ifdef __APPLE__
    const SDL_Scancode fallback=SDL_SCANCODE_F1;
#else
    const SDL_Scancode fallback=SDL_SCANCODE_INSERT;
#endif
    const SDL_Scancode code=shortcut_from_env("BB_MENU_SCANCODE",fallback);
    return code ? code : fallback;
}
static inline int menu_shortcut_uses(SDL_Scancode code) {
    if (menu_shortcut_configured()) return code==menu_shortcut_code();
    /* Existing shell launchers keep their aliases. The visual launcher selects one key. */
#ifdef __APPLE__
    return code==SDL_SCANCODE_F1 || code==SDL_SCANCODE_GRAVE ||
           code==SDL_SCANCODE_NONUSBACKSLASH || code==SDL_SCANCODE_INSERT;
#else
    return code==SDL_SCANCODE_INSERT;
#endif
}
static inline int menu_shortcut_matches(const SDL_KeyboardEvent *event) {
    return menu_shortcut_uses(event->scancode) &&
           !(event->mod & (SDL_KMOD_CTRL|SDL_KMOD_SHIFT|SDL_KMOD_ALT|SDL_KMOD_GUI));
}
static inline const char *menu_shortcut_label(void) {
    if (menu_shortcut_configured()) return SDL_GetScancodeName(menu_shortcut_code());
#ifdef __APPLE__
    return "F1 / ` / § / Insert";
#else
    return "Insert";
#endif
}
static inline SDL_Scancode quick_key_from_env(const char *name, SDL_Scancode fallback) {
    const SDL_Scancode code=shortcut_from_env(name,fallback);
    if (menu_shortcut_uses(code)) {
        fprintf(stderr,"Quicksave: %s is mapped to Configuration menu; shortcut disabled\n",name);
        return SDL_SCANCODE_UNKNOWN;
    }
    return code;
}
static inline QuickBindings quick_bindings_from_env(void) {
    QuickBindings keys={quick_key_from_env("BB_QUICKSAVE_SCANCODE",SDL_SCANCODE_S),
                        quick_key_from_env("BB_QUICKLOAD_SCANCODE",SDL_SCANCODE_L)};
    if (keys.save==keys.load) {
        fprintf(stderr,"Quicksave: save/load keys must be different; shortcuts disabled\n");
        keys.save=keys.load=SDL_SCANCODE_UNKNOWN;
    }
    return keys;
}
static inline int quick_binding_uses(QuickBindings keys, SDL_Scancode code) {
    return code!=SDL_SCANCODE_UNKNOWN && (code==keys.save || code==keys.load);
}
