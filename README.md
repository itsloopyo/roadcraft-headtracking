# RoadCraft Head Tracking

![RoadCraft running with this mod](https://raw.githubusercontent.com/itsloopyo/roadcraft-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for RoadCraft that moves the camera with your head while your wheel or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

> **Updating from v0.1.0?** Settings now live in `CameraUnlock.ini`, next to `HeadTracking.ini` in `root\bin\pc`. The first start of this version reads your `HeadTracking.ini` into it and never changes `HeadTracking.ini` afterwards, so edit `CameraUnlock.ini` from then on. See [Configuration](#configuration).

## Features

- **6DOF positional tracking** - lean, peek and duck with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- RoadCraft on [Steam](https://store.steampowered.com/app/2104890/). The mod carries a build profile for the Steam copy only; a copy bought from any other store runs vanilla, as below
- A tracking source that sends the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) with a webcam
- Windows 10 or 11, 64-bit

## Installation

### Lopari

Once this mod is available in Lopari, download [Lopari](https://lopari.app), choose **RoadCraft**, and click **Play with head tracking**.

### Standalone Installer

1. Download the `RoadCraftHeadTracking-...-installer.zip` from the [releases page](https://github.com/itsloopyo/roadcraft-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1` port `4242`.
5. Launch the game.

The installer puts two files next to `Roadcraft - Retail.exe`: `RoadCraftHeadTracking.asi` (the mod) and `dinput8.dll` (the bundled Ultimate ASI Loader, which the game already imports so the loader is picked up on start).

`Roadcraft - Retail.exe` is not at the top of the game folder. It lives in `root\bin\pc`, and that is where both files go. The loader only looks in the directory the exe is in, so a copy anywhere else does nothing.

Success looks like a `CameraUnlock.ini`, the mod's settings file, and a `HeadTracking.log` appearing in `root\bin\pc` after the first launch, with the log reading `[build] activated profile steam-win64-20260902` and a `[camera] hooked Camera::SetTransform at ...` line.

If the installer cannot find your game, point it at the folder yourself, either with an environment variable:

```powershell
$env:ROADCRAFT_PATH = "D:\Games\RoadCraft"
.\install.cmd
```

or by passing the path as an argument:

```powershell
.\install.cmd "D:\Games\RoadCraft"
```

Give it the folder that contains `root`, not `root\bin\pc` itself.

### Manual Installation

Copy `plugins\RoadCraftHeadTracking.asi` and `vendor\ultimate-asi-loader\dinput8.dll` out of the ZIP into `<game>\root\bin\pc`, beside `Roadcraft - Retail.exe`. The loader keeps its name; nothing needs renaming.

Mod managers do not deploy this mod. A mod manager installs into one fixed subtree of the game folder, and the files have to sit beside the exe in `root\bin\pc` for the loader to find them. There is no Nexus archive for this mod for that reason. Use `install.cmd`, or copy the two files by hand.

## Setting Up OpenTrack

1. Set **Input** to whatever tracker you use.
2. Set **Output** to `UDP over network`, host `127.0.0.1`, port `4242`.
3. Press **Start**.
4. Center with OpenTrack's own Center bind while sitting the way you drive.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1` port `4242`.

### Webcam Setup

Set OpenTrack's **Input** to `Neuralnet tracker`. It tracks your face from a plain webcam, with no markers, clips or IR hardware to fit.

### Phone App Setup

Your app has to send the OpenTrack UDP protocol, either from the phone or through a companion program on the PC. Some phone trackers use something else, so check that first.

There are two ways to wire it up:

- **Straight to the game.** Point the app at your PC's local IP address, port `4242`.
- **Through OpenTrack.** Set OpenTrack's **Input** to your app, then follow [Setting Up OpenTrack](#setting-up-opentrack).

Try it straight to the game first. Hold your head still and watch: if the view shakes or creeps, your app is sending a rough feed, and putting OpenTrack in the middle will clean it up. Use OpenTrack anyway if you want its curves.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It smooths on the phone, so it goes straight to the game. Any app that filters as much works the same way.

Either route, the mod picks between its two smoothing values by where the packets came from, and it goes by address rather than by machine. Any `127.x.x.x` address counts as local. A phone on your WiFi gets `RemoteSmoothing`, which is what you want, but so does OpenTrack running on this same PC if you have pointed it at your PC's own network address. Send to a loopback address to get `LocalSmoothing`.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |

Both columns fire the same action. Each action's keys are one list in `[Hotkeys]` in `CameraUnlock.ini`, `ToggleKey` and `CycleTrackingModeKey`, the chord included, so any of them can be changed or removed.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The mode you pick is saved to `CameraUnlock.ini`, and the game starts in it next time. `End` / `Ctrl+Shift+Y` changes the current session only; whether tracking is on when the game starts is `EnableOnStartup`.

Head tracking works in the chase camera and the cockpit camera. Your head turns the view about the camera's own axes.

Centering is done in the tracker: OpenTrack's Center bind, the center button in your phone app, or SteamVR's reset.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `root\bin\pc\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`

With every setting at its default, the file reads:

```ini
; RoadCraft head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
```
<!-- /cameraunlock:config -->

Changes take effect the next time the game starts.

Hotkeys are written as key names, such as `End`, `PageUp`, `F9` or `Ctrl+Shift+Y`, separated by commas. A key with no name can be written as its Windows virtual key code, `0x` and two hex digits, such as `0xBA`. A value the mod cannot read leaves that setting at its default and is named in `HeadTracking.log`.

Sensitivity, deadzones, curves and axis inversion are set up in your tracker, so one profile behaves the same in every game.

### Window placement

A windowed game is moved once to the centre of the desktop work area on the monitor it opened on, after its window has stopped moving. That is the screen minus the taskbar, so it sits a little above the middle of the glass. A fullscreen or borderless window is left where it is, and so is one the game already centred, as is any window at all on a game build the mod has no profile for. The `[boot] window:` line in the log says which of those happened. There is no setting for it.

### Field of view

The game has its own field of view controls, under **Settings > Camera**: *First-Person camera FOV*, *Third-Person camera FOV* and *Dynamic FOV*. This mod adds none of its own, so set them there.

Head tracking is scaled to whatever the camera is actually rendering, so the game's own zoom does not change how far your head moves the view. With **Dynamic FOV** on, the chase camera widens as you zoom it in, and the mod follows that frame by frame rather than letting the tracking feel weaker the closer the camera gets.

The sliders themselves are the reference it scales against, not something it cancels out: moving one does change how far a head turn sweeps the view, the same way it changes everything else in the frame. Move a slider mid-game and the mod picks the new setting up on the next frame that renders at it.

## Troubleshooting

Read `HeadTracking.log`, in `root\bin\pc`. It records the game folder, the build profile it matched or refused, the config it loaded, the camera it hooked, and the first head pose that reached the camera.

**Mod not loading:**

- No log file at all means the loader is not being picked up. Check that `dinput8.dll` and `RoadCraftHeadTracking.asi` are both in `root\bin\pc`, beside `Roadcraft - Retail.exe`, and not in the game's top folder.
- If the log says the mod stayed dormant, your `Roadcraft - Retail.exe` is not a build this mod has a profile for. The log line says whether your build is newer or older than the one it knows; a newer one needs a mod update.

**No tracking response:**

- Check the log for `[udp] tracker data is arriving`. If that line never appears the packets are not reaching the mod: confirm the tracker's output host and port match `UdpPort`, and that no firewall rule is dropping them.
- If that line is there but `[camera] first composed frame` is not, the data is arriving and something is holding it back. Press `End` (or `Ctrl+Shift+Y`) in case tracking is toggled off, and read the `[state]` line below.
- The mod cannot bind a port another program is already holding. The log records the reason the socket gave rather than naming a culprit, so a `[udp]` line carrying error 10048 is a port already in use, most likely OpenTrack or another game with a head tracking mod. The mod retries every half second, so closing the other app is enough and the game does not need a restart.
- Head tracking is held off on purpose in the pause menu, the map, photo mode, on loading screens, and while a multiplayer session is running. The log line starting `[state]` says which of those it is seeing. The multiplayer check has been tested by hosting a session, not by joining someone else's. If head tracking follows your head in a multiplayer game, please say so on Discord.

**Jittery or unstable tracking:**

- Raise `RemoteSmoothing` if the tracker is on another device, or `LocalSmoothing` if it is on this PC, both in `[Smoothing]` in `CameraUnlock.ini`.
- If a phone app is sending direct, route it through OpenTrack so its filters can clean up the feed.

**Tracking feels stronger or weaker than it should:**

- The log's `[camera] now driving camera` line shows what that camera is rendering and the factor the head pose is scaled by. It reads `1.0000` when the camera is at the field of view you set in **Settings > Camera**. Away from that the `[camera] zoomed to` lines follow the game's own Dynamic FOV as it widens the frame, and a `[camera] ... now rests at` line means the mod read a change as you having moved a slider.

**Edits to `CameraUnlock.ini` do nothing:** the file is read once at startup, so restart the game. If a single value is being ignored, the log names it. Once `CameraUnlock.ini` exists the mod no longer reads `HeadTracking.ini`, so an edit there changes nothing.

## Updating

Download the new release and run `install.cmd` again. Your `CameraUnlock.ini` is kept. Updating from v0.1.0, the first start reads your settings from `HeadTracking.ini` into a new `CameraUnlock.ini`; see [Configuration](#configuration).

## Uninstalling

Run `uninstall.cmd`. This removes `RoadCraftHeadTracking.asi` and the mod's log files. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway. `CameraUnlock.ini` and `HeadTracking.ini` are left in place either way, so your settings are still there if you install again.

## Building from Source

Needs [pixi](https://pixi.sh) and Visual Studio with the C++ toolchain.

```powershell
git clone --recursive https://github.com/itsloopyo/roadcraft-headtracking.git
cd roadcraft-headtracking
pixi run build      # RoadCraftHeadTracking.asi
pixi run test       # unit tests
pixi run package    # the release ZIP
```

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

The loader it ships and the libraries compiled into the `.asi` keep their own licenses, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), which travels at the root of the release ZIP. Copies also sit under `licenses/` and beside the vendored loader.

## Credits

- RoadCraft by [Saber Interactive](https://saber.games/), published by [Focus Entertainment](https://www.focus-entmt.com/)
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu
- [OpenTrack](https://github.com/opentrack/opentrack)
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared head tracking pipeline behind these mods

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Saber Interactive or Focus Entertainment. Use at your own risk.
