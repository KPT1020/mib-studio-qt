#!/usr/bin/env bash
set -euo pipefail

die() {
  echo "error: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

infer_version() {
  if [[ -n "${MIB_APP_VERSION:-}" ]]; then
    printf '%s' "${MIB_APP_VERSION}"
    return
  fi

  if git describe --tags --abbrev=0 >/dev/null 2>&1; then
    git describe --tags --abbrev=0 | sed 's/^v//'
    return
  fi

  printf '0.0.0'
}

infer_sha() {
  local sha="${MIB_GIT_SHA:-${GITHUB_SHA:-${CI_COMMIT_SHA:-${BUILD_SOURCEVERSION:-}}}}"
  if [[ -z "${sha}" ]]; then
    sha="$(git rev-parse --short=12 HEAD)"
  fi
  printf '%.12s' "${sha}"
}

build_release_name() {
  local component="$1"
  local version="$2"
  local sha="$3"
  if [[ -z "${sha}" ]]; then
    printf '%s@%s' "${component}" "${version}"
  else
    printf '%s@%s+%s' "${component}" "${version}" "${sha}"
  fi
}

parse_projects() {
  local projects_csv="$1"
  local token
  local -a result=()

  IFS=',' read -r -a raw_projects <<<"${projects_csv}"
  for token in "${raw_projects[@]}"; do
    token="$(echo "${token}" | xargs)"
    [[ -z "${token}" ]] && continue
    result+=("${token}")
  done

  if [[ "${#result[@]}" -eq 0 ]]; then
    die "no valid projects found in SENTRY_PROJECTS/SENTRY_PROJECT"
  fi

  printf '%s\n' "${result[@]}"
}

main() {
  require_cmd sentry-cli
  require_cmd git

  local component="${MIB_SENTRY_COMPONENT:-mib-studio-qt/desktop}"
  local version
  version="$(infer_version)"
  local sha
  sha="$(infer_sha)"

  local default_release
  default_release="$(build_release_name "${component}" "${version}" "${sha}")"
  local release="${MIB_SENTRY_RELEASE:-${SENTRY_RELEASE:-${default_release}}}"
  local environment="${MIB_CRASH_ENV:-${SENTRY_ENVIRONMENT:-production}}"
  local projects_input="${SENTRY_PROJECTS:-${SENTRY_PROJECT:-}}"
  [[ -n "${projects_input}" ]] || die "set SENTRY_PROJECT or SENTRY_PROJECTS (comma-separated)"

  mapfile -t projects < <(parse_projects "${projects_input}")

  local -a project_args=()
  local project
  for project in "${projects[@]}"; do
    project_args+=("-p" "${project}")
  done

  echo "Creating Sentry release: ${release}"
  sentry-cli releases new "${project_args[@]}" "${release}"

  if ! sentry-cli releases set-commits "${release}" --auto --ignore-missing; then
    echo "warn: automatic commit association failed; retrying with --local" >&2
    sentry-cli releases set-commits "${release}" --local || true
  fi

  if [[ "$#" -gt 0 ]]; then
    echo "Uploading debug files: $*"
    sentry-cli debug-files upload "$@"
  fi

  sentry-cli releases finalize "${release}"
  sentry-cli releases deploys "${release}" new -e "${environment}"
  echo "Sentry release ready: ${release}"
  echo "Set this at runtime so crash events match release metadata:"
  echo "  export SENTRY_RELEASE=${release}"
}

main "$@"
