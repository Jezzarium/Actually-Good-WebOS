# On the TV, for vis.sh: launches an app and captures the screen back to back
# for 1.6 s, recording when each capture started and finished.
# Usage: python3 vis.py <app id> <params json> <outdir>
import json, os, subprocess, sys, time
app, params, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
t0 = time.time()
subprocess.Popen(['luna-send', '-n', '1', 'luna://com.webos.applicationManager/launch',
                  json.dumps({'id': app, 'params': json.loads(params)})], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
times = []
i = 0
while time.time() - t0 < 1.6:
    path = '%s/%03d.jpg' % (out, i)
    s = time.time()
    subprocess.run(['luna-send', '-n', '1', 'luna://com.webos.surfacemanager/captureCompositorOutput',
                    json.dumps({'output': path, 'format': 'JPG'})], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    times.append((i, round((s - t0) * 1000), round((time.time() - t0) * 1000)))
    i += 1
json.dump(times, open(out + '/times.json', 'w'))
