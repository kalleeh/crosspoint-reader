# AWS Certification Quiz - Question Banks

This directory contains question bank files for AWS certification quizzes.

## File Location

Place question files on your device's SD card at:
```
/.crosspoint/aws-quiz/
```

## File Format

Each certification should have its own JSON file named after the cert ID:
- `cloud-practitioner.json`
- `solutions-architect-associate.json`
- `developer-associate.json`
- etc.

## JSON Structure

```json
{
  "questions": [
    {
      "question": "What is Amazon S3?",
      "options": [
        "A) A compute service",
        "B) An object storage service",
        "C) A database service",
        "D) A networking service"
      ],
      "correct": 1,
      "explanation": "Amazon S3 is an object storage service that offers industry-leading scalability, data availability, security, and performance."
    }
  ]
}
```

## Field Descriptions

- **question**: The question text (string)
- **options**: Array of 4 answer choices (strings)
- **correct**: Index of correct answer (0-3, where 0 = first option)
- **explanation**: Explanation of the correct answer (string)

## Example File

See `example-cloud-practitioner.json` for a complete example.

## Notes

- Questions are loaded from SD card only (not embedded in firmware)
- This saves flash memory space
- You can update questions without reflashing firmware
- Quiz will show an error if no question file is found
