# Copyright (c) 2020 Parrot Drones SAS

import ctypes

from libarsdkctrl_bindings import (
    arsdk_ctrl_destroy,
    arsdk_ctrl_new,
    arsdk_ctrl_set_device_cbs,
    struct_arsdk_ctrl,
    struct_arsdk_ctrl_device_cbs,
    struct_pomp_loop,
)


class ArsdkCtrl:
    def __init__(self, loop, device_handler):
        self._loop = ctypes.cast(loop, ctypes.POINTER(struct_pomp_loop))
        self._device_handler = device_handler
        self._arsdk_ctrl = None

        # Create arsdk_ctrl
        self._arsdk_ctrl = ctypes.POINTER(struct_arsdk_ctrl)()
        res = arsdk_ctrl_new(self._loop, ctypes.byref(self._arsdk_ctrl))
        if res < 0:
            raise RuntimeError(f"arsdk_ctrl_new: {res}")

        # Register callbacks
        self._device_cbs = struct_arsdk_ctrl_device_cbs.bind({
            'added': self._device_added_cb,
            'removed': self._device_removed_cb
        })
        res = arsdk_ctrl_set_device_cbs(self._arsdk_ctrl, self._device_cbs)
        if res < 0:
            raise RuntimeError(f"arsdk_ctrl_set_device_cbs: {res}")

    def __del__(self):
        self.cleanup()

    def cleanup(self):
        if self._arsdk_ctrl:
            res = arsdk_ctrl_destroy(self._arsdk_ctrl)
            if res < 0:
                raise RuntimeError(f"arsdk_ctrl_destroy: {res}")
            self._arsdk_ctrl = None

        self._loop = None
        self._device_handler = None

    def _device_added_cb(self, arsdk_device, user_data):
        self._device_handler.add_device(arsdk_device)

    def _device_removed_cb(self, arsdk_device, user_data):
        self._device_handler.remove_device(arsdk_device)

    def get_ptr(self):
        return self._arsdk_ctrl
