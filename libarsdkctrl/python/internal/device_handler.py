# Copyright (c) 2020 Parrot Drones SAS

import ctypes
import logging

import libarsdk

from libarsdkctrl_bindings import (
    arsdk_device_connect,
    arsdk_device_create_cmd_itf,
    arsdk_device_disconnect,
    arsdk_device_get_info,
    struct_arsdk_cmd_itf,
    struct_arsdk_cmd_itf_cbs,
    struct_arsdk_device_conn_cbs,
    struct_arsdk_device_conn_cfg,
    struct_arsdk_device_info,
    struct_pomp_loop,
)

from .utils import string_cast

class DeviceHandler:
    def __init__(self, loop, observer,
                 connected_cb=None, disconnected_cb=None):
        self._log = logging.getLogger("Arsdk.DeviceHandler")
        # Recast pomp_loop pointer to prevent ctypes errors
        self._loop = ctypes.cast(loop, ctypes.POINTER(struct_pomp_loop))
        self._observer = observer
        self._connected_user_cb = connected_cb
        self._disconnected_user_cb = disconnected_cb

        self._device_cbs = struct_arsdk_device_conn_cbs.bind({
            "connecting": self._connecting_cb,
            "connected": self._connected_cb,
            "disconnected": self._disconnected_cb,
            "canceled": self._canceled_cb,
            "link_status": self._link_status_cb,
        })
        self._device = None
        self._cmd_itf = ctypes.POINTER(struct_arsdk_cmd_itf)()

    def add_device(self, device):
        # Log device info
        info = ctypes.POINTER(struct_arsdk_device_info)()
        res = arsdk_device_get_info(device, ctypes.byref(info))
        if res < 0:
            raise RuntimeError(f"arsdk_device_get_info: {res}")
        self._log.info("Discovered device: %s (%s:%d)",
                string_cast(info.contents.name),
                string_cast(info.contents.addr),
                info.contents.port)

        # Connect device
        cfg = struct_arsdk_device_conn_cfg(
            ctypes.create_string_buffer(b"arsdkctrl"),  # name
            ctypes.create_string_buffer(b"python"),     # type
            ctypes.create_string_buffer(b""),           # id
            ctypes.create_string_buffer(b"{}")          # json
        )
        res = arsdk_device_connect(
            device, cfg, self._device_cbs, self._loop)
        if res < 0:
            raise RuntimeError(f"arsdk_device_connect: {res}")

    def get_cmd_itf(self):
        return self._cmd_itf

    def send_command(self, command):
        if not isinstance(command, libarsdk.struct_arsdk_cmd):
            raise TypeError("DeviceHandler.send(): wrong argument type")
        # Need to recast to type from correct python binding
        cmd_itf = ctypes.cast(self._cmd_itf, ctypes.POINTER(
                libarsdk.struct_arsdk_cmd_itf))
        send_status = libarsdk.arsdk_cmd_itf_cmd_send_status_cb_t()
        res = libarsdk.arsdk_cmd_itf_send(cmd_itf, command, send_status, None)
        if res < 0:
            raise RuntimeError(f"arsdk_cmd_itf_send: {res}")

    def remove_device(self, device):
        res = arsdk_device_disconnect(device)
        if res < 0:
            raise RuntimeError(f"arsdk_device_disconnect: {res}")

    def _connecting_cb(self, device, device_info, user_data):
        pass

    def _connected_cb(self, device, device_info, user_data):
        self._device = device

        self._cmd_itf_cbs = struct_arsdk_cmd_itf_cbs.bind({
            'dispose': self._dispose_cb,
            'recv_cmd': self._recv_cmd_cb,
        })

        res = arsdk_device_create_cmd_itf(
            self._device, self._cmd_itf_cbs, ctypes.pointer(self._cmd_itf))
        if res < 0:
            raise RuntimeError(f"arsdk_device_create_cmd_itf: {res}")

        if self._connected_user_cb:
            self._connected_user_cb()

    def _disconnected_cb(self, device, device_info, user_data):
        self._device = None
        if self._disconnected_user_cb:
            self._disconnected_user_cb()

    def _canceled_cb(self, device, device_info, reason, user_data):
        self._device = None

    def _link_status_cb(self, device, device_info, status, user_data):
        pass

    def _recv_cmd_cb(self, itf, command, user_data):
        self._observer.notify(command)

    def _dispose_cb(self, itf, user_data):
        pass
