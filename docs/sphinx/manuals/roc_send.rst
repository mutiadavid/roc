roc-send
********

SYNOPSIS
========

**roc-send** *OPTIONS*

DESCRIPTION
===========

Send real-time audio stream from a file or an audio device to a remote receiver.

Options
-------

-v, --verbose                 Increase verbosity level (may be used multiple times)
-i, --input=NAME              Input file or device
-t, --type=TYPE               Input codec or driver
-s, --source=ADDRESS          Remote source UDP address
-r, --repair=ADDRESS          Remote repair UDP address
-l, --local=ADDRESS           Local UDP address
--fec=ENUM                    FEC scheme  (possible values="rs", "ldpc", "none" default=`rs')
--nbsrc=INT                   Number of source packets in FEC block
--nbrpr=INT                   Number of repair packets in FEC block
--rate=INT                    Sample rate, Hz
--no-resampling               Disable resampling  (default=off)
--resampler-profile=ENUM      Resampler profile  (possible values="low", "medium", "high" default=`medium')
--resampler-interp=INT        Resampler sinc table precision
--resampler-window=INT        Number of samples per resampler window
--interleaving                Enable packet interleaving  (default=off)
--poisoning                   Enable uninitialized memory poisoning (default=off)

Address
-------

*ADDRESS* should be in one of the following forms:

- :PORT (e.g. ":10001")
- IPv4:PORT (e.g. "127.0.0.1:10001")
- [IPv6]:PORT (e.g. "[::1]:10001")

Input
-----

Arguments for ``--input`` and ``--type`` options are passed to SoX:

- *NAME* specifies file or device name
- *TYPE* specifies file or device type

EXAMPLES
========

Send WAV file:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 -i ./file.wav

Send WAV from stdin:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 -t wav -i - < ./file.wav

Capture sound from the default driver and device:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002

Capture sound from the default ALSA device:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 -t alsa

Capture sound from a specific PulseAudio device:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 -t pulseaudio -i <device>

Bind outgoing sender port to a specific inteface:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 -l 192.168.0.2 -i ./file.wav

Select the LDPC-Staircase FEC scheme and a larger block size:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 -i ./file.wav --fec=ldpc --nbsrc=1000 --nbrpr=500

Disable FEC:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -i ./file.wav --fec=none

Force a specific input rate to be requested from on the audio device:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 --rate=44100

Select resampler profile:

.. code::

    $ roc-send -vv -s 192.168.0.3:10001 -r 192.168.0.3:10002 --resampler-profile=high

SEE ALSO
========

:manpage:`roc-recv(1)`, :manpage:`roc-conv(1)`, :manpage:`sox(1)`, the Roc web site at https://roc-project.github.io/

BUGS
====

Please report any bugs found via GitHub issues (https://github.com/roc-project/roc/).

AUTHORS
=======

See the AUTHORS file for a list of maintainers and contributors.
