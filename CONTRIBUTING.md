# Chameleon Ultra Contribution guidelines

Any and all contributions are welcome!

Heres a bit of info and a few guidelines to get you started:

- General
    - Avoid force pushes. Force pushes and "one commit" PRs not only make reviewing more annoying but also erase a significant part of the git history. This, among other things, makes future debugging and bisection a lot harder.
    - Conventional commits. It is recommended to follow the [conventional commit](https://www.conventionalcommits.org/en/v1.0.0/) pattern when it comes to commit messages. While this is not strictly enforced, its highly recommended and a good habbit.
    - Atomic PRs. To help keep an overview and avoid conflicts, it is highly encouraged to file Atomic PRs. Atomic PRs are:
        - Focused Scope: It targets a single, well-defined change, making it easier to understand and review.
        - Minimal Size: It contains only the necessary code modifications to achieve its goal, avoiding unrelated changes.
        - Independent: It should be able to stand on its own without depending on other unmerged PRs, reviewed, and merged independently.
        - Self-Tested: each PR should include an appropriate set of unit tests that tests the changes. (optional but highly appreciated)
    - Atomic Commits. Similar thing as atomic PRs. When you are done with a feature, commit. Made a working change? commit. Git commits are basically free. Doing frequent commits at sensible points throughout development not only helps you keep track of progress but also saves progress and changes so you can revert when something goes wrong. It also helps when debugging and bisecting as more granular commits allow for easier issue location.

- CLI
    - The recommended packagemanager is [UV](https://docs.astral.sh/uv/) (from astralsh). You may use the manager of your choice, but when adding new dependencies they must be added to the UV lock file and pyproject toml as well.
    - Type safety is important. The CLI should be typesafe. Python 3.9+ offer a wide variety of type declarations. Metas [pyrefly](https://pyrefly.org/) is used to do type validation. It is recommended to install the appropriate vscode extension and check your types before opening a PR.
    - Formatting matters. Mostly. While pixelpeeping and exact rules are annoying and unnescesary, format your code in a readable and logical way. [Ruff](https://docs.astral.sh/ruff/) is used to enforce various formatting rules. You may install the Ruff vscode extension or use the CLI to format before opening a PR.
    - Avoid extra packages. Almost everyone knows the "meme" of the javascript ["is-even"](https://www.npmjs.com/package/is-even) package. While it is encouraged and makes sense to use packages where appropriate, just installing packages for the hell of it even if its a 2 liner is not sensible.

## Feature freeze and review cycle

Pull requests are reviewed in batches so maintainers and testers can focus on a manageable set of changes. The `feature freeze` label marks the PRs in the current batch. This does not mean that you can not make new Pull requests or contributions.

The cycle works as follows:

1. At the start of a cycle, every open PR that is ready for review is given the `feature freeze` label. Draft PRs are not included.
2. Maintainers and contributors focus their review and testing on the labeled PRs. These PRs are then merged, closed, or converted back to drafts as appropriate.
3. PRs opened or marked ready after the batch starts normally wait for the next cycle. Maintainers may make exceptions when necessary.
4. Once no open, ready PRs remain in the current batch, a release is made and the next cycle begins. All PRs that are ready at that point enter the new batch.

If a PR in the current batch is converted to a draft, it leaves that batch. Marking it ready again does not automatically add it back to the current batch. It may be included in the next one.

### Inactive drafts

Draft PRs are welcome and the ideal solution while work is in progress and you may want to gather early feedback. To prevent stale PRs from cluttering up the list, inactive draft PRs will follow the following process:

- After 30 days without activity, a bot asks whether there is still interest in completing the PR.
- After another 14 days without activity, the PR receives the `close soon` label.
- After 30 more days without activity, the draft is closed. It can be reopened if development resumes.

A new commit, an edit to the PR, or a new comment resets the inactivity countdown. An author can also reply to the bot with `no`, `no longer interested`, `please close`, or `/close-soon` to move the PR directly to the `close soon` stage.