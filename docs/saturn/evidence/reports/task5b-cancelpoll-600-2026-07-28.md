# Task 5b cancellation-poll experiment — 2026-07-28

The transform callback polls the uncached cancellation latch once per 16
vertices instead of once per vertex. This is a bounded safety-preserving
optimization; the outer worker wait remains bounded and the callback still
observes cancellation at a finite interval.

Same-commit captures at `-r2048/-poly0`:

| profile | render ms/frame | slave share | jobs | timeouts | route parity |
| --- | ---: | ---: | ---: | ---: | --- |
| serial | 2.2151 | 0% | 0 | 0 | pass |
| dual | 17.8073 | 20.0146% | 802 | 0 | pass |

The dual render interval fell from 48,630,101 to 47,971,375 FRT ticks
(approximately 1.4%), but the utilization gate remains materially open. The
paired route comparator is in
`task5b-serial-vs-dual-cancelpoll-600-2026-07-28.json`; this experiment has no
gallery screenshot because it is a performance diagnostic, not a visual
milestone.
