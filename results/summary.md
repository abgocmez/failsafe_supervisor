| fault class | path | n | detect p50 | detect p99 | detect max | recover p50 | recover p99 |
|---|---|---:|---:|---:|---:|---:|---:|
| crash-stop (SIGKILL) | SIGCHLD | 100 | 1.36 | 1.70 | 1.70 | 216.9 | 221.0 |
| fail-silent (SIGSTOP) | DEADLINE | 100 | 52.26 | 102.97 | 105.00 | 266.1 | 315.1 |
| timing / late (300ms stall) | DEADLINE | 100 | 53.39 | 97.07 | 104.54 | 268.8 | 316.7 |

(all latencies in ms; detection measured from fault injection to the supervisor's monotonic-clock event)
