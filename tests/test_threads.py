"""Threads: concurrent encode/decode, and containers that change while they are encoded.

On a free-threaded build (3.14t and later) these run truly in parallel; CI runs them with
PYTHON_GIL=0. With a GIL they still interleave (decode, compression and signing release
it). Either way, nothing may crash, and every container must be written from one
consistent snapshot.
"""

import os
import random
import subprocess
import sys
import sysconfig
import threading
import time

import pytest

import btoon

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))


def run_threads(targets, seconds=1.0):
    """Runs the targets until `seconds` pass; returns the exceptions they raised."""
    stop = threading.Event()
    errors = []

    def wrap(fn):
        def run():
            try:
                fn(stop)
            except BaseException as exc:  # surfaced by the caller
                errors.append(exc)
                stop.set()
        return run

    threads = [threading.Thread(target=wrap(fn)) for fn in targets]
    for t in threads:
        t.start()
    stop.wait(seconds)
    stop.set()
    for t in threads:
        t.join(60)
    assert not any(t.is_alive() for t in threads), "a thread hung"
    return errors


@pytest.mark.skipif(not FREE_THREADED, reason="free-threaded CPython only")
def test_import_keeps_the_gil_disabled():
    # The extension declares Py_MOD_GIL_NOT_USED: importing it must not turn the GIL back
    # on (CPython would, with a RuntimeWarning). -I ignores PYTHON_GIL, which would
    # override the check, and keeps a source checkout's btoon/ off sys.path.
    env = {k: v for k, v in os.environ.items() if k != "PYTHON_GIL"}
    code = "import sys, btoon; btoon.decode(btoon.encode([1])); print(sys._is_gil_enabled())"
    out = subprocess.run([sys.executable, "-I", "-W", "error::RuntimeWarning", "-c", code], env=env,
                         capture_output=True, text=True)
    assert out.returncode == 0 and out.stdout.strip() == "False", out.stderr


def test_parallel_encode_and_decode():
    docs = [
        [{"id": i, "name": f"user{i}", "score": i / 7, "tags": ["a", "b"][: i % 3]} for i in range(200)],
        {"nested": [[i, {"k": [i] * 3}] for i in range(100)], "blob": b"\x00\xff" * 500},
        [btoon.Date(i) for i in range(50)] + [btoon.DateTime(-i) for i in range(50)] + [2**70, -(2**65)],
    ]
    encoded = [btoon.encode(d) for d in docs]
    compressed = [btoon.encode(d, compress=True, hmac_key=b"k" * 32) for d in docs]

    def worker(seed):
        def run(stop):
            rng = random.Random(seed)
            while not stop.is_set():
                i = rng.randrange(len(docs))
                assert btoon.encode(docs[i]) == encoded[i]
                assert btoon.decode(encoded[i], timestamps="object") == btoon.decode(encoded[i], timestamps="object")
                assert btoon.decode(compressed[i], hmac_key=b"k" * 32) == btoon.decode(encoded[i])
        return run

    assert not run_threads([worker(s) for s in range(6)])


def test_containers_changed_by_other_threads_encode_consistently():
    # Each mutation below is atomic (a slice assignment, dict.update(), a row update), so a
    # consistent snapshot shows exactly one state: never a mix, a hole or a stale value.
    states = [[1] * 50, [2] * 80, [3] * 3]
    shared_list = list(states[0])
    shared_dict = {f"k{j}": 0 for j in range(30)}
    rows = [{"a": 0, "b": 0, "c": 0} for _ in range(40)]
    fresh = [""]  # new non-ASCII strs: threads race to fill the same str's UTF-8 cache
    doc = {"list": shared_list, "dict": shared_dict, "rows": rows, "again": shared_list, "fresh": fresh}

    def mutate_list(stop):
        i = 0
        while not stop.is_set():
            i += 1
            shared_list[:] = states[i % 3]

    def mutate_dict(stop):
        i = 0
        while not stop.is_set():
            i += 1
            shared_dict.update({f"k{j}": i for j in range(30)})

    def mutate_rows(stop):
        rng = random.Random(1)
        i = 0
        while not stop.is_set():
            i += 1
            rows[rng.randrange(len(rows))].update(a=i, b=i, c=i)
            fresh[:] = [f"ключ-{i}-é" * 3, {f"é{i}": f"значение {i}"}]

    checked = []

    def encoder(stop):
        while not stop.is_set():
            out = btoon.decode(btoon.encode(doc))
            assert out["list"] in states and out["again"] in states
            assert len(set(out["dict"].values())) == 1, out["dict"]
            assert all(r["a"] == r["b"] == r["c"] for r in out["rows"])
            if len(out["fresh"]) == 2:
                i = out["fresh"][0].split("-")[1]
                assert out["fresh"] == [f"ключ-{i}-é" * 3, {f"é{i}": f"значение {i}"}]
            checked.append(1)

    errors = run_threads([mutate_list, mutate_dict, mutate_rows] + [encoder] * 4, seconds=1.5)
    assert not errors, errors[0]
    assert checked


