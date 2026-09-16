# RoadCraft Head Tracking

![RoadCraft running with this mod](https://raw.githubusercontent.com/itsloopyo/roadcraft-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for RoadCraft that moves the camera with your head while your wheel or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

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

Success looks like a `HeadTracking.ini` and a `HeadTracking.log` appearing in `root\bin\pc` after the first launch, with the log reading `[build] activated profile steam-win64-20260902` and a `[camera] hooked Camera::SetTransform at ...` line.

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

Both columns fire the same action. All four keys are remappable in `[Hotkeys]`.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

Head tracking works in the chase camera and the cockpit camera. Your head turns the view about the camera's own axes.

Centering is done in the tracker: OpenTrack's Center bind, the center button in your phone app, or SteamVR's reset.

## Configuration

`HeadTracking.ini` is written to `root\bin\pc` on first run and read at startup. Edit it and restart the game. Every key it holds, with the shipped default:

```ini
[Network]
UdpPort=4242

[General]
EnableOnStartup=1

[Hotkeys]
; Windows virtual key codes, in hex. Each action has a nav-cluster key and a
; Ctrl+Shift+<key> chord, and both fire it.
ToggleKey=0x23
CycleModeKey=0x21
ChordToggleKey=0x59
ChordCycleModeKey=0x47

[Smoothing]
; 0.0 none .. 1.0 heavy. Covers rotation and position. LocalSmoothing applies
; to a tracker sending to 127.0.0.1; RemoteSmoothing to anything arriving over
; the network, including this PC's own LAN address.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
Enabled=1
; How far the head may move the camera, in metres, 0 to 0.5. LimitZ is leaning
; forward and LimitZBack is leaning away; LimitY is up and LimitYDown is down.
LimitX=0.30
LimitY=0.20
LimitYDown=0.20
LimitZ=0.40
LimitZBack=0.10
```

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

- Raise `RemoteSmoothing` if the tracker is on another device, or `LocalSmoothing` if it is on this PC.
- If a phone app is sending direct, route it through OpenTrack so its filters can clean up the feed.

**Tracking feels stronger or weaker than it should:**

- The log's `[camera] now driving camera` line shows what that camera is rendering and the factor the head pose is scaled by. It reads `1.0000` when the camera is at the field of view you set in **Settings > Camera**. Away from that the `[camera] zoomed to` lines follow the game's own Dynamic FOV as it widens the frame, and a `[camera] ... now rests at` line means the mod read a change as you having moved a slider.

**Edits to `HeadTracking.ini` do nothing:** the file is read once at startup, so restart the game. If a single value is being ignored, the log names it and says what it used instead.

## Updating

Download the new release and run `install.cmd` again. Your `HeadTracking.ini` is preserved.

That also means a key added by a newer release is not written into the file you already have. Copy the block for it out of the configuration section above, or delete `HeadTracking.ini` and start the game once to get a fresh one with every key in it.

## Uninstalling

Run `uninstall.cmd`. This removes `RoadCraftHeadTracking.asi` and the mod's log files. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway. Your `HeadTracking.ini` is left alone either way.

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
