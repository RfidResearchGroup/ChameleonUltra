const FEATURE_FREEZE_LABEL = "feature freeze";
const CLOSE_SOON_LABEL = "close soon";
const INACTIVITY_MARKER = "<!-- chameleon-ultra-draft-inactivity:";

const LABELS = {
  [FEATURE_FREEZE_LABEL]: {
    color: "1d76db",
    description: "Included in the current review and release batch",
  },
  [CLOSE_SOON_LABEL]: {
    color: "d73a4a",
    description: "Will be closed unless development resumes",
  },
};

const DAY = 24 * 60 * 60 * 1000;

module.exports = async ({ github, context, core, mode }) => {
  const { owner, repo } = context.repo;
  let knownLabels;

  const daysSince = (timestamp) =>
    (Date.now() - new Date(timestamp).getTime()) / DAY;

  const hasLabel = (pull, name) =>
    pull.labels.some((label) => label.name.toLowerCase() === name.toLowerCase());

  async function ensureLabel(name) {
    if (!knownLabels) {
      const labels = await github.paginate(github.rest.issues.listLabelsForRepo, {
        owner,
        repo,
        per_page: 100,
      });
      knownLabels = new Set(labels.map((label) => label.name.toLowerCase()));
    }

    if (knownLabels.has(name.toLowerCase())) {
      return;
    }

    try {
      await github.rest.issues.createLabel({ owner, repo, name, ...LABELS[name] });
      knownLabels.add(name.toLowerCase());
      core.info(`Created the "${name}" label`);
    } catch (error) {
      // Two simultaneous runs may both notice that a label is absent.
      if (error.status !== 422) throw error;
      knownLabels.add(name.toLowerCase());
    }
  }

  async function addLabel(pullNumber, name) {
    await ensureLabel(name);
    await github.rest.issues.addLabels({
      owner,
      repo,
      issue_number: pullNumber,
      labels: [name],
    });
  }

  async function removeLabel(pull, name) {
    if (!hasLabel(pull, name)) return;

    try {
      await github.rest.issues.removeLabel({
        owner,
        repo,
        issue_number: pull.number,
        name,
      });
    } catch (error) {
      if (error.status !== 404) throw error;
    }
  }

  async function inactivityComments(pullNumber) {
    const comments = await github.paginate(github.rest.issues.listComments, {
      owner,
      repo,
      issue_number: pullNumber,
      per_page: 100,
    });

    return comments.filter(
      (comment) =>
        comment.user?.login === "github-actions[bot]" &&
        comment.body?.includes(INACTIVITY_MARKER),
    );
  }

  async function clearDraftCountdown(pull) {
    const hadCloseSoon = hasLabel(pull, CLOSE_SOON_LABEL);
    await removeLabel(pull, CLOSE_SOON_LABEL);
    const comments = await inactivityComments(pull.number);

    for (const comment of comments) {
      await github.rest.issues.deleteComment({
        owner,
        repo,
        comment_id: comment.id,
      });
    }

    if (comments.length || hadCloseSoon) {
      core.info(`Reset the inactivity countdown for #${pull.number}`);
    }
  }

  function warningBody(warningDays, responseDays) {
    return [
      `${INACTIVITY_MARKER}warning -->`,
      "This draft pull request has not had activity for a while. Are you still interested in finishing it?",
      "",
      `Any new commit, PR edit, or conversation comment will reset this timer. If there is no activity for another ${responseDays} days, the \`${CLOSE_SOON_LABEL}\` label will be added.`,
      "",
      "If you are no longer interested, reply with `no`, `no longer interested`, `please close`, or `/close-soon`.",
      "",
      `<sub>This reminder was posted after ${warningDays} days of inactivity.</sub>`,
    ].join("\n");
  }

  function closeSoonBody(closeDays) {
    return [
      `${INACTIVITY_MARKER}close-soon -->`,
      `This draft pull request is now marked \`${CLOSE_SOON_LABEL}\` because no continuing interest was confirmed.`,
      "",
      `It will be closed after ${closeDays} more days without activity. A new commit, PR edit, or conversation comment will cancel the countdown.`,
    ].join("\n");
  }

  async function markCloseSoon(pull, comment, closeDays) {
    await addLabel(pull.number, CLOSE_SOON_LABEL);
    const body = closeSoonBody(closeDays);

    if (comment) {
      await github.rest.issues.updateComment({
        owner,
        repo,
        comment_id: comment.id,
        body,
      });
    } else {
      await github.rest.issues.createComment({
        owner,
        repo,
        issue_number: pull.number,
        body,
      });
    }

    core.info(`Marked draft #${pull.number} as "${CLOSE_SOON_LABEL}"`);
  }

  if (mode === "start-feature-freeze") {
    await ensureLabel(FEATURE_FREEZE_LABEL);
    await ensureLabel(CLOSE_SOON_LABEL);

    const pulls = await github.paginate(github.rest.pulls.list, {
      owner,
      repo,
      state: "open",
      per_page: 100,
    });
    const readyPulls = pulls.filter((pull) => !pull.draft);

    for (const pull of readyPulls) {
      await addLabel(pull.number, FEATURE_FREEZE_LABEL);
      await removeLabel(pull, CLOSE_SOON_LABEL);
    }

    core.summary
      .addHeading("Feature-freeze batch started")
      .addRaw(
        readyPulls.length
          ? `Labeled ${readyPulls.length} open, non-draft pull request(s): ${readyPulls.map((pull) => `#${pull.number}`).join(", ")}.`
          : "There were no open, non-draft pull requests to label.",
      );
    await core.summary.write();
    return;
  }

  if (mode === "pull-activity") {
    const pull = context.payload.pull_request;
    const action = context.payload.action;

    // A converted draft has left the current batch. A reopened PR waits for the next batch rather than silently rejoining its old one.
    if (action === "converted_to_draft" || action === "reopened") {
      await removeLabel(pull, FEATURE_FREEZE_LABEL);
    }

    if (pull.draft || action === "ready_for_review" || action === "reopened") {
      await clearDraftCountdown(pull);
    }
    return;
  }

  if (mode === "discussion-activity") {
    if (context.payload.issue && !context.payload.issue.pull_request) return;

    const pullNumber = context.payload.issue?.number ?? context.payload.pull_request?.number;
    if (!pullNumber || context.actor.endsWith("[bot]")) return;

    const { data: pull } = await github.rest.pulls.get({
      owner,
      repo,
      pull_number: pullNumber,
    });
    if (!pull.draft) return;

    const comments = await inactivityComments(pull.number);
    const activeComment = comments.at(-1);
    if (!activeComment) return;

    const reply = context.payload.comment?.body?.trim() ?? "";
    const isAuthorReply = context.actor === pull.user.login;
    const isNegativeReply = /^(?:no|no thanks|not anymore|no longer interested|please close(?: this)?|close (?:it|this|this pr)|\/close-soon)[\s.!]*$/i.test(reply);

    if (isAuthorReply && isNegativeReply) {
      const closeDays = Number(process.env.DRAFT_CLOSE_DAYS);
      await markCloseSoon(pull, activeComment, closeDays);
      return;
    }

    await clearDraftCountdown(pull);
    return;
  }

  if (mode === "scan-drafts") {
    const warningDays = Number(process.env.DRAFT_WARNING_DAYS);
    const responseDays = Number(process.env.DRAFT_RESPONSE_DAYS);
    const closeDays = Number(process.env.DRAFT_CLOSE_DAYS);

    if (![warningDays, responseDays, closeDays].every(Number.isFinite)) {
      core.setFailed("Draft inactivity periods must all be numbers");
      return;
    }

    await ensureLabel(CLOSE_SOON_LABEL);
    const pulls = await github.paginate(github.rest.pulls.list, {
      owner,
      repo,
      state: "open",
      per_page: 100,
    });

    for (const pull of pulls.filter((candidate) => candidate.draft)) {
      const comments = await inactivityComments(pull.number);
      const activeComment = comments.at(-1);
      const phase = activeComment?.body?.includes(`${INACTIVITY_MARKER}close-soon`)
        ? "close-soon"
        : activeComment?.body?.includes(`${INACTIVITY_MARKER}warning`)
          ? "warning"
          : undefined;

      // Honor a close-soon label applied manually by starting the final timer.
      if (hasLabel(pull, CLOSE_SOON_LABEL) && phase !== "close-soon") {
        await markCloseSoon(pull, activeComment, closeDays);
        continue;
      }

      // Removing the label manually cancels the final countdown.
      if (phase === "close-soon" && !hasLabel(pull, CLOSE_SOON_LABEL)) {
        await github.rest.issues.deleteComment({
          owner,
          repo,
          comment_id: activeComment.id,
        });
        continue;
      }

      if (!activeComment && daysSince(pull.updated_at) >= warningDays) {
        await github.rest.issues.createComment({
          owner,
          repo,
          issue_number: pull.number,
          body: warningBody(warningDays, responseDays),
        });
        core.info(`Warned inactive draft #${pull.number}`);
        continue;
      }

      if (phase === "warning" && daysSince(activeComment.created_at) >= responseDays) {
        await markCloseSoon(pull, activeComment, closeDays);
        continue;
      }

      if (phase === "close-soon" && daysSince(activeComment.updated_at) >= closeDays) {
        await github.rest.issues.updateComment({
          owner,
          repo,
          comment_id: activeComment.id,
          body: [
            `${INACTIVITY_MARKER}closed -->`,
            `Closing this draft pull request after the \`${CLOSE_SOON_LABEL}\` waiting period expired. It can be reopened if development resumes.`,
          ].join("\n"),
        });
        await github.rest.pulls.update({
          owner,
          repo,
          pull_number: pull.number,
          state: "closed",
        });
        core.info(`Closed inactive draft #${pull.number}`);
      }
    }
  }
};
