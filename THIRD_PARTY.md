# Dependencies and corresponding sources

mnTransfer code: GPL-3.0-or-later, see LICENSE. C++ and Qt provide the desktop and
network code; libsodium supplies the cryptographic primitives. No Python or Java
runtime is required by the client. GitHub's Qt installation action may use Python
as a build tool; this is not an application dependency.

Packaged official Qt builds use 6.8.3, dynamically linked. Qt Core, GUI, Widgets,
Network, DBus and WebSockets are available under LGPL-3.0 / GPL-3.0 and their
upstream notices. Their unmodified corresponding source, including license files:

https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz

Users may replace the supplied dynamic Qt libraries with compatible builds.
The complete mnTransfer source and CMake build files accompany every release.

libsodium uses the ISC license. Its version is recorded in BUILD_INFO.txt.
Source releases and license:

- https://download.libsodium.org/libsodium/releases/
- https://github.com/jedisct1/libsodium/blob/master/LICENSE

Windows uses the vcpkg static library; Linux/macOS use their platform library.
Qt's system platform plugins also depend on operating-system libraries. Linux
archives require system GL/X11/font libraries; macOS/Windows use their respective
system APIs. Platform runtime redistribution remains subject to upstream licenses.

Cloudflare deployment uses Wrangler (Apache-2.0/MIT dependencies), pinned by
relay/package-lock.json. The relay's JavaScript source is required by the Workers
platform. It is not shipped as a Python/Java client or an alternative C++ runtime.
