#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -d '/Applications/Android Studio.app/Contents/jbr/Contents/Home' ]]; then
  export JAVA_HOME='/Applications/Android Studio.app/Contents/jbr/Contents/Home'
fi
if [[ -z "${ANDROID_HOME:-}" && -d "$HOME/Library/Android/sdk" ]]; then
  export ANDROID_HOME="$HOME/Library/Android/sdk"
fi
"$project_dir/android-demo/gradlew" -p "$project_dir/android-demo" :app:assembleDebug :app:check :app:lintDebug "$@"
mkdir -p "$project_dir/dist/android"
cp "$project_dir/android-demo/app/build/outputs/apk/debug/app-debug.apk" "$project_dir/dist/android/DeepMirror-Android-USB-Demo.apk"
cp "$project_dir/android-demo/README.md" "$project_dir/dist/android/README.md"
printf 'APK: %s\n' "$project_dir/dist/android/DeepMirror-Android-USB-Demo.apk"
