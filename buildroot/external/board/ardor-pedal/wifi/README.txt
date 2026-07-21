Wi-Fi provisioning
==================

To enable Wi-Fi, create /opt/ardor-pedal/wifi/wpa_supplicant.conf on the
writable data partition, then restart the pedal (or run
/etc/init.d/S42wifi start as root).

Example WPA2/WPA3-Personal configuration:

    ctrl_interface=/run/wpa_supplicant
    update_config=0
    country=HU

    network={
        ssid="replace-with-network-name"
        psk="replace-with-network-password"
    }

Keep this file owner-readable only:

    chmod 600 /opt/ardor-pedal/wifi/wpa_supplicant.conf

The file is deliberately not included in the image or Git repository. Use
Ethernet SSH or the serial console for initial provisioning.
