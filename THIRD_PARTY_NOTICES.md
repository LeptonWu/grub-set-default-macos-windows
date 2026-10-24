# Third-party components

The project's own source is under the MIT license in LICENSE. Distributed
executables also contain libraries with their own terms:

* e2fsprogs libext2fs: GNU Library General Public License version 2.
* e2fsprogs libcom_err: MIT-style license.
* e2fsprogs libuuid: BSD-style license.
* Windows builds: MinGW-w64 and GCC runtime components, including their
  applicable runtime exceptions and permissive licenses.

Packages include upstream notices under licenses/ and complete application
and e2fsprogs source archives under source/. Extract both source archives into
the same directory. From the extracted grub-set-default-macos-windows directory,
run build-windows.sh on Linux with MinGW-w64 installed, or build-macos.sh on
macOS with Apple Command Line Tools installed. The scripts build the libraries
and link the application, allowing rebuilding with modified library sources.
Keep these notices and sources with redistributed packages.

The binaries are not Apple-notarized or Windows Authenticode-signed. CI tests
use disposable image files; physical-disk discovery and actual boot behavior
require testing on the target machine.
