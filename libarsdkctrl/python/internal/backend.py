# Copyright (c) 2020 Parrot Drones SAS

import ctypes

from libarsdkctrl_bindings import (
    arsdkctrl_backend_net_destroy,
    arsdkctrl_backend_net_new,
    struct_arsdkctrl_backend_net,
    struct_arsdkctrl_backend_net_cfg,
)


class BackendNet:
    def __init__(self, arsdk_ctrl):
        self._backend_net = ctypes.POINTER(struct_arsdkctrl_backend_net)()

        cfg = struct_arsdkctrl_backend_net_cfg()
        res = arsdkctrl_backend_net_new(arsdk_ctrl.get_ptr(),
                ctypes.pointer(cfg),
                ctypes.byref(self._backend_net))
        if res < 0:
            raise RuntimeError(f"arsdkctrl_backend_net_new: {res}")

    def __del__(self):
        self.cleanup()

    def cleanup(self):
        if self._backend_net is not None:
            res = arsdkctrl_backend_net_destroy(self._backend_net)
            if res < 0:
                raise RuntimeError(f"arsdkctrl_backend_net_destroy: {res}")
            self._backend_net = None

    def get_ptr(self):
        return self._backend_net
