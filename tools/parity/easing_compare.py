#!/usr/bin/env python3
"""Compare the easing tables numerically by implementing both sides from their sources.

The port implements the reference's named `xxxOut` / `xxxInOut` helpers generically as
`1 - in(1 - x)` and the `x2 < 1` split. This script evaluates both formulations at a dense
grid so the equivalence (or any difference) is a measured result rather than a reading.
"""
import math

C1 = 1.70158
C3 = C1 + 1.0
N1 = 7.5625
D1 = 2.75
C4 = (2.0 * math.pi) / 3.0


def sine_in(x): return 1.0 - math.cos(x * math.pi / 2.0)
def quad_in(x): return x * x
def cubic_in(x): return x * x * x
def quart_in(x): return x * x * x * x
def quint_in(x): return x * x * x * x * x
def expo_in(x): return 0.0 if x == 0.0 else math.pow(2.0, 10.0 * x - 10.0)
def circ_in(x): return 1.0 - math.sqrt(max(0.0, 1.0 - x * x))
def back_in(x): return C3 * x * x * x - C1 * x * x
def elastic_in(x):
    if x == 0.0: return 0.0
    if x == 1.0: return 1.0
    return -math.pow(2.0, 10.0 * x - 10.0) * math.sin((x * 10.0 - 10.75) * C4)


def bounce_out(x):
    if x < 1.0 / D1:
        return N1 * x * x
    if x < 2.0 / D1:
        t = x - 1.5 / D1
        return N1 * t * t + 0.75
    if x < 2.5 / D1:
        t = x - 2.25 / D1
        return N1 * t * t + 0.9375
    t = x - 2.625 / D1
    return N1 * t * t + 0.984375


def bounce_in(x): return 1.0 - bounce_out(1.0 - x)


def side_out(fn, x):
    """The port's generic out form."""
    return 1.0 - fn(1.0 - x)


def side_in_out(fn, x):
    x2 = x * 2.0
    if x2 < 1.0:
        return fn(x2) / 2.0
    return 1.0 - fn(2.0 - x2) / 2.0


# The reference's named helpers, transcribed from Easing.java.
def ref_out(fn, x):
    return 1.0 - fn(1.0 - x)


def ref_in_out(fn, x):
    x2 = x * 2.0
    if x2 < 1.0:
        return fn(x2) / 2.0
    return 1.0 - fn(2.0 - x2) / 2.0


BASE = {
    "sine": sine_in, "quad": quad_in, "cubic": cubic_in, "quart": quart_in,
    "quint": quint_in, "expo": expo_in, "circ": circ_in, "back": back_in,
    "elastic": elastic_in,
}


def main() -> int:
    worst = 0.0
    worst_at = None
    samples = [i / 400.0 for i in range(401)]
    for name, fn in BASE.items():
        for label, port_fn, ref_fn in (
            ("out", lambda x, f=fn: side_out(f, x), lambda x, f=fn: ref_out(f, x)),
            ("inout", lambda x, f=fn: side_in_out(f, x), lambda x, f=fn: ref_in_out(f, x)),
        ):
            for x in samples:
                a = port_fn(x)
                b = ref_fn(x)
                delta = abs(a - b)
                if delta > worst:
                    worst = delta
                    worst_at = (name, label, x, a, b)
    # bounce is dispatched directly on both sides; check the in/out pair too.
    for x in samples:
        for port_val, ref_val, label in (
            (bounce_in(x), 1.0 - bounce_out(1.0 - x), "bounceIn"),
            (side_in_out(bounce_in, x), ref_in_out(bounce_in, x), "bounceInOut"),
        ):
            delta = abs(port_val - ref_val)
            if delta > worst:
                worst = delta
                worst_at = (label, "", x, port_val, ref_val)

    print(f"max |port - reference| over all easing pairs = {worst:.3e}")
    if worst_at:
        print(f"worst at {worst_at}")
    print("VERDICT:", "EQUIVALENT" if worst < 1e-12 else "DIVERGES")
    return 0 if worst < 1e-12 else 1


if __name__ == "__main__":
    raise SystemExit(main())
