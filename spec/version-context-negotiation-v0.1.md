# Version and Context Negotiation v0.1

Status: **Research Draft**

This document prevents compact MCL Wire representations from becoming a source of silent semantic corruption.

## 1. Wire-version rule

A node MUST determine compatible major Wire behavior before decoding established-session context-compressed objects.

Major version negotiation may occur through MCL-AP bootstrap, another transport binding, or an already-established MCL Link path.

Version 0 is experimental and has no long-term compatibility promise.

## 2. Context establishment

Context compression is never implicit.

Logical exchange:

```text
CONTEXT_OFFER {
    wire_major
    schema_revision
    context_id
    generation
    ruleset_digest
    lifetime
}

CONTEXT_ACCEPT {
    context_id
    generation
    digest_confirmation
}
```

Exact field widths are not frozen.

A sender MUST NOT emit context-compressed application objects until the corresponding offer has been accepted.

## 3. Context identity

`context_id` is a compact session reference.
`generation` distinguishes reset/reuse.
`ruleset_digest` detects mismatched dictionaries/rules.

A context identifier is not a machine identity or credential.

## 4. Decode failure

If an object refers to:
- an unknown context ID;
- a stale generation;
- a mismatched ruleset digest;
- an incompatible Wire major version;

the receiver MUST NOT reinterpret the bytes using a default/fixed schema.

The receiver MUST drop the object and MAY send an explicit context reset/NACK if the active transport permits a response.

## 5. Recovery

Recovery path:

```text
CONTEXT_MISMATCH
    -> reject compressed object
    -> CONTEXT_NACK / RESET
    -> sender transmits fixed/non-context representation
    -> new context negotiation if useful
```

Safety-critical one-way broadcast profiles SHOULD prefer a self-contained representation unless context validity is already strongly established and the application tolerates context loss.

## 6. Downgrade behavior

A node MUST NOT silently downgrade to a semantically different interpretation.

When multiple compatible versions/profiles exist, selection is explicit and observable to local policy.

Security-sensitive deployments may reject downgrade when freshness/authentication policy says the offer is suspicious.

## 7. Unknown extensions

The negotiated Core/Wire version does not imply support for every extension.

Extension capability is separately advertised.
Unknown critical extensions reject the object.
Unknown non-critical extensions may be skipped under deterministic extension framing rules.
