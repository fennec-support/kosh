unset KOSH_FLAGS
BIN=$(cd "$(dirname "$BIN")" && pwd -P)/$(basename "$BIN")
start_directory=$(pwd -P)
root=$TEST_TEMP_DIRECTORY/lint-embedded-sources
mkdir -p "$root/.github/workflows" "$root/.gitea/workflows" \
  "$root/.forgejo/workflows" "$root/.circleci" "$root/.buildkite" \
  "$root/.woodpecker" "$root/.vscode" "$root/.devcontainer" "$root/recipes"
root=$(cd "$root" && pwd -P)
trap 'cd "$start_directory" && [ -n "$root" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$root"' EXIT

cat > "$root/.github/workflows/check.yml" <<'EOF'
name: check
on: push
jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - shell: bash
        run: |
          printf '%s\n' "$EMBEDDED_GITHUB"
          awk 'BEGIN { print "$10" }'
      - shell: pwsh
        run: |
          Write-Output "$POWERSHELL_HOST"
      - run: target-only-ci-command
EOF

cat > "$root/README.md" <<'EOF'
# Example

The prose contains $MARKDOWN_PROSE.

```bash
printf '%s\n' "$MARKDOWN_EMBEDDED"
missing_markdown_executable
```
EOF

cat > "$root/Containerfile" <<'EOF'
FROM alpine
SHELL ["/bin/bash", "-c"]
RUN printf '%s\n' "$CONTAINER_EMBEDDED"
RUN missing_container_target_executable
EOF

cat > "$root/Makefile" <<'EOF'
SHELL := /bin/bash
OBJECTS = \
	MAKE_CONTINUATION_TOKEN \
	MAKE_CONTINUATION_SECOND
SHELL_RESULT := $(shell printf '%s\n' "$$MAKE_SHELL_EMBEDDED"; missing_make_shell_executable)
# COMMENT_RESULT := $(shell missing_make_comment_executable)
define TEMPLATE
	missing_make_define_executable
endef
all:
	@for item in one; do \
	  printf '%s\n' "$$item"; \
	done
	$(MAKE) -C subdir
	printf '%s\n' "$$MAKE_EMBEDDED" "$(MAKE_ONLY)" "$@"
	missing_make_executable
inline: ; printf '%s\n' "$$MAKE_INLINE_EMBEDDED"; missing_make_inline_executable
EOF

cat > "$root/package.json" <<'EOF'
{
  "name": "embedded-test",
  "metadata": { "scripts": { "ignored": "echo nested" } },
  "scripts": {
    "check": "printf '%s\\n' \"$PACKAGE_EMBEDDED\"; missing_package_executable"
  }
}
EOF

cat > "$root/.gitea/workflows/check.yml" <<'EOF'
steps:
  - run: 'printf "%s\n" "$EMBEDDED_GITEA"'
EOF

cat > "$root/.forgejo/workflows/check.yml" <<'EOF'
steps:
  - run: 'printf "%s\n" "$EMBEDDED_FORGEJO"'
EOF

cat > "$root/.gitlab-ci.yml" <<'EOF'
job:
  script:
    - printf '%s\n' "$EMBEDDED_GITLAB"
EOF

cat > "$root/playbook.yml" <<'EOF'
- hosts: all
  tasks:
    - ansible.builtin.shell: printf '%s\n' "$ANSIBLE_EMBEDDED"
EOF

cat > "$root/compose.yml" <<'EOF'
services:
  app:
    image: alpine
    command: printf '%s\n' "$COMPOSE_EMBEDDED"
EOF

cat > "$root/cloudbuild.yaml" <<'EOF'
steps:
  - script: printf '%s\n' "$CLOUD_BUILD_EMBEDDED"
EOF

cat > "$root/.circleci/config.yml" <<'EOF'
version: 2.1
jobs:
  check:
    steps:
      - run: printf '%s\n' "$EMBEDDED_CIRCLE"
EOF

