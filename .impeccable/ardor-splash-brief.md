# Ardor boot splash

- Mode: Operate; passive boot feedback on the 1280 × 720 pedal display.
- Audience/job: reassure a player at stage distance that the rig is starting while hiding Linux boot output.
- Approved direction: Signal Wake (`.impeccable/mocks/ardor-splash-signal-wake.png`).
- Composition: graphite field; compact centered red lamp and ARDOR title; lower-third calibration rail; quiet `INITIALISING AUDIO ENGINE` caption.
- Motion: one red rail segment traverses and reverses; no percentage or invented progress.
- Grammar: flat RGB color, square geometry, crisp rules, condensed block lettering, no depth effects.
- Handoff: retain the final framebuffer contents; LVGL replaces them with its first frame.
- Implementation media: all geometry, text, and motion are deterministic native framebuffer code.
