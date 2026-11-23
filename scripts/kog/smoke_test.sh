#!/usr/bin/env bash
# usage: smoke_test.sh DIRECTORY EXECUTABLE [SECONDS]
# Starts the client under Xvfb with software OpenGL, lets it run, quits it through the input fifo and checks the result.
set -uo pipefail

dir=$(cd "${1:?directory}" && pwd)
exe=${2:?executable}
seconds=${3:-25}
work=$(mktemp -d)
log="$work/client.log"

export HOME="$work/home" XDG_DATA_HOME="$work/data" SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1
mkdir -p "$HOME"

(
  sleep "$seconds"
  timeout 5 sh -c "echo quit > '$work/in'"
) &

cd "$dir" || exit 1
timeout $((seconds + 40)) xvfb-run -a -s "-screen 0 1280x720x24" "./$exe" \
  "cl_input_fifo $work/in" "cl_save_settings 0" "gfx_fullscreen 0" "stdout_output_level 1" "logfile $log" \
  > "$work/stdout.txt" 2>&1
rc=$?
wait

status=0
if [ "$rc" -ne 0 ]; then
  echo "smoke test: exit code $rc" >&2
  status=1
fi
if [ ! -s "$log" ]; then
  echo "smoke test: no log was written" >&2
  status=1
fi
if grep -a -i -E 'assert|segmentation|terminate called|aborted|addresssanitizer|unhandled exception' "$log" "$work/stdout.txt" > "$work/crash.txt"; then
  echo "smoke test: crash markers in the output:" >&2
  head -n 10 "$work/crash.txt" >&2
  status=1
fi

echo "--- client log (last 25 lines)"
tail -n 25 "$log" 2>/dev/null
mkdir -p smoke-results && cp "$log" smoke-results/client.log 2>/dev/null
cp "$work/stdout.txt" smoke-results/stdout.txt 2>/dev/null
[ "$status" -eq 0 ] && echo "smoke test: ok" || echo "smoke test: FAILED" >&2
exit $status