cat > "$root/azure-pipelines.yml" <<'EOF'
steps:
  - bash: printf '%s\n' "$AZURE_EMBEDDED"
EOF

cat > "$root/bitbucket-pipelines.yml" <<'EOF'
pipelines:
  default:
    - step:
        script:
          - printf '%s\n' "$EMBEDDED_BITBUCKET"
EOF

cat > "$root/.buildkite/pipeline.yml" <<'EOF'
steps:
  - command: printf '%s\n' "$EMBEDDED_BUILDKITE"
EOF

cat > "$root/.travis.yml" <<'EOF'
script: printf '%s\n' "$EMBEDDED_TRAVIS"
EOF

cat > "$root/pod.yml" <<'EOF'
apiVersion: v1
kind: Pod
spec:
  containers:
    - command: ["/bin/sh", "-c"]
      args: |
        printf '%s\n' "$KUBERNETES_EMBEDDED"
EOF

cat > "$root/.drone.yml" <<'EOF'
kind: pipeline
steps:
  - commands:
      - printf '%s\n' "$EMBEDDED_DRONE"
EOF

cat > "$root/.woodpecker/check.yml" <<'EOF'
steps:
  check:
    commands:
      - printf '%s\n' "$WOODPECKER_EMBEDDED"
EOF

cat > "$root/Taskfile.yml" <<'EOF'
version: '3'
tasks:
  check:
    cmds:
      - printf '%s\n' "$TASKFILE_EMBEDDED"
      - missing_taskfile_executable
EOF

cat > "$root/justfile" <<'EOF'
check:
  printf '%s\n' "$JUSTFILE_EMBEDDED"
  missing_justfile_executable
EOF

cat > "$root/check.spec" <<'EOF'
Name: check
Version: 1
Release: 1
%check
printf '%s\n' "$RPM_EMBEDDED"
%files
EOF

cat > "$root/.vscode/tasks.json" <<'EOF'
{"version":"2.0","label":"command","other":"missing_vscode_value_executable","tasks":[{"type":"shell","command":"printf '%s\\n' \"$VSCODE_EMBEDDED\"; missing_vscode_executable"}]}
EOF

cat > "$root/.devcontainer/devcontainer.json" <<'EOF'
{
  "postCreateCommand":"printf '%s\\n' \"$DEV_CONTAINER_EMBEDDED\"; missing_dev_target_executable",
  "waitFor":"missing_dev_wait_executable",
  "initializeCommand":"missing_dev_host_executable"
}
EOF

cat > "$root/Jenkinsfile" <<'EOF'
printf '%s\n' "$JENKINS_HOST"
EOF

cat > "$root/check.tf" <<'EOF'
printf '%s\n' "$TERRAFORM_HOST"
EOF

cat > "$root/check.nix" <<'EOF'
printf '%s\n' "$NIX_HOST"
EOF

count_variable()
{
  name=$1
  file=$2
  output=$("$BIN" --lint --no-traces "$file" 2>&1)
  printf '%s' "$output" | grep -c "The variable '$name'"
}

count_variable_and_missing_executable()
{
  variable_name=$1
  command_name=$2
  file=$3
  output=$("$BIN" --lint --no-traces "$file" 2>&1)
  variable_count=$(printf '%s' "$output" | \
    grep -c "The variable '$variable_name'")
  command_count=$(printf '%s' "$output" | \
    grep -c "The command '$command_name' was not found")
  printf '%s %s' "$variable_count" "$command_count"
}

github=$(count_variable EMBEDDED_GITHUB "$root/.github/workflows/check.yml")
github_output=$("$BIN" --lint --no-traces \
  "$root/.github/workflows/check.yml" 2>&1)
github_host=$(printf '%s' "$github_output" | \
  grep -Ec 'POWERSHELL_HOST|target-only-ci-command|positional parameter|Expected a command before the pipe')
set -- $(count_variable_and_missing_executable MARKDOWN_EMBEDDED \
  missing_markdown_executable "$root/README.md")
