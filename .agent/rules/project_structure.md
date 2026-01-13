# Project Structure Rules

## Folder Organization

- **`docs/`**: Documentation files. All project documentation, research notes, and analysis reports should reside here.
- **`logs/`**: Log files. All runtime logs (`.log`) and diagnostic outputs should be stored here.
- **`scripts/`**: Shell scripts. Helper scripts for building, deploying, and testing.
- **`src/`**: Source Code. Main source directory for the C++/Qt application.
- **`.agent/`**: Agent configuration. Contains rules (`rules/`) and workflows (`workflows/`).

## Root Directory Hygiene

- The root directory should only contain:
    - `README.md`
    - `CHANGELOG.md`
    - Top-level build config
    - Key subdirectories
- Avoid creating temporary files in the root.
