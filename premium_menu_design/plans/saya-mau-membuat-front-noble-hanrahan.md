# Plan: Premium Enterprise Mod Menu (Front-end only)

## Context
User wants a premium, enterprise-grade front-end mod menu UI (single-player trainer style) covering 9 categories / ~55 features listed in the brief. UI mockup only — toggles/sliders hold local state, no game hooking.

## Aesthetic direction
- Invoke `aesthetic-stance` skill + `create_make_theme` first; write `guidelines/Guidelines.md` as instructed.
- Stance (less-common choice): "instrument panel / avionics console" — warm graphite canvas, bone-white text, single signal-amber accent, hairline rules, mono numerics. Not neon-cyberpunk.
- Fonts in `src/styles/fonts.css`: Space Grotesk (UI) + JetBrains Mono (values/hotkeys).
- Update token values only in `src/styles/theme.css` (keep `--background`, `--foreground`, `--border`, `@theme inline` contract).

## Layout (`src/app/App.tsx`, default export)
- **Left rail**: brand mark, 9 category nav (lucide icons, active-count badge per category), profile/preset switcher, footer with build version.
- **Top bar**: search (filters all features across categories), process status ("Attached · game.exe · PID"), global panic/disable-all button, hotkey hint.
- **Main panel**: category header (title, description, enabled count), grid of feature cards. Each card: name, Indonesian description, hotkey chip, and control type:
  - toggle (most features)
  - toggle + slider (Damage Multiplier 2x–9999x log scale, Attack Speed, Hitbox reach, Super Speed, EXP multiplier, Custom FOV, Time Scale)
  - special panels: Custom Stat Editor (5 attribute steppers + radar-ish bars), Teleport (waypoint list + XYZ inputs), Weather/Time (segmented control + freeze), Item/NPC Spawner (ID input, quantity, spawn button, recent list), Quest Flag Editor (table of flags with set/reset).
- **Right inspector**: live session readout (active mods list, mock HP/Stamina/Mana bars reacting to toggles), activity log of toggle events with timestamps.
- Toasts via `sonner` on toggle; subtle `motion/react` transitions for panel switch.

## Data
Single `CATEGORIES` const array: `{id, label, icon, features: [{id, name, desc, hotkey, control, min, max, unit}]}` with all brief items in Indonesian. State: `Record<featureId, {on, value}>`.

## Verification
- Vite dev server already running; check preview: navigate every category, search filtering, toggles update counts/inspector/log, sliders, spawner & teleport forms, panic button resets all.
- Run `npx tsc --noEmit` (or build) to confirm no type errors.
