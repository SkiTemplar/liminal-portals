"""Carga sostenida en todos los hilos lógicos durante N segundos (benchmark térmico)."""

import math
import multiprocessing as mp
import sys
import time


def burn(deadline: float) -> int:
    count = 0
    while time.time() < deadline:
        for i in range(10_000):
            count += int(math.sqrt(i * 1.000001) * math.sin(i))
    return count


def main() -> int:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 300.0
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else mp.cpu_count()
    deadline = time.time() + seconds
    with mp.Pool(workers) as pool:
        pool.map(burn, [deadline] * workers)
    print(f"cpu_stress: {workers} procesos durante {seconds:.0f} s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