markdown=$1
markdown_missing=$2
container_output=$("$BIN" --lint --no-traces "$root/Containerfile" 2>&1)
container=$(printf '%s' "$container_output" | \
  grep -c "The variable 'CONTAINER_EMBEDDED'")
container_target_missing=$(printf '%s' "$container_output" | \
  grep -c "The command 'missing_container_target_executable' was not found")
make_output=$("$BIN" --lint --no-traces "$root/Makefile" 2>&1)
makefile=$(printf '%s' "$make_output" | \
  grep -c "The variable 'MAKE_EMBEDDED'")
make_missing=$(printf '%s' "$make_output" | \
  grep -c "The command 'missing_make_executable' was not found")
make_inline=$(printf '%s' "$make_output" | \
  grep -c "The variable 'MAKE_INLINE_EMBEDDED'")
make_inline_missing=$(printf '%s' "$make_output" | \
  grep -c "The command 'missing_make_inline_executable' was not found")
make_shell=$(printf '%s' "$make_output" | \
  grep -c "The variable 'MAKE_SHELL_EMBEDDED'")
make_shell_missing=$(printf '%s' "$make_output" | \
  grep -c "The command 'missing_make_shell_executable' was not found")
make_host=$(printf '%s\n' "$make_output" | grep -E '^[^ ].*: (warning|error): ' | \
  grep -Ec "MAKE_ONLY|MAKE_CONTINUATION|missing_make_comment_executable|missing_make_define_executable|positional parameter|for loop has no")
set -- $(count_variable_and_missing_executable PACKAGE_EMBEDDED \
  missing_package_executable "$root/package.json")
package=$1
package_missing=$2
gitea=$(count_variable EMBEDDED_GITEA "$root/.gitea/workflows/check.yml")
forgejo=$(count_variable EMBEDDED_FORGEJO "$root/.forgejo/workflows/check.yml")
gitlab=$(count_variable EMBEDDED_GITLAB "$root/.gitlab-ci.yml")
ansible=$(count_variable ANSIBLE_EMBEDDED "$root/playbook.yml")
compose=$(count_variable COMPOSE_EMBEDDED "$root/compose.yml")
cloud_build=$(count_variable CLOUD_BUILD_EMBEDDED "$root/cloudbuild.yaml")
circle=$(count_variable EMBEDDED_CIRCLE "$root/.circleci/config.yml")
azure=$(count_variable AZURE_EMBEDDED "$root/azure-pipelines.yml")
bitbucket=$(count_variable EMBEDDED_BITBUCKET "$root/bitbucket-pipelines.yml")
buildkite=$(count_variable EMBEDDED_BUILDKITE "$root/.buildkite/pipeline.yml")
travis=$(count_variable EMBEDDED_TRAVIS "$root/.travis.yml")
kubernetes=$(count_variable KUBERNETES_EMBEDDED "$root/pod.yml")
drone=$(count_variable EMBEDDED_DRONE "$root/.drone.yml")
woodpecker=$(count_variable WOODPECKER_EMBEDDED "$root/.woodpecker/check.yml")
set -- $(count_variable_and_missing_executable TASKFILE_EMBEDDED \
  missing_taskfile_executable "$root/Taskfile.yml")
taskfile=$1
taskfile_missing=$2
set -- $(count_variable_and_missing_executable JUSTFILE_EMBEDDED \
  missing_justfile_executable "$root/justfile")
justfile=$1
justfile_missing=$2
rpm=$(count_variable RPM_EMBEDDED "$root/check.spec")
vscode_output=$("$BIN" --lint --no-traces "$root/.vscode/tasks.json" 2>&1)
vscode=$(printf '%s' "$vscode_output" | \
  grep -c "The variable 'VSCODE_EMBEDDED'")
vscode_missing=$(printf '%s' "$vscode_output" | \
  grep -c "The command 'missing_vscode_executable' was not found")
vscode_value_missing=$(printf '%s' "$vscode_output" | \
  grep -c "The command 'missing_vscode_value_executable' was not found")
