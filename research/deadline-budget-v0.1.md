# Contact Deadline Lower Bounds v0.1

Status: **research calculation**

This note combines the MCL Core scenario deadlines/ranges with the MCL Wire fixed Tier-0 candidate sizes to calculate a hard lower bound on payload rate.

For a one-way message:

```text
propagation_time = distance / sound_speed
serialization_budget = decision_deadline - propagation_time
minimum_payload_rate = message_bits / serialization_budget
```

The calculation intentionally excludes preamble, synchronization, guard time, contention, channel coding, retransmission, processing, and policy evaluation. Therefore every reported rate is an optimistic lower bound.

## Result

Across 41 current Tier-0-candidate events:

- no scenario is impossible from propagation delay alone under its current nominal range/deadline assumptions;
- 6 events spend more than half of their nominal deadline on acoustic propagation alone;
- median ideal payload-rate lower bound is about **0.38 kb/s**;
- 95th percentile is about **3.26 kb/s**;
- worst case is the S25 emergency stop `REQUEST`: about **5.01 kb/s** at 25 m with a 100 ms nominal decision deadline.

Top lower bounds:

| Scenario | Message | Range | Deadline | Propagation | Ideal minimum payload rate |
|---|---|---:|---:|---:|---:|
| S25 | REQUEST | 25 m | 100 ms | 72.8 ms | 5.01 kb/s |
| S25 | AUTHORITY_CLAIM | 25 m | 100 ms | 72.8 ms | 4.12 kb/s |
| S01 | REQUEST | 20 m | 100 ms | 58.3 ms | 3.26 kb/s |
| S01 | HAZARD | 20 m | 100 ms | 58.3 ms | 2.88 kb/s |
| S13 | HAZARD | 20 m | 100 ms | 58.3 ms | 2.88 kb/s |
| S19 | HAZARD | 50 m | 200 ms | 145.7 ms | 2.21 kb/s |

## Consequence

This supports the decision not to optimize AP-R1 around headline megabit throughput. Small deterministic semantic frames move the hard problem toward acquisition, propagation, reliability, and overhead.

It also shows why bootstrap and established-session timing must be measured separately. A 20–40 ms acquisition/preamble cost can dominate a 100 ms deadline even if the payload modem itself is several kb/s.

## Next calculation

Add explicit profiles for acquisition/preamble duration, FEC rate, link header/integrity bytes, guard interval, and acknowledgement policy, then compute end-to-end feasibility rather than the present payload-only lower bound.
