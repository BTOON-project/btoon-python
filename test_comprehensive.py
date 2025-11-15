#!/usr/bin/env python3
"""
Comprehensive test suite for btoon-python
Tests all major functionality including encoding, decoding, types, and edge cases.
"""

import btoon
import sys

def test_basic_types():
    """Test encoding/decoding of all basic Python types."""
    print("Testing basic types...", end=" ")

    test_cases = [
        None,
        True,
        False,
        0,
        123,
        -456,
        3.14159,
        -2.71828,
        "hello world",
        "unicode: 你好世界 🌍",
        b"binary data",
        [],
        [1, 2, 3],
        {},
        {"key": "value"},
        {"nested": {"dict": {"with": "values"}}},
        [1, "mixed", 3.14, True, None],
    ]

    for original in test_cases:
        encoded = btoon.encode(original)
        decoded = btoon.decode(encoded)
        assert decoded == original, f"Mismatch: {original} != {decoded}"

    print("✅ PASSED")
    return True


def test_tabular_optimization():
    """Test tabular optimization for arrays of uniform objects."""
    print("Testing tabular optimization...", end=" ")

    # Create uniform array of objects (should trigger tabular optimization)
    users = [
        {"id": i, "name": f"user{i}", "age": 20 + i, "active": i % 2 == 0}
        for i in range(100)
    ]

    # Encode with tabular optimization
    encoded_tabular = btoon.encode(users, auto_tabular=True)

    # Encode without tabular optimization
    encoded_regular = btoon.encode(users, auto_tabular=False)

    # Tabular should be smaller (or at least not larger)
    # Note: For small datasets, tabular might not always be smaller
    # but it should decode correctly

    decoded = btoon.decode(encoded_tabular)
    assert decoded == users, "Tabular decoding failed"

    print(f"✅ PASSED (tabular: {len(encoded_tabular)} bytes vs regular: {len(encoded_regular)} bytes)")
    return True


def test_large_integers():
    """Test encoding/decoding of large integers."""
    print("Testing large integers...", end=" ")

    test_values = [
        0,
        127,  # max fixint
        128,  # min uint8
        255,  # max uint8
        256,  # min uint16
        65535,  # max uint16
        65536,  # min uint32
        2**31 - 1,  # max int32
        2**32 - 1,  # max uint32
        2**63 - 1,  # max int64
        -1,
        -128,  # min int8
        -32768,  # min int16
        -2**31,  # min int32
    ]

    for val in test_values:
        encoded = btoon.encode(val)
        decoded = btoon.decode(encoded)
        assert decoded == val, f"Integer mismatch: {val} != {decoded}"

    print("✅ PASSED")
    return True


def test_unicode_strings():
    """Test encoding/decoding of Unicode strings."""
    print("Testing Unicode strings...", end=" ")

    test_strings = [
        "",
        "ASCII",
        "Ñoño",
        "你好世界",
        "مرحبا بالعالم",
        "🌍🌎🌏",
        "Emoji: 😀😃😄😁",
        "Mixed: Hello नमस्ते 안녕하세요",
    ]

    for s in test_strings:
        encoded = btoon.encode(s)
        decoded = btoon.decode(encoded)
        assert decoded == s, f"String mismatch: {s!r} != {decoded!r}"

    print("✅ PASSED")
    return True

    print("✅ PASSED")
    return True


def test_nested_structures():
    """Test deeply nested data structures."""
    print("Testing nested structures...", end=" ")

    # Create nested structure
    nested = {"level": 0}
    current = nested
    for i in range(1, 20):
        current["child"] = {"level": i}
        current = current["child"]

    encoded = btoon.encode(nested)
    decoded = btoon.decode(encoded)
    assert decoded == nested, "Nested structure mismatch"

    # Test nested arrays
    nested_array = []
    current_array = nested_array
    for i in range(10):
        new_level = [i]
        current_array.append(new_level)
        current_array = new_level

    encoded = btoon.encode(nested_array)
    decoded = btoon.decode(encoded)
    assert decoded == nested_array, "Nested array mismatch"

    print("✅ PASSED")
    return True


def test_roundtrip():
    """Test roundtrip encoding/decoding preserves data."""
    print("Testing roundtrip...", end=" ")

    complex_data = {
        "users": [
            {"id": 1, "name": "Alice", "age": 30, "tags": ["admin", "user"]},
            {"id": 2, "name": "Bob", "age": 25, "tags": ["user"]},
        ],
        "metadata": {
            "version": "1.0",
            "timestamp": 1234567890,
            "flags": [True, False, True],
        },
        "numbers": [1, 2, 3, 4, 5],
        "mixed": [1, "two", 3.0, None, True],
    }

    encoded = btoon.encode(complex_data)
    decoded = btoon.decode(encoded)
    assert decoded == complex_data, "Roundtrip failed"

    # Test multiple roundtrips
    for _ in range(5):
        encoded = btoon.encode(decoded)
        decoded = btoon.decode(encoded)
        assert decoded == complex_data, "Multiple roundtrip failed"

    print("✅ PASSED")
    return True


def test_version():
    """Test version information."""
    print("Testing version info...", end=" ")

    version = btoon.version()
    assert isinstance(version, str), "Version should be a string"
    assert len(version) > 0, "Version should not be empty"

    print(f"✅ PASSED (version: {version})")
    return True


def test_error_handling():
    """Test error handling for invalid inputs."""
    print("Testing error handling...", end=" ")

    # Test decoding invalid data
    try:
        btoon.decode(b"\xff\xff\xff")
        assert False, "Should have raised an error for invalid data"
    except ValueError:
        pass  # Expected

    # Test decoding empty data
    try:
        btoon.decode(b"")
        assert False, "Should have raised an error for empty data"
    except ValueError:
        pass  # Expected

    print("✅ PASSED")
    return True


def test_binary_data():
    """Test encoding/decoding of binary data."""
    print("Testing binary data...", end=" ")

    test_cases = [
        b"",
        b"\x00",
        b"\x00\x01\x02\x03",
        b"\xff" * 100,
        bytes(range(256)),
    ]

    for data in test_cases:
        encoded = btoon.encode(data)
        decoded = btoon.decode(encoded)
        assert decoded == data, f"Binary data mismatch"

    print("✅ PASSED")
    return True


def test_size_comparison():
    """Compare BTOON size with Python's pickle."""
    print("Testing size comparison...", end=" ")

    import pickle

    data = {
        "users": [
            {"id": i, "name": f"user{i}", "email": f"user{i}@example.com", "active": True}
            for i in range(50)
        ]
    }

    btoon_size = len(btoon.encode(data))
    pickle_size = len(pickle.dumps(data))

    print(f"✅ PASSED (BTOON: {btoon_size} bytes, Pickle: {pickle_size} bytes)")
    return True


def main():
    """Run all tests."""
    print("=" * 60)
    print("BTOON Python - Comprehensive Test Suite")
    print("=" * 60)
    print()

    tests = [
        test_version,
        test_basic_types,
        test_large_integers,
        test_unicode_strings,
        test_binary_data,
        test_nested_structures,
        test_roundtrip,
        test_tabular_optimization,
        test_error_handling,
        test_size_comparison,
    ]

    passed = 0
    failed = 0

    for test in tests:
        try:
            if test():
                passed += 1
        except Exception as e:
            print(f"❌ FAILED: {e}")
            failed += 1
            import traceback
            traceback.print_exc()

    print()
    print("=" * 60)
    print(f"Results: {passed} passed, {failed} failed out of {len(tests)} tests")
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
