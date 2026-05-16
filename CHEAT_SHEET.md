# feedback.exe — cheat sheet

One page. Keys only. Full reference → [REFERENCE.md](REFERENCE.md).

## Layers (toggle on/off)

`F1` warp · `F2` optics · `F3` gamma · `F4` color · `F5` contrast · `F6` decay
`F7` noise · `F8` couple · `F9` external · `F10` inject · `Ins` physics · `PgDn` thermal

## Parameters (hold `Shift` for ×20 coarse)

`Q/A` zoom · `W/S` rotation · `←→↑↓` translate
`[/]` chroma · `;/'` blur-X · `,/.` blur-Y · `-/=` blur-angle
`G/B` gamma · `E/D` hue rate · `R/F` saturation · `T/Y` contrast
`U/J` decay · `N/M` noise · `K/I` couple · `O/L` external (cam)
`V` invert · `X/Z` sensor-γ · `8/7` sat-knee · `0/9` color-xtalk
Numpad: `4/1` amp · `5/2` scale · `6/3` speed · `8/7` rise · `0/9` swirl

## Inject patterns (hold `Space`)

`1` H-bars · `2` V-bars · `3` dot · `4` checker · `5` gradient
`6` noise · `7` rings · `8` spiral · `9` polka · `0` starburst
`Alt+B` bouncer (10-sec animated box)
Hold `Alt+1` triangle · `Alt+2` star · `Alt+3` circle · `Alt+4` square

## Quality cycles

`PgUp` blur kernel · `F12` CA sampler · `Home` noise archetype · `End` fields
`Delete` pixelate style · `Ctrl+Del` CRT bleed · `Alt+Del` reroll burn
`Alt+S` true 3D sphere volume · left-drag rotate sphere view

## Control UI

`Tab`/`H` open/close sticky YAML UI · `[`/`]` section · `↑/↓` select · `←/→` nudge
`Enter` exact value entry / toggle · `P` pin selected control · `Esc` close UI

## Output

`Ctrl+↑/↓` fade (feeds back) · `Alt+↑/↓` brightness (display only)
VFX-1: `Alt+[/]` cycle · `Alt+\` off · `Alt+;/'` param · `Alt+/` B-src
VFX-2: add `Ctrl+` to each chord

## BPM + music

`Tab` tap tempo · `Ctrl+Tab` sync · `Alt+Tab` division cycle
Modulations — `Ctrl+Alt+` `I` inject · `S` strobe · `V` vfx-cycle · `F` flash · `D` decay-dip
`Ctrl+Alt+H` hue-jump toggle · `Ctrl+Alt+=/−` step (0–100; 25=¼, 50=½, 12 default)
`Ctrl+Alt+R` beat-invert toggle · `Ctrl+Alt+,/.` flip divisor
`Ctrl+Alt+N/P` music preset · `Ctrl+Alt+Space` music play/pause
`Space` (hold) jump to breakbeat preset while held
`Ctrl+M` install MIDI virtual-port driver (first run)

## App

`H` control UI · `C` clear fields · `P` pause (couples to music)
`\` reload shaders · `F11` fullscreen · `?` / `/` print help to stdout
`` ` `` record EXR · `PrtSc` screenshot · `Shift+PrtSc` hi-res supersample (2× sim)
`Ctrl+S` save preset · `Ctrl+N/P` cycle presets
`Esc` quit — arms confirm (`Y` / 2nd `Esc` = quit, `N` = cancel)

macOS keyboard aliases: `Cmd+\` screenshot · `Cmd+Opt+\` hi-res supersample

## Bootup

```
# max quality fullscreen
./feedback.exe --fullscreen --precision 32 --blur-q 2 --ca-q 2 --fields 4
# same but windowed (shortcut)
./feedback.exe --high-color
# balanced (any machine)
./feedback.exe --fullscreen --precision 16
# unattended / gallery
./feedback.exe --fullscreen --demo
# interactive picker — option 8 is "high color"
./feedback.exe
# record every action fire to a session CSV (for tweaking controller maps)
./feedback.exe --log-usage
```

## Gamepad (Xbox) — closed-help defaults

LS translate · RS X rotate, Y output fade · `A` tap tempo · `B` clear · `X` pause
`Y` help · `LB/RB` VFX-1 cycle · `D-pad U/D` preset cycle · `D-pad L/R` VFX-2
`Start` record · `Back` help · `LS click` couple · `RS click` external

Buttons remap per-help-section when the help panel is open — bottom-right tag shows active section.
