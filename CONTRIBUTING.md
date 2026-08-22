# Contributing to Aegleseeker

First off, thank you for considering contributing to Aegleseeker! It's people like you that make Aegleseeker such a great project.

## Where do I go from here?

If you've noticed a bug or have a feature request, make sure to check the [Issues](../../issues) tab to see if someone else has already created a ticket. If not, go ahead and make one!

## Fork & create a branch

If this is something you think you can fix, then fork Aegleseeker and create a branch with a descriptive name.

A good branch name would be (where issue #325 is the ticket you're working on):

```sh
git checkout -b 325-add-new-feature
```

## Setup Environment

Aegleseeker is built using Visual Studio. You will need:
- Visual Studio (with C++ development workloads installed)
- The project is configured via `AegleDllMSVC.slnx` and `AegleDllMSVC.vcxproj`. Open the solution file in Visual Studio to get started.

## Implementation Guidelines

- Ensure your code adheres to the existing style in the project.
- Write clear, concise commit messages.
- If you're adding a new feature, consider adding relevant tests or updating existing ones.

## Make a Pull Request

At this point, you should switch back to your master branch and make sure it's up to date with Aegleseeker's master branch:

```sh
git remote add upstream git@github.com:AnarchDevelopment/aegledll.git
git checkout master
git pull upstream master
```

Then update your feature branch from your local copy of master, and push it!

```sh
git checkout 325-add-new-feature
git rebase master
git push --set-upstream origin 325-add-new-feature
```

Finally, go to GitHub and make a Pull Request!

## Code Review

Once your pull request is opened, it will be reviewed by the maintainers. They may ask for changes or provide feedback. Please be open to constructive criticism and ready to make updates to your code.

## Code Formatting

Please ensure your code follows standard C++ formatting guidelines and maintains consistency with the rest of the project's codebase. If the project uses a specific formatting tool (like `.clang-format`), make sure to format your code before committing.

## Contact

If you have any questions, need help, or want to discuss ideas, you can reach out through the following channels:
- **Discord**: nqtvyzer
- **GitHub**: [iVyz3r](https://github.com/iVyz3r)