dev_container_output=$("$BIN" --lint --no-traces \
  "$root/.devcontainer/devcontainer.json" 2>&1)
dev_container=$(printf '%s' "$dev_container_output" | \
  grep -c "The variable 'DEV_CONTAINER_EMBEDDED'")
dev_host_missing=$(printf '%s' "$dev_container_output" | \
  grep -c "The command 'missing_dev_host_executable' was not found")
dev_target_missing=$(printf '%s' "$dev_container_output" | \
  grep -c "The command 'missing_dev_target_executable' was not found")
dev_wait_missing=$(printf '%s' "$dev_container_output" | \
  grep -c "The command 'missing_dev_wait_executable' was not found")
jenkins=$(count_variable JENKINS_HOST "$root/Jenkinsfile")
terraform=$(count_variable TERRAFORM_HOST "$root/check.tf")
nix=$(count_variable NIX_HOST "$root/check.nix")

printf 'github=%s markdown=%s container=%s make=%s package=%s\n' \
  "$github" "$markdown" "$container" "$makefile" "$package"
printf 'github-host=%s\n' "$github_host"
printf 'make-host=%s\n' "$make_host"
printf 'make-inline=%s make-inline-missing=%s\n' \
  "$make_inline" "$make_inline_missing"
printf 'make-shell=%s make-shell-missing=%s\n' \
  "$make_shell" "$make_shell_missing"
printf 'gitea=%s forgejo=%s gitlab=%s ansible=%s compose=%s cloud-build=%s\n' \
  "$gitea" "$forgejo" "$gitlab" "$ansible" "$compose" "$cloud_build"
printf 'circle=%s azure=%s bitbucket=%s buildkite=%s travis=%s kubernetes=%s\n' \
  "$circle" "$azure" "$bitbucket" "$buildkite" "$travis" "$kubernetes"
printf 'drone=%s woodpecker=%s taskfile=%s justfile=%s rpm=%s\n' \
  "$drone" "$woodpecker" "$taskfile" "$justfile" "$rpm"
printf 'vscode=%s dev-container=%s jenkins=%s terraform=%s nix=%s\n' \
  "$vscode" "$dev_container" "$jenkins" "$terraform" "$nix"
printf 'missing markdown=%s make=%s package=%s taskfile=%s justfile=%s vscode=%s\n' \
  "$markdown_missing" "$make_missing" "$package_missing" \
  "$taskfile_missing" "$justfile_missing" "$vscode_missing"
printf 'target container=%s dev-lifecycle=%s dev-wait=%s dev-host=%s\n' \
  "$container_target_missing" "$dev_target_missing" "$dev_wait_missing" \
  "$dev_host_missing"
printf 'json-value-key=%s\n' "$vscode_value_missing"

cat > "$root/.github/workflows/template.yml" <<'EOF'
jobs:
  tag:
    steps:
      - run: |
          docker tag ${{ needs.build.outputs.image }} target; if true; then
EOF
template_output=$("$BIN" --lint --no-traces \
  "$root/.github/workflows/template.yml" 2>&1)
template_excerpt_count=$(printf '%s\n' "$template_output" |
  grep -c 'docker tag \${{ needs.build.outputs.image }} target')
template_warning_count=$(printf '%s\n' "$template_output" | grep -c 'warning:')
printf 'template-excerpt=%s template-warnings=%s\n' \
  "$template_excerpt_count" "$template_warning_count"

cat > "$root/recipes/justfile" <<'EOF'
set shell := ["bash", "-uc"]

bin := ".venv" / "bin"

[private]
setup arg="x": (other "a:b")
    if [ ! -x {{ bin }}/tool ]; then \
        uv venv {{ bin }} && \
        echo "{{ arg }}"; \
    fi
    {{ bin }}/tool --check
    @-rm -f stale

other value:
    if true; then echo "{{ value }}"
    missing_recipe_executable

[script("python3")]
python:
    print("a" if True else "b")

