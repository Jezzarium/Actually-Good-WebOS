# Test tools

`key.sh` presses remote keys on the TV over ssh and can capture the screen
afterwards, so the home screen can be tested from this machine:

```sh
tools/key.sh UP ENTER                 # open the Inputs menu (drawn by the Control Center)
tools/key.sh -c /tmp/cap.png DOWN     # move into the grid, then save a screenshot
```

`record.sh` records the screen to a GIF (about 10 fps; needs `ffmpeg` here),
either pressing keys for you or for a set time while you use the remote:

```sh
tools/record.sh RIGHT RIGHT DOWN 1.5 UP     # a number pauses that many seconds
tools/record.sh -t 10 -o demo.gif           # 10 s of you driving the remote
```

`install.sh` installs the newest package in `build/` and waits until it is installed.
