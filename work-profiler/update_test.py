"""In-place update test (runtime/dread_ui/fe_update.cpp): serves a release folder made by make_release.py on
127.0.0.1 and starts an installed game's EXE with the front end's test hooks, which press UPDATE / REBUILD / RESTART.

  update_test.py INSTALL_DIR SERVED_RELEASE_DIR RESULT_FILE [seconds]

INSTALL_DIR is a test install (never the user's games folder). Prints the result lines and the installed version."""
import functools, http.server, os, subprocess, sys, threading, time

inst, served, result = os.path.normpath(sys.argv[1]), sys.argv[2], sys.argv[3]
secs = int(sys.argv[4]) if len(sys.argv) > 4 else 120
if 'games' in os.path.normpath(inst).lower().split(os.sep):
    raise SystemExit('refusing to test on a games folder')
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=served)
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), h)
port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
env = dict(os.environ, SWR_FE_TEST='1', SWR_FE_AUTOUPDATE='1', SWR_FE_RESULT=result, SWR_FE_EXIT_FRAME='2000000',
           SWR_UPDATE_URL='http://127.0.0.1:%d' % port)
if os.path.exists(result):
    os.remove(result)
exe = os.path.join(inst, 'Metroid Dread.exe')
t0 = time.time()
subprocess.Popen([exe], cwd=inst, env=env)
# the first process may restart into the new EXE: wait until no game process from this folder is left
def running():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "(Get-Process 'Metroid Dread' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '%s*' }).Count" % inst.replace("'", "''")],
                         capture_output=True, text=True).stdout.strip()
    return int(out or '0')
time.sleep(3)
while time.time() - t0 < secs and running():
    time.sleep(1)
left = running()
srv.shutdown()
lines = open(result).read().splitlines() if os.path.exists(result) else []
seen = []
for l in lines:   # one line per change of update state
    u = [p for p in l.split() if p.startswith('update=') or p.startswith('setup=')]
    if u and (not seen or u != seen[-1]):
        print('%-60s %s' % (' '.join(u), l.split('error=', 1)[-1][:120]))
        seen.append(u)
print('processes left: %d, %.0f s' % (left, time.time() - t0))
print('installed:', [l for l in open(os.path.join(inst, 'update.txt')).read().splitlines() if l.startswith('version')])
