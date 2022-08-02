# Copyright (c) 2020 Parrot Drones SAS

import ctypes

from libarsdk import (
    arsdk_device_type,
    arsdk_device_type__enumvalues,
)

from libarsdkctrl_bindings import (
    arsdk_discovery_net_destroy,
    arsdk_discovery_net_new_with_port,
    arsdk_discovery_net_start,
    arsdk_discovery_net_stop,
    struct_arsdk_discovery_cfg,
    struct_arsdk_discovery_net,
)

class DiscoveryNet:
    def __init__(self, arsdk_ctrl, backend_net, ip_addr, port):
        assert type(ip_addr) is str or type(ip_addr) is bytes
        if type(ip_addr) is str:
            self._ip_addr = ip_addr.encode('utf-8')
        self._port = port

        self._discovery_net = ctypes.POINTER(struct_arsdk_discovery_net)()

        cfg = struct_arsdk_discovery_cfg()

        # Array of device types we are interrested in, use them all
        device_types = list(arsdk_device_type__enumvalues.keys())
        cfg.types = (arsdk_device_type * len(device_types))(*device_types)
        cfg.count = len(device_types)

        res = arsdk_discovery_net_new_with_port(
                arsdk_ctrl.get_ptr(),
                backend_net.get_ptr(),
                ctypes.pointer(cfg),
                self._ip_addr,
                self._port,
                ctypes.byref(self._discovery_net)
        )
        if res < 0:
            raise RuntimeError(f"arsdk_discovery_net_new_with_port: {res}")

    def __del__(self):
        self.cleanup()

    def cleanup(self):
        if self._discovery_net:
            res = arsdk_discovery_net_destroy(self._discovery_net)
            if res < 0:
                raise RuntimeError(f"arsdk_discovery_net_destroy: {res}")
            self._discovery_net = None

    def start(self):
        res = arsdk_discovery_net_start(self._discovery_net)
        if res < 0:
            raise RuntimeError(f"arsdk_discovery_net_start: {res}")

    def stop(self):
        res = arsdk_discovery_net_stop(self._discovery_net)
        if res < 0:
            raise RuntimeError(f"arsdk_discovery_net_stop: {res}")
