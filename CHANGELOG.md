# Changelog

## [0.0.0] - 2026-09-15

### Added
- Added six degrees of freedom head tracking for the RoadCraft chase and cockpit cameras, driven by any OpenTrack compatible tracker over UDP.
- Held head tracking off in menus, the map, photo mode, on loading screens and while a multiplayer session is running.
- Scaled head tracking to the field of view the camera is rendering, so the chase camera's Dynamic FOV zoom does not change how far a head turn moves the view. The Camera settings stay the reference it scales against, and it follows one that is changed mid-game.
- Centred a windowed game on the monitor it opens on, once it has finished placing its window. A fullscreen or borderless window, and one the game already centred, are left alone.
