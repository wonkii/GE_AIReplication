# Shared Project Guidelines

## Saved Artifacts

All team members and automation tools working in this repository must follow the [Saved artifact management guidelines](docs/Legacy/Workflow/Saved_결과물_관리.md).

- Store newly generated build logs, test results, screenshots, reports, and temporary inspection scripts in a task-specific directory. Do not add individual files directly to the `Saved` root.
- Use `Saved/Tasks/<date-time>-<task-name>-<unique-id>/` by default. Existing task-specific layouts such as `Saved/Tests/LAN/<run-id>` and `Saved/Experiments/<BatchId>` are also allowed.
- Create `README.md` first when setting up a task directory. Record the tool or command used, the purpose, the creation time and time zone, what the key files contain and how to use them, the outcome, and whether the artifacts need to be retained. Update the outcome when the task ends. Do not record passwords or tokens.
- Use `Scripts/NewSavedTask.ps1` for PowerShell tasks. Generators written in other languages must automatically create a README following the same rules. Include this behavior when adding or updating a generator.
- Keep Unreal-managed `Saved/Logs`, `Config`, `Autosaves`, shader and build caches, and temporary cook and stage directories in their default engine locations. Put separately collected or generated artifacts in the task directory.
- Do not move or delete existing artifacts without authorization. `Saved` is excluded from Git. Keep shared rules and generator scripts in the repository, and retain any required raw experiment data and results in a separate shared team location.

## Code Changes

- Review the existing implementation and its call sites before making changes.
- Use components and interfaces when adding new features. Avoid hard-coding feature behavior.
- Limit changes to what the request requires and follow the naming and formatting conventions of the surrounding code.
- When changing replication behavior, check the effects on server authority, the owning client, and remote clients.

## Build and Verification

- Run the project build relevant to the changed feature.
- When changing replication behavior, verify the change in a multiplayer environment.
- At the end of the task, summarize the checks you ran and their results. For any checks you could not run, record why.
- Follow the Saved artifact management rules above for separately collected build logs and test results.
