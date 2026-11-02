# Test tools

`key.sh` presses remote keys on the TV over ssh and can capture the screen
afterwards, so the home screen can be tested from this machine:

```sh
tools/key.sh UP ENTER                 # open the Inputs panel
tools/key.sh -c /tmp/cap.png DOWN     # move into the grid, then save a screenshot
```
