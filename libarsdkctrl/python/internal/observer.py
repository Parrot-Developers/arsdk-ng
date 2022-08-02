# Copyright (c) 2020 Parrot Drones SAS

import logging


class ArsdkObserverBinding:
    def __init__(self, notifier, bindings):
        self._notifier = notifier
        self.bindings = bindings

    def __del__(self):
        """Used for cleanup only. Call unobserve() explicitely instead."""
        self.unobserve()

    def unobserve(self):
        if self._notifier:
            self._notifier._unobserve(self)
            self._notifier = None


class ArsdkObserver:
    """Arsdk event observer and callback registry.

    We use message class instances as keys in the stored dictionary.
    The Python IDs ot the messages have been modified to match the ones present
    in struct_arsdk_cmd. This allows us to access callbacks with only the ID.
    See internal/messages.py to see how the ID is computed.
    """

    def __init__(self):
        self._observers = {}
        self._log = logging.getLogger('ArsdkObserver')

    def _attach_event(self, event, callback, observer):
        observers = self._observers.get(event, None)
        if observers is None:
            observers = dict()
            self._observers[event] = observers
        observers[observer] = callback

    def _remove_observer(self, event, observer):
        observers = self._observers.get(event, None)
        if observers:
            del observers[observer]
            if not observers:
                del self._observers[event]

    def _unobserve(self, observer):
        for (events, callback) in observer.bindings:
            for event in events:
                self._remove_observer(event, observer)

    def register(self, bindings):
        # TODO: Check binding format and callable
        bindings = self.normalize_bindings(bindings)
        observer = ArsdkObserverBinding(self, bindings)
        for (events, callback) in bindings:
            for event in events:
                self._attach_event(event, callback, observer)
        return observer

    def notify(self, event):
        evt_id = event.contents.id
        observers = self._observers.get(evt_id, None)
        if observers:
            # FIXME: hack to get the message class instance from event id
            msg_class = next(c for c in self._observers.keys() if c == evt_id)
            msg_dec = msg_class.decode(event)
            for callback in list(observers.values()):
                try:
                    callback(msg_dec)
                except Exception:
                    self._log.exception("Notify %s", msg_class.fullname())

    def normalize_bindings(self, args):
        if isinstance(args, tuple):
            (ev, cb) = args
            return [({ev}, cb)]
        elif isinstance(args, dict):
            return [({ev}, cb) for (ev, cb) in args.items()]
        elif isinstance(args, list):
            def bind(ev, cb):
                return (ev, cb) if isinstance(ev, set) else ({ev}, cb)
            return [bind(e, c) for (e, c) in args]
        else:
            raise ValueError("Malformed bindings")
