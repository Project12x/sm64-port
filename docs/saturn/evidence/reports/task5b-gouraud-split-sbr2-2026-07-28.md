# Task 5b SBR2 checkpoint telemetry — 2026-07-28

The route probe now snapshots performance counters at the exact frozen BOB
checkpoint (`replay_ticks=600`). The paired reports pass the v3 route
comparator after applying the declared build profile (`view_radius=6000`,
`poly_tier=0`).

| checkpoint field | serial | dual |
|---|---:|---:|
| route state signature | identical | identical |
| triangles transformed / emitted | 854,580 / 110,958 | 854,580 / 110,958 |
| frame serial | 150 | 150 |
| sim FRT accumulator | 23,634,881 | 23,635,006 |
| render FRT accumulator | 2,957,536 | 4,999,436 |
| render FRT last | 12,893 | 10,853 |
| master wait ticks | 0 | 868,889 |
| slave busy ticks / jobs | 0 / 0 | 14,985,355 / 600 |
| slave timeouts / faults | 0 / 0 | 0 / 0 |

The route and renderer counters are exact matches. The new telemetry is
informational by design: it exposes the dispatch cost without weakening the
determinism gate. Render FRT is the first comparable performance signal; it
shows the current split is not yet a win (the dual path has higher accumulated
render time) and identifies master/slave synchronization as the next
optimization target.
