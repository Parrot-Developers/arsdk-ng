#!/usr/bin/env python3

""" libarsdkctrl-local python sample code: takeoff and land

Send a takeoff command to the drone, wait until it's hovering then land.

Copyright (c) 2020 Parrot Drones SAS
"""

import signal
import libpomp
from libarsdkctrl import ArsdkItf, ArsdkMessage


PilotingState = ArsdkMessage.Ardrone3.PilotingState
Piloting = ArsdkMessage.Ardrone3.Piloting

class LocalController:
    def __init__(self):
        self._running = True
        self._loop = libpomp.pomp_loop_new()
        if self._loop is None:
            raise RuntimeError("Failed to create pomp loop")

        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

        self._arsdk_itf = ArsdkItf(
            self._loop,
            connected_cb=self.on_connected,
            addr="127.0.0.1",
            port=44447)

        # Register callback on ARSDK events
        bindings = {
            PilotingState.FlyingStateChanged: self.on_flying_state_changed
        }
        self.observer = self._arsdk_itf.register(bindings)

    def __del__(self):
        # Cleanup callbacks
        self.observer.unobserve()

    def on_flying_state_changed(self, msg):
        print("Flying state changed:", msg.State)
        if msg.State == msg.State.HOVERING:
            self.land()
        elif msg.State == msg.State.LANDED:
            print("Drone landed, exiting.")
            self._running = False

    def on_connected(self):
        # Takeoff once controller is connected
        self.takeoff()

    def takeoff(self):
        self._arsdk_itf.send(Piloting.TakeOff())

    def land(self):
        self._arsdk_itf.send(Piloting.Landing())

    def run(self):
        self._arsdk_itf.start()
        while self._running:
            libpomp.pomp_loop_wait_and_process(self._loop, 250)
        self._arsdk_itf.stop()

    def _signal_handler(self, signum, frame):
        self._running = False


if __name__ == "__main__":
    controller = LocalController()
    controller.run()