def test_table_cells_replaced_while_encoding():
    # Without the GIL, 0.2 segfaulted here within seconds: it read table cells as borrowed
    # references, and another thread replacing a cell freed it before the encoder took
    # its own reference. Rows are now copied (strong references) under their lock.
    rows = [{"a": [i] * 8, "b": {"x": [i]}, "c": "s" * 40 + str(i)} for i in range(64)]

    def replace(seed):
        def run(stop):
            i = seed
            while not stop.is_set():
                i += 1
                row = rows[i % len(rows)]
                row["a"] = [i] * 8  # frees the old list: only the row referenced it
                row["b"] = {"x": [i]}
                row["c"] = "s" * 40 + str(i)
        return run

    def encoder(stop):
        while not stop.is_set():
            out = btoon.decode(btoon.encode(rows))
            assert len(out) == len(rows) and all(len(r["a"]) == 8 for r in out)

    errors = run_threads([replace(k * 1000) for k in range(4)] + [encoder] * 4, seconds=2.0)
    assert not errors, errors[0]


def test_containers_torn_apart_by_other_threads_never_crash():
    # Unstructured changes (resizing, deleting, nesting) while four threads encode: every
    # encode either succeeds with a decodable document or raises a BtoonError.
    shared = {"list": list(range(100)), "dict": {str(i): i for i in range(100)}, "rows": [{"x": 1}] * 20}

    def chaos(seed):
        def run(stop):
            rng = random.Random(seed)
            while not stop.is_set():
                op = rng.randrange(6)
                lst, d = shared["list"], shared["dict"]
                try:
                    if op == 0:
                        lst.append(rng.random())
                    elif op == 1:
                        lst.pop(rng.randrange(len(lst) + 1))
                    elif op == 2:
                        d[str(rng.randrange(200))] = [rng.random()] * rng.randrange(5)
                    elif op == 3:
                        d.popitem()
                    elif op == 4:
                        shared["rows"] = [{"x": i, "y": [i]} for i in range(rng.randrange(30))]
                    else:
                        lst.clear() if rng.random() < 0.05 else lst.extend(range(10))
                except (IndexError, KeyError):  # the other chaos thread got there first
                    pass
        return run

    def encoder(stop):
        while not stop.is_set():
            try:
                data = btoon.encode(shared)
            except btoon.BtoonError:
                continue
            btoon.decode(data)

    errors = run_threads([chaos(1), chaos(2), encoder, encoder, encoder, encoder], seconds=1.5)
    assert not errors, errors[0]


def test_default_and_decode_run_alongside_mutation():
    # default() runs Python code mid-encode; with other threads mutating what it returns,
    # the replacement is still encoded from one snapshot.
    payload = {"v": [0] * 10}

    class Box:
        pass

    def default(o):
        return payload["v"]

    def mutate(stop):
        i = 0
        while not stop.is_set():
            i += 1
            payload["v"] = [i] * 10
            payload["v"][:] = [i] * 10

    def encoder(stop):
        while not stop.is_set():
            out = btoon.decode(btoon.encode([Box(), Box()], default=default))
            assert all(len(set(x)) == 1 for x in out)

    errors = run_threads([mutate, encoder, encoder], seconds=1.0)
    assert not errors, errors[0]


def test_bytearray_resized_during_decode():
    # The input is copied under its buffer export, so a concurrent resize either waits for
    # the export (BufferError) or happens before/after the copy; decode sees whole bytes.
    data = bytearray(btoon.encode([{"a": i, "b": "x" * 10} for i in range(2000)]))
    expected = btoon.decode(bytes(data))
    resizes = 0

    def resize(stop):
        nonlocal resizes
        while not stop.is_set():
            try:
                data.extend(b"\x00")
                del data[-1:]
                resizes += 1
            except BufferError:
                pass

    def decoder(stop):
        while not stop.is_set():
            try:
                assert btoon.decode(data) == expected
            except btoon.BtoonError as exc:
                assert exc.code == "trailing_data"  # copied while the extra byte was there

    errors = run_threads([resize, decoder, decoder], seconds=1.0)
    assert not errors, errors[0]


def test_stress_finishes_quickly():
    # Guard against a lock-order deadlock between critical sections: many short encodes of
    # nested containers shared by all threads must keep making progress.
    a = [{"x": [1, 2, 3]}] * 10
    b = {"a": a, "b": [a, a]}
    done = []

    def work(stop):
        n = 0
        while not stop.is_set():
            btoon.encode([a, b, a])
            n += 1
        done.append(n)

    start = time.monotonic()
    assert not run_threads([work] * 8, seconds=0.5)
    assert time.monotonic() - start < 30
    assert all(n > 0 for n in done)
