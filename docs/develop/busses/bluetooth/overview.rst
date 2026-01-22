Bluetooth overview
==================

(Copied mostly from
http://urnenfeld.blogspot.de/2012/07/in-past-i-got-to-know-that-motivation.html)

**L2cap under ``network/protocols/l2cap``**: Provides socket interface
to have l2cap channels. L2CAP offers connection oriented and
connectionless sockets. But bluetooth stack as this point has no
interchangeability with TCP/IP, A Higher level Bluetooth profile must be
implemented

**Bluetooth kernel modules under ``src/add-ons/kernel/drivers/bluetooth``**:
Contains three components:

- **hci/**: HCI protocol handling and bluetooth device management
- **core/**: Global bluetooth data structures (connection handles, L2CAP channels)
- **transport/usb/**: USB transport driver (h2generic), implementing the H2 transport

**Bluetooth kit under ``src/kits/bluetooth``**: C++ implementation based
on JSR82 api.

**Bluetooth Server under ``src/servers/bluetooth``**: Basically handling
opened devices (locally connected in our system) and forwarding
kit calls to them.

**Bluetooth Preferences under ``src/preferences/bluetooth``**:
Configuration using the kit.

**Test applications under ``src/tests/kits/bluetooth``**.
