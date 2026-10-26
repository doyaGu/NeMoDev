#!/bin/sh
set -eu

# Resolve the launcher itself, including user-created symlinks. Keep the
# caller's working directory so relative composition paths retain their meaning.
launcher=$(readlink -f -- "$0")
directory=$(CDPATH= cd -- "$(dirname -- "$launcher")" && pwd)
exec "$directory/Bin/NeMoDev" "$@"
