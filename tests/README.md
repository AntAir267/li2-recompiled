# Tests

They need the game (`work/game`, or where `tools/setup.sh` linked it) and write to `work/`, which is
not in the repository. All run off-screen in test mode (`--test`: a virtual clock and scripted
input), which does not play movies.

- `make build/fncheck`: every function against the original code in an emulator. It needs Unicorn
  (`.venv/bin/pip install unicorn`).
- `tests/hashcheck.sh`: run hashes of fixed scenes against recorded baselines. Baselines are not in
  the repository: run `tests/hashcheck.sh record` first, on a build you trust. Record again after
  a change that alters the game's memory on purpose (a new menu row does).
- `tests/render_compare.sh`, `tests/gpuxf_compare.sh`: two renderer settings against each other,
  frames and game state (`tests/imgdiff.py` compares frames; it needs NumPy and Pillow).
- `tests/zone_compare.sh`: both renderers in each zone. `tests/dialogue_probe.sh`: which inputs end
  a line of dialogue. `tests/menu_camera.sh`, `tests/prompts_shots.sh`: menu and prompt screenshots.
- `tests/perf_town.sh`, `tests/perf_byfn.sh`: the game thread's CPU time and a profile.

`tests/zone_compare.sh` and the skateboard part of `tests/prompts_shots.sh` load a save: put one of
your own, with Pepper in the town and the skateboard unlocked, in `work/testuser/_data/` (with the
`SaveDef.lgo`, `Settings.set` and `sw.dat` beside it). `work/testuser/gm.dls` is used if it is there.
