from dataclasses import dataclass


class ContextError(ValueError):
    pass


class IncompatibleVersion(ContextError):
    pass


class ContextMismatch(ContextError):
    pass


@dataclass(frozen=True)
class ContextKey:
    wire_major: int
    context_id: int
    generation: int
    ruleset_digest: bytes


class ContextReceiver:
    def __init__(self, supported_wire_majors=(0,)):
        self.supported_wire_majors = set(supported_wire_majors)
        self.active: ContextKey | None = None

    def accept_offer(self, key: ContextKey):
        if key.wire_major not in self.supported_wire_majors:
            raise IncompatibleVersion(key.wire_major)
        if not 0 <= key.context_id <= 255:
            raise ContextError("context_id")
        if not 0 <= key.generation <= 255:
            raise ContextError("generation")
        if len(key.ruleset_digest) < 4:
            raise ContextError("digest too short")
        self.active = key
        return key

    def reset(self):
        self.active = None

    def authorize_compressed_decode(self, key: ContextKey):
        if self.active is None:
            raise ContextMismatch("no active context")
        if key != self.active:
            raise ContextMismatch("context key mismatch")
        return True
