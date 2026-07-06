#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint ob_dump_reader_flutter.podspec` to validate before publishing.
#
# NOTE: written by pattern, matching the official
# `flutter create --template=plugin_ffi` scaffold exactly (confirmed by
# generating that scaffold and diffing against it) — not built on a real
# macOS/Xcode machine, unlike linux/ and android/ (both empirically built
# and run this session).
Pod::Spec.new do |s|
  s.name             = 'ob_dump_reader_flutter'
  s.version          = '0.2.0'
  s.summary          = 'Flutter-ready ObjectBox LMDB reader toolkit.'
  s.description      = <<-DESC
Bundles a purpose-built, vendored-LMDB dart:ffi binding as a real Flutter
plugin so the compiled native library ships inside the app.
                       DESC
  s.homepage         = 'https://github.com/o-murphy/ob-dump'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'o-murphy' => 'thehelixpg@gmail.com' }

  # This will ensure the source files in Classes/ are included in the native
  # builds of apps using this plugin. Podspec does not support relative
  # paths outside this directory, so Classes/ contains forwarder C files
  # that relatively #include the real vendored sources at ../src/lmdb/, so
  # they're shared among every target platform rather than duplicated here.
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'Flutter'
  s.platform = :ios, '12.0'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
end