script:
    #!/usr/bin/env bash
    x=$(echo {{ bin }})
    echo "$x"
EOF
cat > "$root/recipes/tools.just" <<'EOF'
build:
    missing_module_executable
EOF
cat > "$root/recipes/uci.sh.j2" <<'EOF'
#!/bin/sh
{# vlans #}
{% for vlan in vlans %}
uci set network.{{ vlan.name }}_dev.vid='{{ vlan.id }}'
{% endfor %}
uci commit network
EOF
cat > "$root/recipes/nginx.conf.j2" <<'EOF'
server { listen {{ port }}; {% if tls %}ssl on;{% endif %} }
EOF
cat > "$root/recipes/broken.sh.j2" <<'EOF'
#!/bin/sh
case {{ value }} in
EOF
for template_file in justfile tools.just uci.sh.j2 broken.sh.j2; do
  "$BIN" --lint --no-traces "$root/recipes/$template_file" 2>&1 |
    grep -E '^[^ ].*: (error|warning): ' | sed "s|^$root/||"
done
nginx_template=$("$BIN" --lint --no-traces "$root/recipes/nginx.conf.j2" 2>&1 |
  grep -c 'is not recognized')
printf 'non-shell-template-unrecognized=%s\n' "$nginx_template"

mkdir -p "$root/shapes/.github/workflows"
cat > "$root/shapes/.gitlab-ci.yml" <<'EOF'
.base: &base
  script:
  - echo base
job:
  before_script: # setup
  - |
    if true; then
      echo ok
    fi
  - >
    echo folded
    && echo continued
  - !reference [.base, script]
  script: ["echo one", "echo two"]
  after_script:
  - if true; then
  variables:
    A: b
EOF
cat > "$root/shapes/.github/workflows/ci.yml" <<'EOF'
on: push
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - run: |
          cat <<EOF > out.txt
          hello ${{ github.sha }}
          EOF
          echo "${{ secrets.TOKEN }}" > token
        if: always()
      - run: case $x in
EOF
cat > "$root/shapes/compose.yaml" <<'EOF'
services:
  app:
    image: x
    command: ["python", "app.py", "--flag"]
    healthcheck:
      test: ["CMD-SHELL", "curl -f http://localhost || exit 1"]
  worker:
    image: x
    entrypoint: ["sh", "-c"]
    command: ["if true; then"]
EOF
cat > "$root/shapes/pod.yaml" <<'EOF'
apiVersion: v1
kind: Pod
spec:
  containers:
  - name: a
    command: ["/bin/sh", "-c"]
    args:
      - |
        echo start
        while true; do
  - name: b
    args: ["--config=/etc/x", "-c", "value"]
EOF
cat > "$root/shapes/azure-pipelines.yml" <<'EOF'
steps:
- script: |
    echo $(Build.BuildId) ${{ parameters.name }} $[ variables.x ]
    echo "$(date)"
  displayName: show
- bash: if true; then
EOF
for shape_file in .gitlab-ci.yml .github/workflows/ci.yml compose.yaml pod.yaml azure-pipelines.yml; do
  (cd "$root/shapes" && "$BIN" --lint --no-traces "$shape_file" 2>&1) |
    grep -E '^[^ ].*: (error|warning): '
done

cases="$root/cases"
mkdir -p "$cases/.github/workflows" "$cases/roles/web/tasks" \
  "$cases/.devcontainer" "$cases/.vscode"
cat > "$cases/.github/workflows/shells.yml" <<'EOF'
defaults:
  run:
    shell: pwsh
jobs:
  win:
    runs-on: ubuntu-latest
    steps:
      - run: if ($x) { Write-Host hi }
      - run: if true; then
        shell: bash
      - run: Get-Item | Where { $_ }
EOF
cat > "$cases/roles/web/tasks/main.yml" <<'EOF'
- name: bash task
  shell: |
    [[ -f /etc/x ]] && echo ok
  args:
    executable: /bin/bash
- name: posix task
  ansible.builtin.shell:
    cmd: if true; then
    chdir: /tmp
EOF
printf '%s\n' '.ONESHELL:' 'build: SHELL := /bin/bash' 'build:' \
  '	if [[ -n x ]]; then' '	  echo yes' '	fi' 'plain:' '	[[ -n x ]]' \
  > "$cases/Makefile.inc"
cat > "$cases/pkg.spec.in" <<'EOF'
%prep
%setup -q
if true; then
%conf
%configure
%post -p <lua>
if posix.stat("/x") then end
EOF
cat > "$cases/guide.mdx" <<'EOF'
```console
$ npm install
added 1 package
$ if true; then
```

> ```sh title="a.sh"
> cp <file> <dest>
> if true; then
> ```
EOF
mkdir -p "$cases/just"
cat > "$cases/just/justfile" <<'EOF'
set shell := ['/usr/bin/env', 'bash', '-c']
set export
name := "x"

[windows]
w:
    if ($true) { echo hi }

[confirm("sure?"), script("python3")]
p:
    if True: pass

a target:
    [[ -n $name$target ]] && echo "{{ "}}" }}"
    if true; then
