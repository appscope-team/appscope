# Contribution Guidelines

Contribution to the project is very welcome and always needed. Please see the below guidelines for contributing to the project. If you have more questions, please feel free to join us on [slack](https://cribl.io/community/#form).

## :exclamation: Licensing

The Apache license (4a) requires that the original top level LICENSE file is preserved, but names can be added to the copyright owners list in that file. Copyright is automatic, but idenfifying key contributors is useful.

> Summary: In the LICENCE file, add names if you wish to; Never remove anything.

In addition, the license requires (4b) that all files changed carry a notice stating that You made a change.  The general method used to state that a file was changed is to add a new copyright line under the original one. 

> Summary: If you are changing a source code file you Must add or update this notice:

```
/*
 * (c) 2021 Cribl, Inc
 * (c) <Years of contribution to This file> Your Name
 */
```

The license also requires that all existing copyright notices are preserved.

## Github

### Issues

Should always contain:
- Issue detail
- Resolution detail

__Please tag issues appropriately for tracking purposes.__

### Pull Requests

Should contain:
- QA Instructions 
- ...anything else you find useful i.e. notes, todo list
- ...maybe some comments from the review process

### Discussions

Can contain:
- Proposals
- Ideas
- Q&A

### Projects

We try to keep our project boards updated to reflect the current workload and priorities.

### Security

Only AppScope team members can approve and merge PR's.

AppScope team members must have MFA on their GitHub account ; and use a GPG key to sign commits. 

## Coding Style

TBD - Please see the open [discussion](https://github.com/criblio/appscope/discussions/1245) on coding style.

## Testing

Every PR should be tested in some way, sometimes by a manual QA reviewer, but preferably by an extension to integration or unit tests.

Every PR must pass the GitHub Actions CI pipeline 100%, which includes build, unit test, and integration test steps.

### Integration Tests

We use these to test features and should be the focal point of testing. 

Integration tests live in `test/integration/`.

### Unit Tests

We use these to test subsets of features where appropriate.

Unit Tests for the C code live in `test/unit/`.

Unit Tests for the Go code live in `cli/__package__/`.

## Documentation

With every release, we should update user documentation. User documentation lives in the `website` directory, and is published to [appscope.dev](https://appscope.dev).

## Compatibility

Please keep in mind the current set of [limitations and requirements](https://appscope.dev/docs/requirements) for using AppScope. We want to ensure that we maintain or shrink these where possible, and avoid growing them.
For example, we want to AppScope to work on all popular linux distributions and versions; on glibc and musl environments ; with apps written in any language, etc.

We want to maintain backwards compatibility with previous versions of AppScope in almost all cases.

We want to maintain our current licensing terms found in `LICENSE`.

