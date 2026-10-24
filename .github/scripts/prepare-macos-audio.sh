#!/usr/bin/env bash
# Provision only the disposable GitHub-hosted macOS worker. Never use this on a
# musician's Mac: lifecycle checks select this device explicitly, not by default.
set -euo pipefail
if [[ "${GITHUB_ACTIONS:-}" != true || "${RUNNER_ENVIRONMENT:-}" != github-hosted || "${RUNNER_OS:-}" != macOS ]]; then
  echo 'Virtual audio provisioning is restricted to disposable GitHub-hosted macOS workers.' >&2
  exit 2
fi
: "${RUNNER_TEMP:?GitHub runner temporary directory is required}"

# Pinned official binary, reviewed against Homebrew's blackhole-2ch cask.
# https://github.com/Homebrew/homebrew-cask/blob/master/Casks/b/blackhole-2ch.rb
# https://github.com/ExistentialAudio/BlackHole#installation-instructions
version=0.7.1
checksum=57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf
package="$RUNNER_TEMP/ScreamSeq-BlackHole2ch-$version.pkg"
curl --fail --location --retry 2 --max-time 120 \
  "https://existential.audio/downloads/BlackHole2ch-$version.pkg" --output "$package"
printf '%s  %s\n' "$checksum" "$package" | shasum -a 256 --check
pkgutil --check-signature "$package"
sudo -n installer -pkg "$package" -target /
# The runner is isolated and no app is running at this step. This is needed for
# Core Audio to enumerate a newly installed HAL plugin without a machine reboot.
if pgrep -x coreaudiod >/dev/null; then sudo -n killall coreaudiod; fi
system_profiler SPAudioDataType -json
