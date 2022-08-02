"""Arsdk dynamic message generator.

The ArsdkMessage class will dynamically parse the generated message table
from libarsdk, and will create Python wrappers for messages on demand.

For commands with enum arguments, the values are in the message properties.
For instance: ArsdkMessage.Rth.Rth_auto_trigger.Reason.BATTERY_CRITICAL_SOON

Note: The first letter of commands is capitalized,
      and enum values are in upper case.

Syntax: ArsdkMessage.Ardrone3.Piloting.NavigateHome(1)

Copyright (c) 2020 Parrot Drones SAS
"""

from .internal.messages import ArsdkMessageFactory


# Metaclass used for calling attributes on a type (via singleton instance)
class ArsdkMessageMeta(type):
    def __getattr__(self, key):
        return ArsdkMessage.get().__getattr__(key)


class ArsdkMessage(ArsdkMessageFactory, metaclass=ArsdkMessageMeta):
    instance = None

    @staticmethod
    def get():
        if not ArsdkMessage.instance:
            ArsdkMessage.instance = ArsdkMessage()
        return ArsdkMessage.instance
