import pytest
import ctypes
import struct


# Simulate a safe string copy function that enforces bounds checking
# This represents what syscall_strcpy SHOULD do (truncate or reject oversized input)
def safe_strcpy(dst_size, src):
    """
    Safe string copy that enforces buffer bounds.
    Returns the copied string truncated to dst_size - 1 (leaving room for null terminator),
    or raises ValueError if src is None.
    This is the invariant: reads must never exceed declared buffer length.
    """
    if src is None:
        raise ValueError("Source string cannot be None")
    
    # The invariant: destination buffer must not be overflowed
    # Safe implementation truncates to dst_size - 1 characters + null terminator
    max_copy = dst_size - 1  # Reserve space for null terminator
    if max_copy < 0:
        raise ValueError("Buffer size must be at least 1")
    
    result = src[:max_copy]
    return result


def simulate_buffer_copy(dst_buffer_size, src_string):
    """
    Simulates a buffer copy operation with bounds checking.
    Returns (success, copied_data, overflow_detected)
    """
    if dst_buffer_size <= 0:
        return False, None, True
    
    src_bytes = src_string.encode('utf-8') if isinstance(src_string, str) else src_string
    src_len = len(src_bytes)
    
    # Check if source exceeds destination buffer
    overflow_would_occur = src_len >= dst_buffer_size  # >= because we need null terminator
    
    # Safe copy: truncate to fit
    max_copy = dst_buffer_size - 1
    copied = src_bytes[:max_copy]
    
    return True, copied, overflow_would_occur


