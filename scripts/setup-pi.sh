#!/usr/bin/env bash
# One-time Pi setup so the supervisor can own the hardware watchdog.
#
# Raspberry Pi OS ships /lib/systemd/system.conf.d/40-rpi-enable-watchdog.conf
# with RuntimeWatchdogSec=1m, so systemd (PID 1) opens /dev/watchdog0 and kicks
# it itself. This drop-in overrides that to 0 and releases the device, letting
# the supervisor open and kick it directly. daemon-reexec applies it without a
# reboot; the drop-in persists across reboots.
#
# Alternative (not used here): keep systemd owning the hardware watchdog and run
# the supervisor as a systemd service with WatchdogSec + sd_notify(WATCHDOG=1),
# layering app -> systemd -> hardware. We take the device directly so the
# "freeze the supervisor -> the board reboots" behaviour is attributable to our
# own code and mirrors a bare-metal MCU watchdog.
set -eu

sudo mkdir -p /etc/systemd/system.conf.d
printf '[Manager]\nRuntimeWatchdogSec=0\n' \
  | sudo tee /etc/systemd/system.conf.d/50-disable-runtime-watchdog.conf >/dev/null
sudo systemctl daemon-reexec
sleep 1

if cat /sys/class/watchdog/watchdog0/state 2>/dev/null | grep -q inactive; then
  echo "OK: systemd released /dev/watchdog0; the supervisor can own it (run as root)."
else
  echo "NOTE: /dev/watchdog0 still active; check for other holders."
fi
