# Changelog

## [Unreleased]

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `root\bin\pc\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A first start with no `HeadTracking.ini` no longer writes one. It creates `CameraUnlock.ini` instead.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] ToggleKey` and `ChordToggleKey`, virtual key codes, are imported together into `ToggleKey`, and `CycleModeKey` and `ChordCycleModeKey` into `CycleTrackingModeKey`, each as the plain key and Ctrl+Shift with the chord key.
- The tracking mode (Page Up / Ctrl+Shift+G) is saved to `CameraUnlock.ini` when you change it, and the game starts in the mode you left it in. Turning tracking on or off (End / Ctrl+Shift+Y) is still not saved; the game starts with head tracking on or off as `EnableOnStartup` says.
- The keys are renamed to the names every head tracking mod on `CameraUnlock.ini` uses: the lean limits `LimitX`, `LimitY`, `LimitYDown`, `LimitZ` and `LimitZBack` are `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and `PositionLimitZBack`. `[Position] Enabled`, which chose the mode tracking started in, is imported as that mode: `Enabled=0` starts in rotation only, as it did.

## [0.1.0] - 2026-09-16

### Added
- Added six degrees of freedom head tracking for the RoadCraft chase and cockpit cameras, driven by any OpenTrack compatible tracker over UDP.
- Held head tracking off in menus, the map, photo mode, on loading screens and while a multiplayer session is running.
- Scaled head tracking to the field of view the camera is rendering, so the chase camera's Dynamic FOV zoom does not change how far a head turn moves the view. The Camera settings stay the reference it scales against, and it follows one that is changed mid-game.
- Centred a windowed game on the monitor it opens on, once it has finished placing its window. A fullscreen or borderless window, and one the game already centred, are left alone.
