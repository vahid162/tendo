import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


WORKFLOW = Path(__file__).resolve().parents[1] / ".github/workflows/release.yml"


def workflow_step(name):
    for step in WORKFLOW.read_text().split("      - name: ")[1:]:
        if step.splitlines()[0] == name:
            return step
    raise AssertionError(f"Missing workflow step: {name}")


def step_script(name):
    return textwrap.dedent(workflow_step(name).split("        run: |\n", 1)[1])


def step_enabled(name, outputs):
    condition = next(line.strip()[4:] for line in workflow_step(name).splitlines()
                     if line.strip().startswith("if:"))
    for clause in condition.split(" && "):
        match = re.fullmatch(r"steps\.version\.outputs\.(\w+) (==|!=) '([^']*)'", clause)
        if match is None:
            raise AssertionError(f"Unsupported condition: {condition}")
        field, operator, expected = match.groups()
        equal = outputs.get(field, "") == expected
        if equal != (operator == "=="):
            return False
    return True


class ReleaseWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "outputs"

    def validate(self, branch, version, firmware=None, created="false"):
        (self.root / "VERSION").write_text(version + "\n")
        (self.root / "tando_final_demo_15min.ino").write_text(
            f'#define TANDO_VERSION "{version if firmware is None else firmware}"\n'
        )
        self.output.write_text("")
        script = step_script("Validate release version").replace("${{ github.event.created }}", created)
        result = subprocess.run(
            ["bash", "-c", script], cwd=self.root, capture_output=True, text=True,
            env={**os.environ, "GITHUB_REF_NAME": branch, "GITHUB_OUTPUT": str(self.output)},
        )
        outputs = dict(line.split("=", 1) for line in self.output.read_text().splitlines())
        return result, outputs

    def test_main_rc_validates_but_skips_publication(self):
        result, outputs = self.validate("main", "0.10.0-rc.10")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(outputs, {"skip": "true"})
        self.assertIn("::notice::", result.stdout)

    def test_publishing_channels_keep_exact_versions_and_release_kinds(self):
        for branch, version, prerelease in (
            ("main", "0.10.0", "false"), ("develop", "0.10.0-rc.10", "true"),
        ):
            with self.subTest(branch=branch):
                result, outputs = self.validate(branch, version)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(outputs, {
                    "skip": "false", "version": version, "tag": "v" + version,
                    "prerelease": prerelease,
                })

    def test_invalid_versions_channels_and_mismatches_still_fail(self):
        for branch, version, firmware in (
            ("main", "", None), ("main", "0.10.0-rc", None),
            ("main", "0.10.0-rc.10-extra", None), ("main", "not-a-version", None),
            ("develop", "0.10.0", None), ("develop", "0.10.0-rc", None),
            ("feature/test", "0.10.0-rc.10", None),
            ("main", "0.10.0-rc.10", "0.10.0-rc.9"),
            ("main", "0.10.0", "0.9.0"),
            ("develop", "0.10.0-rc.10", "0.10.0-rc.9"),
        ):
            with self.subTest(branch=branch, version=version, firmware=firmware):
                result, outputs = self.validate(branch, version, firmware)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("::error::", result.stdout)
                self.assertNotEqual(outputs.get("skip"), "false")

    def test_develop_branch_bootstrap_keeps_existing_skip(self):
        result, outputs = self.validate("develop", "0.7.1", created="true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(outputs, {"skip": "true"})

    def test_skip_and_missing_outputs_prevent_all_publication_steps(self):
        steps = (
            "Verify tag is new", "Prepare firmware release asset",
            "Create stable GitHub Release", "Create GitHub Pre-release",
        )
        for outputs in ({}, {"skip": "true"}, {"skip": "true", "prerelease": "false"}):
            with self.subTest(outputs=outputs):
                self.assertEqual([name for name in steps if step_enabled(name, outputs)], [])
        for prerelease, release_step in (
            ("false", "Create stable GitHub Release"), ("true", "Create GitHub Pre-release"),
        ):
            outputs = {"skip": "false", "prerelease": prerelease}
            self.assertEqual([name for name in steps if step_enabled(name, outputs)],
                             ["Verify tag is new", "Prepare firmware release asset", release_step])

    def test_existing_remote_tag_is_rejected_without_moving_it(self):
        def git(*args):
            return subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True, text=True)

        remote = self.root / "remote.git"
        git("init", "--bare", str(remote))
        git("init", ".")
        (self.root / "VERSION").write_text("0.10.0-rc.10\n")
        git("add", "VERSION")
        git("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-m", "fixture")
        git("remote", "add", "origin", str(remote))
        tag = "v0.10.0-rc.10"
        git("tag", tag)
        git("push", "origin", "refs/tags/" + tag)
        before = git("ls-remote", "--tags", "origin").stdout
        script = step_script("Verify tag is new")
        result = subprocess.run(
            ["bash", "-c", script.replace("${{ steps.version.outputs.tag }}", tag)],
            cwd=self.root, capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stdout)
        self.assertEqual(git("ls-remote", "--tags", "origin").stdout, before)
        result = subprocess.run(
            ["bash", "-c", script.replace("${{ steps.version.outputs.tag }}", "v0.10.0-rc.11")],
            cwd=self.root, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
