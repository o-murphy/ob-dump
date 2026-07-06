// Relative import to reuse the vendored C sources shared with every other
// platform. CocoaPods' `source_files` glob can't reach outside this
// directory, so this forwarder — the same trick the official
// `flutter create --template=plugin_ffi` scaffold uses — pulls in the real
// file via the preprocessor instead. See ../ob_dump_reader_flutter.podspec.
#include "../../src/lmdb/mdb.c"
