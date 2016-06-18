# Journal Combined View

Follow systemd journal from multiple machines.

Features:
* Message priority is indicated with color
* Errors (priority <= "err") are prevented from scrolling out too quickly
* Continue reading from the last read message when the connections fails or this tool is restarted

# How To Use

On each machine you want to monitor enable the gatewayd service:

  systemctl start systemd-journal-gatewayd

The journal is now accessible at `http://<host>:19531/`. Note that this makes your journal accessible to the whole network without authentication!

Start the journal aggregator:

  journal-comvi <host>

The last retrieved position of each host is stored in the current directory by default (see `-c` option).

## References

* [Journal Export Format](https://www.freedesktop.org/wiki/Software/systemd/export/)
* [HTTP API](https://www.freedesktop.org/software/systemd/man/systemd-journal-gatewayd.html)
* [sd-journal manual](http://0pointer.de/public/systemd-man/sd-journal.html)
