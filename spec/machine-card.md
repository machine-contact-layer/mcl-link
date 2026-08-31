# MCL MachineCard v0

Status: **Research Draft**

A `MachineCard` is the compact capability description used during or immediately after first contact. It is conceptually richer than a bootstrap digest but smaller than a general device description.

## Working logical structure

```text
MachineCard {
    protocol_versions
    machine_class?
    semantic_extensions[]
    governing_capabilities[]
    transport_bindings[]
    transport_profiles[]
    duplex_modes[]
    max_frame_size?
    credential_types[]
    extension_namespaces[]
}
```

## Principles

- A MachineCard is a capability claim, not proof that the machine is authorized to exercise every capability.
- The bootstrap may carry only a digest/reference; the full card may follow after contact.
- MCL-AP may attach measured directional link state separately. Static device capability must not be confused with current path capability.
- Unknown optional extensions should be ignorable.
- Compact representation belongs to MCL Wire.

## Example

```yaml
protocol_versions:
  core: ["0.0-draft"]
  wire: ["0.0-draft"]
machine_class: service_robot
governing_capabilities:
  - hazard_receive
  - request_receive
  - transport_handoff
transport_bindings:
  - mcl-ap
  - mcl-ble
transport_profiles:
  mcl-ap: [AP-B0, AP-R1]
  mcl-ble: [connectionless, connected]
duplex_modes:
  - half
credential_types:
  - opaque-reference
```

Numeric/bit-efficient encoding is intentionally not frozen in this draft.