EOF
printf '%s\n' '#!/bin/sh -eu' '[[ -n x ]]' > "$cases/run.j2"
cat > "$cases/package.json" <<'EOF'
{"scripts": {"a": "echo \u0026\u0026 true", "b": "if true; then"}}
EOF
cat > "$cases/.devcontainer/devcontainer.jsonc" <<'EOF'
{
  // "postCreateCommand": "if true; then",
  "postAttachCommand": {"a": "echo ${containerWorkspaceFolder}", "b": "if true; then"},
  "customizations": {"vscode": {"settings": {"postStartCommand": "if true; then"}}},
  "onCreateCommand": ["sh", "-c", "case x in"]
}
EOF
cat > "$cases/.vscode/tasks.json" <<'EOF'
{
  "tasks": [
    {"label": "a", "command": "echo ${workspaceFolder:sub} ${env:HOME}",
     "windows": {"command": "dir C:\\x"}},
    {"label": "b", "command": "if true; then"}
  ],
  "inputs": [{"id": "x", "type": "command", "command": "ext.pick"}]
}
EOF
cat > "$cases/Dockerfile-dev" <<'EOF'
FROM alpine
ARG VERSION=1
RUN --mount=type=cache,target=/c echo "$VERSION"; \
    # comment inside the instruction
    if true; then
