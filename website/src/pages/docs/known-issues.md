---
title: Known Issues
---

# Known Issues

## AppScope 0.6

2021-04-01 - Maintenance Pre-Release

As of this AppScope pre-release, known issues include:

- Java JRE < v.6 is not supported.

- [#38](https://github.com/criblio/appscope/issues/35) Go < v.1.8 is not supported.

- [#38](https://github.com/criblio/appscope/issues/35) Go executables with stripped symbols do not allow function interposition. Executables will run, but will not provide data to AppScope.

- [#143](https://github.com/criblio/appscope/issues/143) Linux distros that use uClibc or MUSL instead of glibc, such as Alpine, are not supported

- [#10](https://github.com/criblio/appscope/issues/10), [#165,](https://github.com/criblio/appscope/issues/165) Scoping the Docker executable is problematic.

- [#119](https://github.com/criblio/appscope/issues/119) HTTP 2 metrics and headers can be viewed only with [Cribl LogStream](https://cribl.io/product/).

<hr>

See [all issues](https://github.com/criblio/appscope/issues) currently open on GitHub.