@pytest.mark.parametrize("dst_size,payload", [
    # Normal buffer size (64 bytes) with oversized inputs
    (64, "A" * 128),           # 2x overflow
    (64, "A" * 640),           # 10x overflow
    (64, "A" * 1024),          # 16x overflow
    (64, "B" * 65),            # Just over boundary
    (64, "C" * 63),            # Just under boundary (valid)
    (64, "D" * 64),            # Exactly at boundary (overflow by 1 for null)
    
    # Small buffer (8 bytes) with oversized inputs
    (8, "A" * 16),             # 2x overflow
    (8, "A" * 80),             # 10x overflow
    (8, "ABCDEFGH"),           # Exactly at boundary
    (8, "ABCDEFG"),            # Valid (7 chars + null)
    
    # Attack payloads - format string attacks
    (64, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"),
    (64, "%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x"),
    (64, "%n%n%n%n%n%n%n%n"),
    
    # Attack payloads - null byte injection
    (64, "safe\x00" + "A" * 100),
    (64, "\x00" * 128),
    
    # Attack payloads - shell metacharacters
    (64, "; rm -rf / #" * 10),
    (64, "$(cat /etc/passwd)" * 5),
    (64, "`whoami`" * 20),
    
    # Attack payloads - unicode/encoding attacks
    (64, "\xff\xfe" * 64),
    (64, "\xc0\xaf" * 64),
    
    # Attack payloads - return-oriented programming pattern
    (64, "\x41\x42\x43\x44\x45\x46\x47\x48" * 20),
    
    # Attack payloads - heap spray pattern
    (64, "\x90" * 256),        # NOP sled
    (64, "\xcc" * 256),        # INT3 breakpoints
    
    # Attack payloads - very large inputs
    (64, "X" * 10000),
    (64, "Y" * 100000),
    
    # Edge cases
    (1, "A"),                  # Minimum buffer, single char (overflow)
    (1, ""),                   # Minimum buffer, empty string (valid)
    (2, "A"),                  # Small buffer, valid
    (2, "AB"),                 # Small buffer, overflow by null
    
    # Attack payloads - path traversal
    (64, "../" * 30 + "etc/passwd"),
    (64, "..\\..\\..\\windows\\system32\\" * 5),
    
    # Attack payloads - SQL injection style
    (64, "' OR '1'='1'; DROP TABLE users; --" * 3),
    
    # Attack payloads - binary data
    (64, bytes(range(256)).decode('latin-1') * 2),
])
def test_buffer_read_never_exceeds_declared_length(dst_size, payload):
    """
    Invariant: Buffer reads must never exceed the declared destination buffer length.
    
    A safe string copy implementation must either:
    1. Truncate the source to fit within the destination buffer (including null terminator), OR
    2. Reject/raise an error for inputs that would overflow the buffer.
    
    Under NO circumstances should the copied data exceed dst_size bytes
    (dst_size - 1 characters + null terminator).
    """
    success, copied_data, overflow_would_occur = simulate_buffer_copy(dst_size, payload)
    
    # Invariant 1: The operation must succeed (safe copy always truncates)
    assert success is True, (
        f"Buffer copy operation failed unexpectedly for dst_size={dst_size}"
    )
    
    # Invariant 2: Copied data must never be None when operation succeeds
    assert copied_data is not None, (
        f"Copied data is None after successful operation"
    )
    
    # Invariant 3: The length of copied data must NEVER exceed dst_size - 1
    # (dst_size - 1 because we need space for null terminator)
    max_allowed_length = dst_size - 1
    actual_length = len(copied_data)
    
    assert actual_length <= max_allowed_length, (
        f"BUFFER OVERFLOW DETECTED: copied {actual_length} bytes into buffer of size {dst_size}. "
        f"Maximum allowed is {max_allowed_length} bytes (excluding null terminator). "
        f"Payload length was {len(payload)} bytes. "
        f"This violates CWE-120: Buffer Copy without Checking Size of Input."
    )
    
    # Invariant 4: If overflow would occur, verify truncation happened correctly
    if overflow_would_occur:
        assert actual_length == max_allowed_length, (
            f"When overflow is detected, data should be truncated to exactly {max_allowed_length} bytes, "
            f"but got {actual_length} bytes."
        )
    
    # Invariant 5: Verify the copied data is a prefix of the source
    src_bytes = payload.encode('utf-8') if isinstance(payload, str) else payload
    if len(src_bytes) > 0 and actual_length > 0:
        assert src_bytes[:actual_length] == copied_data, (
            f"Copied data is not a valid prefix of source data. "
            f"Data corruption detected during bounds-checked copy."
        )
    
    # Invariant 6: Safe copy result from safe_strcpy must also respect bounds
    safe_result = safe_strcpy(dst_size, payload if isinstance(payload, str) else payload.decode('latin-1'))
    assert len(safe_result) <= max_allowed_length, (
        f"safe_strcpy violated bounds: returned {len(safe_result)} chars for buffer size {dst_size}"
    )


@pytest.mark.parametrize("dst_size,payload", [
    (64, "A" * 128),
    (64, "A" * 640),
    (8, "overflow_attack"),
    (16, "B" * 32),
])
def test_no_out_of_bounds_read_with_ctypes_buffer(dst_size, payload):
    """
    Invariant: Using ctypes to simulate actual buffer allocation,
    verify that a safe copy never writes beyond the allocated buffer boundaries.
    """
    # Allocate a buffer of exactly dst_size bytes
    buffer = ctypes.create_string_buffer(dst_size)
    
    src_bytes = payload.encode('utf-8') if isinstance(payload, str) else payload
    
    # Safe copy: only copy up to dst_size - 1 bytes
    max_copy = dst_size - 1
    safe_src = src_bytes[:max_copy]
    
    # Perform the safe copy into the ctypes buffer
    buffer.raw = safe_src + b'\x00' * (dst_size - len(safe_src))
    
    # Verify buffer integrity
    result = buffer.raw
    
    # Invariant: result length must equal dst_size (the allocated buffer size)
    assert len(result) == dst_size, (
        f"Buffer size mismatch: expected {dst_size}, got {len(result)}"
    )
    
    # Invariant: The string content (up to null terminator) must not exceed dst_size - 1
    null_pos = result.find(b'\x00')
    if null_pos != -1:
        string_length = null_pos
    else:
        string_length = dst_size
    
    assert string_length <= dst_size - 1, (
        f"String in buffer exceeds safe length: {string_length} > {dst_size - 1}. "
        f"Buffer overflow invariant violated!"
    )
    
    # Invariant: The copied content must be a prefix of the original source
    assert src_bytes[:string_length] == result[:string_length], (
        f"Buffer content does not match expected prefix of source"
    )


@pytest.mark.parametrize("payload_size_multiplier,base_buffer_size", [
    (2, 64),
    (10, 64),
    (100, 64),
    (2, 8),
    (10, 8),
    (2, 256),
    (10, 256),
])
def test_overflow_detection_scales_with_input_size(payload_size_multiplier, base_buffer_size):
    """
    Invariant: Regardless of how much larger the input is than the buffer,
    the output must always be bounded by the buffer size.
    This tests that the security property holds at scale.
    """
    payload = "A" * (base_buffer_size * payload_size_multiplier)
    
    success, copied_data, overflow_would_occur = simulate_buffer_copy(base_buffer_size, payload)
    
    # Overflow must be detected when payload exceeds buffer
    assert overflow_would_occur is True, (
        f"Overflow not detected for payload of size {len(payload)} "
        f"into buffer of size {base_buffer_size}"
    )
    
    # Despite overflow attempt, copied data must be bounded
    assert len(copied_data) <= base_buffer_size - 1, (
        f"CRITICAL: Buffer overflow not prevented! "
        f"Copied {len(copied_data)} bytes into {base_buffer_size}-byte buffer. "
        f"Input was {payload_size_multiplier}x the buffer size. "
        f"CWE-120 vulnerability confirmed."
    )
    
    # The amount of data NOT copied should be proportional to overflow
    bytes_truncated = len(payload) - len(copied_data)
    expected_min_truncation = len(payload) - (base_buffer_size - 1)
    
    assert bytes_truncated >= expected_min_truncation, (
        f"Insufficient truncation: only {bytes_truncated} bytes truncated, "
        f"expected at least {expected_min_truncation}"
    )