#!/bin/sh
# Copy the complete package locally first; MTP/FAT may strip execute bits.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$(uname -m)" in
  x86_64) binary="$here/detector-clock-linux-x86_64" ;;
  *) echo 'This package contains an x86_64 Linux binary; build detector_clock.c for your architecture.' >&2; exit 2 ;;
esac
[ -f "$binary" ] || { echo 'Copy the complete clock package to a local directory first.' >&2; exit 2; }
chmod u+x "$binary" || { echo 'Use a writable local directory, outside an MTP mount.' >&2; exit 2; }
exec "$binary" "$@"
