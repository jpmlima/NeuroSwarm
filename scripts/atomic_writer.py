#!/usr/bin/env python3
import os
import sys
import tempfile

def atomic_write(file_path, content):
    """Writes content to a file atomically using a temporary file."""
    dir_name = os.path.dirname(os.path.abspath(file_path))
    with tempfile.NamedTemporaryFile('w', dir=dir_name, delete=False) as tf:
        tf.write(content)
        tempname = tf.name
    try:
        os.replace(tempname, file_path)
        print(f"[ATOMIC_WRITER] Successfully wrote to {file_path}")
        return True
    except Exception as e:
        if os.path.exists(tempname):
            os.remove(tempname)
        print(f"[ATOMIC_WRITER] Error: {e}")
        return False

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: atomic_writer.py <file_path> <content>")
        sys.exit(1)
    atomic_write(sys.argv[1], sys.argv[2])
