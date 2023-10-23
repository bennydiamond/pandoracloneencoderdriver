#!/bin/sh

fw_setenv initargs rootfstype=ramfs init=/init ramoops.pstore_en=1 ramoops.record_size=0x8000 ramoops.console_size=0x4000 use_cma_first=1 androidboot.selinux=disabled usbhid.quirks=0x0f0d:0x0001:0x040

cp pandora.service /storage/.config/system.d/pandora.service
systemctl enable pandora.service

chmod +x inputattach-pandora
