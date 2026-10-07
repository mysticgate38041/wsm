#!/system/bin/sh

[ "$BOOTMODE" = true ] || abort "Install from a module manager while Android is booted."
[ "${API:-0}" -ge 23 ] || abort "Android API 23 or newer is required."

if [ -n "$MAGISK_VER_CODE" ]; then
  case "$MAGISK_VER_CODE" in
    *[!0-9]*) abort "Invalid Magisk version code." ;;
  esac
  [ "$MAGISK_VER_CODE" -ge 26000 ] || abort "Zygisk API v4 requires Magisk 26.0 or newer."
else
  ui_print "External root manager — assuming Zygisk API v4 provider."
fi

case "$ARCH" in
  x86) ABI=x86 ;;
  x64|x86_64) ABI=x86_64 ;;
  arm) ABI=armeabi-v7a ;;
  arm64) ABI=arm64-v8a ;;
  *) abort "Unsupported manager architecture: $ARCH" ;;
esac
[ -f "$MODPATH/zygisk/$ABI.so" ] || abort "Native loader missing for $ABI."
[ -f "$MODPATH/engine/arm64-v8a.so" ] || abort "ARM64 engine missing."

set_perm_recursive "$MODPATH" 0 0 0755 0644
ui_print "WSM v0.1.0-f0 POC installed. Primary ABI: $ABI"
ui_print "Target process: com.kakaogames.gdts"
ui_print "Watch logcat: WSM (loader) and WSMEngine (arm64 world)."
