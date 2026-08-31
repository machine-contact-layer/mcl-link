import os
import random
from context_negotiation_model import *


def main():
    good = ContextKey(0, 7, 3, b"\x11\x22\x33\x44")
    r = ContextReceiver((0,))

    try:
        r.authorize_compressed_decode(good)
        raise AssertionError("compressed decode before context accepted")
    except ContextMismatch:
        pass

    r.accept_offer(good)
    assert r.authorize_compressed_decode(good)

    bads = [
        ContextKey(0, 8, 3, good.ruleset_digest),
        ContextKey(0, 7, 2, good.ruleset_digest),
        ContextKey(0, 7, 3, b"\xaa\xbb\xcc\xdd"),
        ContextKey(1, 7, 3, good.ruleset_digest),
    ]
    for bad in bads:
        try:
            r.authorize_compressed_decode(bad)
            raise AssertionError(f"accepted mismatched context {bad}")
        except ContextMismatch:
            pass

    try:
        r.accept_offer(ContextKey(1, 1, 1, b"1234"))
        raise AssertionError("accepted incompatible major")
    except IncompatibleVersion:
        pass

    r.reset()
    try:
        r.authorize_compressed_decode(good)
        raise AssertionError("old context survived reset")
    except ContextMismatch:
        pass

    r.accept_offer(good)
    accepted = 0
    rejected = 0
    for _ in range(10000):
        if random.random() < 0.05:
            candidate = good
        else:
            candidate = ContextKey(
                random.choice([0, 1]),
                random.randrange(0, 256),
                random.randrange(0, 256),
                os.urandom(4),
            )
        try:
            r.authorize_compressed_decode(candidate)
            assert candidate == good
            accepted += 1
        except ContextMismatch:
            assert candidate != good
            rejected += 1

    print("exact accepted:", accepted)
    print("mismatched rejected:", rejected)
    print("context safety properties: PASS")


if __name__ == "__main__":
    main()