HEALTHCHECK --interval=5s CMD case x in
ONBUILD RUN while true; do
CMD ["sh", "-c", "until false; do"]
RUN [ -f /x ] && echo found
RUN <<'EOT' bash
[[ -n x ]] && echo ok
EOT
SHELL ["powershell", "-Command"]
RUN if ($x) { }
EOF
cat > "$cases/azure-pipelines-ci.yml" <<'EOF'
steps:
- task: PowerShell@2
  inputs:
    script: |
      if ($true) {
- task: Bash@3
  inputs:
    script: if true; then
EOF
cat > "$cases/.drone.yaml" <<'EOF'
kind: pipeline
steps:
- commands:
  - if true; then
---
kind: pipeline
platform: {os: windows}
steps:
- commands:
  - if ($x) { }
EOF
cat > "$cases/cloudbuild-prod.yaml" <<'EOF'
steps:
- name: ubuntu
  entrypoint: bash
  args: ["-c", "echo $$HOME $_REGION $PROJECT_ID; if true; then"]
EOF
cat > "$cases/.gitlab-ci.yaml" <<'EOF'
job:
  script:
    - echo "$CI_COMMIT_SHA"
    - if true; then
EOF
cat > "$cases/compose.override.yml" <<'EOF'
services:
  app:
    command: "case x in"
EOF
for case_file in .github/workflows/shells.yml roles/web/tasks/main.yml \
  Makefile.inc pkg.spec.in guide.mdx just/justfile run.j2 package.json \
  .devcontainer/devcontainer.jsonc .vscode/tasks.json Dockerfile-dev \
  azure-pipelines-ci.yml .drone.yaml cloudbuild-prod.yaml .gitlab-ci.yaml \
  compose.override.yml; do
  (cd "$cases" && "$BIN" --lint --no-traces "$case_file" 2>&1) |
    grep -E '^[^ ].*: (error|warning): '
done

more="$root/more"
mkdir -p "$more/hooks" "$more/.vscode" "$more/make" "$more/compose"
cat > "$more/hooks/package.json" <<'EOF'
{
  "config": {"cmd": "if true; then"},
  "simple-git-hooks": {"pre-commit": "case x in"},
  "husky": {"hooks": {"pre-push": "while true; do"}},
  "lint-staged": {"*.js": "if true; then"}
}
EOF
cat > "$more/configmap.yaml" <<'EOF'
apiVersion: v1
kind: ConfigMap
data:
  entrypoint.sh: |
    #!/bin/sh
    if true; then
  settings.conf: |
    if true; then
EOF
cat > "$more/workflow.yaml" <<'EOF'
apiVersion: argoproj.io/v1alpha1
kind: Workflow
spec:
  templates:
  - name: a
    script:
      command: [bash]
      source: |
        case x in
EOF
cat > "$more/compose/compose.yaml" <<'EOF'
services:
  app:
    build:
      dockerfile_inline: |
        FROM alpine
        RUN if true; then
EOF
printf '%s\n' '.RECIPEPREFIX = >' 'all:' '> echo ok' '> case x in' \
  > "$more/make/Makefile"
cat > "$more/session.md" <<'MD'
```console
$ cat <<EOF > x
if true; then
EOF
output line
$ while true; do
```
MD
cat > "$more/.vscode/tasks.json" <<'EOF'
{"tasks": [
  {"label": "a", "type": "process", "command": "bash", "args": ["-c", "if true; then"]},
  {"label": "b", "type": "process", "command": "python3", "args": ["-c", "if True: pass"]}
]}
EOF
for more_file in hooks/package.json configmap.yaml workflow.yaml \
  compose/compose.yaml make/Makefile session.md .vscode/tasks.json; do
  (cd "$more" && "$BIN" --lint --no-traces "$more_file" 2>&1) |
    grep -E '^[^ ].*: (error|warning): '
done

fixes="$root/fixes"
mkdir -p "$fixes/.github/workflows" "$fixes/.vscode" "$fixes/compose" "$fixes/escape"
cat > "$fixes/.github/workflows/shebang.yml" <<'EOF'
jobs:
  a:
    runs-on: ubuntu-latest
    steps:
      - run: |
          #!/bin/bash
          if true;then echo hi;fi
EOF
cat > "$fixes/session.md" <<'MD'
```console
$ echo   hi
added 1 package (3s)
```

```sh
if true;then echo x;fi
```
MD
cat > "$fixes/compose/compose.yaml" <<'EOF'
services:
  app:
    command: echo "$FOO"
    build:
      dockerfile_inline: |
        FROM alpine
        ARG FOO
EOF
cat > "$fixes/escape/Dockerfile" <<'EOF'
# escapes a ` inside RUN lines
FROM alpine
SHELL ["python3", "-c"]
RUN <<EOT
RUN if true; then
EOT
SHELL ["/bin/sh", "-c"]
RUN case x in
EOF
cat > "$fixes/.vscode/tasks.json" <<'EOF'
{"tasks": [{"label": "w", "windows": {"command": "bash", "args": ["-c", "if ("]}}]}
EOF
(cd "$fixes" && "$BIN" --format .github/workflows/shebang.yml) |
  sed -n '6,11p' | sed 's/ *$/|/'
(cd "$fixes" && "$BIN" --format session.md)
for fix_file in compose/compose.yaml escape/Dockerfile .vscode/tasks.json; do
  (cd "$fixes" && "$BIN" --lint --no-traces "$fix_file" 2>&1) |
    grep -E '^[^ ].*: (error|warning): ' || true
done
