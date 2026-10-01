# NetBSD2

NetBSD2 — независимый форк [NetBSD](https://github.com/NetBSD/src) на базе
версии 11 для одноплатных компьютеров и встраиваемых устройств.
Основное направление — поддержка Raspberry Pi 5: драйверы оборудования,
исправления ядра и инструменты сборки ядра и UEFI.

На Raspberry Pi 5 проверены Wi-Fi, Ethernet, охлаждение, встроенный Bluetooth
и микрофоны WM8960 Audio HAT. В дереве также есть адаптации Raspberry Pi
Zero 2 W и диагностические изменения для Compute Module 5; степень их
готовности отличается от поддержки Pi 5.

[Описание адаптаций, сборки и ограничений](README.oxtorg.md) содержит
состояние проверок и происхождение исходников. Изменения форка не означают
их включения в официальный NetBSD.

Репозиторий содержит исходники ОС и поддержку оборудования. Прикладные
программы и настройки конкретного устройства добавляются при развёртывании.
Учётные данные и персональные образы в репозитории не публикуются.

Ниже сохранена исходная справка NetBSD. Ссылки на официальные бинарные
выпуски относятся к NetBSD и не содержат доработок этого форка.

NetBSD
======

NetBSD is a free, fast, secure, and highly portable Unix-like Open
Source operating system.  It is available for a [wide range of
platforms](https://wiki.NetBSD.org/ports/), from large-scale servers
and powerful desktop systems to handheld and embedded devices.

Building
--------

You can cross-build NetBSD from most UNIX-like operating systems.
To build for amd64 (x86_64), in the src directory:

    ./build.sh -U -u -j4 -m amd64 -O ~/obj release

Additional build information available in the [BUILDING](BUILDING) file.

Binaries
--------

- [Daily builds](https://nycdn.NetBSD.org/pub/NetBSD-daily/HEAD/latest/)
- [Releases](https://cdn.NetBSD.org/pub/NetBSD/)

Testing
-------

On a running NetBSD system:

    cd /usr/tests; atf-run | atf-report

Troubleshooting
---------------

- Send bugs and patches [via web form](https://www.NetBSD.org/cgi-bin/sendpr.cgi?gndb=netbsd).
- Subscribe to the [mailing lists](https://www.NetBSD.org/mailinglists/).
  The [netbsd-users](https://www.NetBSD.org/mailinglists/#netbsd-users) list is a good choice for many problems; watch [current-users](https://www.NetBSD.org/mailinglists/#current-users) if you follow the bleeding edge of NetBSD-current.
- Join the community IRC channel [#netbsd @ libera.chat](https://web.libera.chat/#netbsd).

Latest sources
--------------

To fetch the main CVS repository:

    cvs -d anoncvs@anoncvs.NetBSD.org:/cvsroot checkout -P src

To work in the Git mirror, which is updated every few hours from CVS:

    git clone https://github.com/NetBSD/src.git

Additional Links
----------------

- [The NetBSD Guide](https://www.NetBSD.org/docs/guide/en/)
- [NetBSD manual pages](https://man.NetBSD.org/)
- [NetBSD Cross-Reference](https://nxr.NetBSD.org/)
