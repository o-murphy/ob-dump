# ob_dump_reader example

A small Flutter app that writes an ObjectBox-shaped LMDB store and reads it back
with `ob_dump_reader`, i.e. it exercises the native LMDB library the package's
build hook compiles. It is what CI builds and runs on every platform
(`scripts/ci/flutter-smoke.sh`), and builds as an **offline Flatpak** with
[flutpak](https://github.com/o-murphy/flutpak) to prove the build hook works
inside the Flatpak sandbox.

```bash
cd dart/example
flutter run -d linux
OB_DUMP_EXAMPLE_SELFTEST=1 flutter run -d linux   # headless check, exits 0/1
```
