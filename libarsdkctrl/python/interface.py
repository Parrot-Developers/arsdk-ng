# Copyright (c) 2020 Parrot Drones SAS

from msghub_utils import get_pbuf_msg_num, service_id, service_name
from .messages import ArsdkMessage
from .internal.arsdkctrl import ArsdkCtrl
from .internal.backend import BackendNet
from .internal.device_handler import DeviceHandler
from .internal.discovery import DiscoveryNet
from .internal.observer import ArsdkObserver

from arsdk import camera2_pb2 as camera2_msgs
from google.protobuf import empty_pb2 as pbuf_empty
from google.protobuf.message import Message

class ArsdkItf:
    """Wrapper for a Arsdk client, with a net discovery and net backend.
    """

    def __init__(self, pomp_loop,
                 connected_cb=None,
                 disconnected_cb=None,
                 addr='127.0.0.1',
                 port=44447):
        self._observer = None
        self._device_handler = None
        self._arsdk_ctrl = None
        self._backend_net = None
        self._discovery_net = None

        self._observer = ArsdkObserver()
        self._device_handler = DeviceHandler(pomp_loop, self._observer,
                connected_cb, disconnected_cb)
        self._arsdk_ctrl = ArsdkCtrl(pomp_loop, self._device_handler)
        self._backend_net = BackendNet(self._arsdk_ctrl)
        self._discovery_net = DiscoveryNet(self._arsdk_ctrl, self._backend_net,
                addr, port)

    def __del__(self):
        self.cleanup()

    def cleanup(self):
        if self._discovery_net:
            self._discovery_net.cleanup()
            self._discovery_net = None

        if self._backend_net:
            self._backend_net.cleanup()
            self._backend_net = None

        if self._arsdk_ctrl:
            self._arsdk_ctrl.cleanup()
            self._arsdk_ctrl = None

        self._device_handler = None
        self._observer

    def start(self):
        self._discovery_net.start()

    def stop(self):
        self._discovery_net.stop()

    def send(self, msg):
        """Send an Arsdk command instance.

        Example: itf.send(ArsdkMessage.Rth.Navigatehome(1))
        """
        self._device_handler.send_command(msg)

    def send_custom_cmd(self, pbuf_msg):
        """Send a custom protobuf command instance.

        Example: itf.send_custom_cmd(
            pbuf_msg=camera2_pb2.Command(
                start_photo=camera2_pb2.Command.StartPhoto(
                    camera_id=0
                )
            )
        )
        """
        if isinstance(pbuf_msg, Message):
            self.send(
                ArsdkMessage.Generic.Custom_cmd(
                    service_id(service_name(pbuf_msg)),
                    get_pbuf_msg_num(pbuf_msg),
                    pbuf_msg.SerializeToString()
                )
            )
        else:
            raise TypeError("CustomCmd: wrong argument type, only protobuf type is accepted")

    def register(self, bindings):
        """Register callbacks on specified events.
        Accepted binding formats:
            (event, callback)
            {event: callback, ...}
            [(event, callback), ...]
            [({event, ...}, callback), ...]

        Callbacks take one argument, which is a message object containing its
        fields as properties. The names are the same as in ARSDK XML files.

        Note: The first letter of argument names is capitalized

        Example: Ardrone3.MediaRecordState.VideoStateChanged will return
        an object 'msg' with the following fields:
         - msg.State (IntEnum)
         - msg.Mass_storage_id (int)
        """
        return self._observer.register(bindings)

    def camera2_selected_fields(self, descriptor, keys):
        """Create a dict suitable for the selected_fields field in camera2 pbuf messages.

        For each field in 'keys', associate its protobuf index, as
        given by 'descriptor', to an empty message.

        """
        _EMPTY_MSG = pbuf_empty.Empty()
        return {
            descriptor.fields_by_name[key].number: _EMPTY_MSG
            for key in keys
        }
