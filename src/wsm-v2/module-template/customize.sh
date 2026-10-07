#!/system/bin/sh
[ "$BOOTMODE" = true ] || abort "Install through a module manager in booted Android."
case "${API:-0}" in *[!0-9]*|'') abort "Invalid Android API." ;; esac
[ "${API:-0}" -ge 26 ] || abort "WSM requires Android 8 / API 26 or newer."
if [ -n "$MAGISK_VER_CODE" ]; then
  case "$MAGISK_VER_CODE" in *[!0-9]*) abort "Invalid Magisk version." ;; esac
  [ "$MAGISK_VER_CODE" -ge 26000 ] || abort "Zygisk API v4 requires Magisk 26+."
fi
case "$ARCH" in
  x64|x86_64) ABI=x86_64 ;;
  arm64|arm64-v8a) ABI=arm64-v8a ;;
  *) abort "Unsupported ABI: $ARCH. WSM ships x86_64 and arm64-v8a." ;;
esac
for path in "zygisk/$ABI.so" "engine/$ABI.so" payload/arm64-v8a.so dex/wsm_menu.dex release.json verify.list; do
  [ -s "$MODPATH/$path" ] || abort "Incomplete module: $path"
done
command -v sha256sum >/dev/null 2>&1 || abort "sha256sum required for integrity verification."
(cd "$MODPATH" && sed '/  META-INF\//d' verify.list | sha256sum -c - >/dev/null 2>&1) || abort "Module integrity check failed."
set_perm_recursive "$MODPATH" 0 0 0755 0644 || abort "Failed setting module permissions."
ui_print "- WSM v6.1.0-rc2 / $ABI / integrity PASS"
ui_print "- Enable a compatible Zygisk provider and reboot once."
ui_print "- Target: com.kakaogames.gdts, version 3.54.0 / code 423 only."
ui_print "- RC2: complete 47-feature gameplay qualification remains pending."
ui_print "- Profiles are manual; controls begin OFF."
ui_print "- Disable other GT injection modules before testing WSM."
ui_print "- Runtime diagnostics: WSM / WSMEngine / WSM-H64."
