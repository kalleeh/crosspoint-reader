#!/usr/bin/env python3
"""
Normalise 9 AWS quiz JSON files to the canonical schema used by the parser.

Canonical per-question shape:
  {
    "question": "...",
    "options": ["text A", "text B", "text C", "text D"],   # always a list
    "correct": 0,                                           # always an int (0-3)
    "domain": "...",
    "explanation": "..."
  }

Conversion rules applied:
  1. Rename "correct_answer" → "correct" when "correct" is absent.
  2. Convert letter correct value ("A"/"B"/"C"/"D") → integer (0/1/2/3).
  3. Convert dict options ({"A": ..., "B": ..., "C": ..., "D": ...}) → list.
  4. All other fields are kept unchanged.
  5. Files are overwritten in-place (indent=2, ensure_ascii=False).
"""

import json
import pathlib

BASE = pathlib.Path("/Users/wallbomk/Projects.local/crosspoint-reader/aws-quiz")

LETTER_TO_INT = {"A": 0, "B": 1, "C": 2, "D": 3}

# Files wrapped in a top-level dict; their questions live under the key "questions"
TARGET_FILES = [
    "sysops-associate.json",
    "security-specialty.json",
    "database-specialty.json",
    "ml-specialty.json",
    "analytics-specialty.json",
    "networking-specialty.json",
    "sa-professional.json",
    "devops-professional.json",
    "sap-specialty.json",
]


def normalise_question(q: dict) -> dict:
    """Return a new question dict that conforms to the canonical schema."""
    q = dict(q)  # shallow copy so we don't mutate the original in-place

    # --- Step 1: rename correct_answer → correct ---
    if "correct_answer" in q and "correct" not in q:
        q["correct"] = q.pop("correct_answer")
    elif "correct_answer" in q:
        # Both keys present; drop the redundant one
        q.pop("correct_answer")

    # --- Step 3: convert dict options → list ---
    if isinstance(q.get("options"), dict):
        opts = q["options"]
        q["options"] = [opts["A"], opts["B"], opts["C"], opts["D"]]

    # --- Step 2: convert letter correct → int ---
    if isinstance(q.get("correct"), str):
        letter = q["correct"].strip().upper()
        if letter in LETTER_TO_INT:
            q["correct"] = LETTER_TO_INT[letter]
        else:
            raise ValueError(f"Unexpected correct value: {q['correct']!r}")

    return q


def process_file(fname: str) -> int:
    """Normalise a file in-place. Returns the number of questions processed."""
    path = BASE / fname
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)

    # All target files wrap questions under a "questions" key inside a dict
    if isinstance(data, dict) and "questions" in data:
        original_questions = data["questions"]
        data["questions"] = [normalise_question(q) for q in original_questions]
        count = len(data["questions"])
    elif isinstance(data, list):
        data = [normalise_question(q) for q in data]
        count = len(data)
    else:
        raise ValueError(f"Unrecognised top-level structure in {fname}")

    with open(path, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")  # trailing newline

    return count


def main():
    print(f"{'File':<35}  {'Questions':>10}  Status")
    print("-" * 55)
    total = 0
    for fname in TARGET_FILES:
        try:
            count = process_file(fname)
            total += count
            print(f"{fname:<35}  {count:>10}  OK")
        except Exception as exc:
            print(f"{fname:<35}  {'ERROR':>10}  {exc}")
    print("-" * 55)
    print(f"{'TOTAL':<35}  {total:>10}")


if __name__ == "__main__":
    main()
