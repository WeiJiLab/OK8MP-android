#!/bin/bash
DEFAULT_BRANCH="aosp_kernel-common-android11-5.4"

# Examples:
# to update
# * kernel from common and goldfish modules (recommended):
#   ./update_54_kernel.sh --bug 123 --bid 6332140
# * only goldfish modules:
#   ./update_54_kernel.sh --bug 123 --bid 6332140 --update modules
# * only kernel (common):
#   ./update_54_kernel.sh --bug 123 --bid 6332140 --update kernel

set -e
set -o errexit
source gbash.sh

DEFINE_int bug 0 "Bug with the reason for the update"
DEFINE_int bid 0 "Build id for goldfish modules"
DEFINE_string update "both" "Select which prebuilts to update, (kernel|modules|both)"
DEFINE_string kernel "common" "Select which kernel to fetch, (common|goldfish)"
DEFINE_string branch "${DEFAULT_BRANCH}" "Branch for fetch_artifact"
DEFINE_string change_id "" "AOSP/master Change-Id"

fetch_arch() {
  scratch_dir="${1}"
  branch="${2}"
  bid="${3}"
  do_fetch_kernel="${4}"
  do_fetch_modules="${5}"
  kernel_target="${6}"
  kernel_artifact="${7}"
  modules_target="${8}"

  mkdir "${scratch_dir}"
  pushd "${scratch_dir}"

  if [[ "${do_fetch_kernel}" -ne 0 ]]; then
    /google/data/ro/projects/android/fetch_artifact \
      --bid "${bid}" \
      --target "${kernel_target}" \
      --branch "${branch}" \
      "${kernel_artifact}"
  fi

  if [[ "${do_fetch_modules}" -ne 0 ]]; then
    /google/data/ro/projects/android/fetch_artifact \
      --bid "${bid}" \
      --target "${modules_target}" \
      --branch "${branch}" \
      "*.ko"
  fi

  popd
}

move_artifacts() {
  scratch_dir="${1}"
  dst_dir="${2}"
  kernel_artifact="${3}"
  do_fetch_modules="${4}"

  pushd "${scratch_dir}"

  if [[ -f "${kernel_artifact}" ]]; then
    mv "${kernel_artifact}" "${dst_dir}/kernel-qemu2"
  fi

  if [[ "${do_fetch_modules}" -ne 0 ]]; then
    rm -rf "${dst_dir}/ko-new"
    rm -rf "${dst_dir}/ko-old"
    mkdir "${dst_dir}/ko-new"
    mv *.ko "${dst_dir}/ko-new"
    mv "${dst_dir}/ko" "${dst_dir}/ko-old"
    mv "${dst_dir}/ko-new" "${dst_dir}/ko"
    rm -rf "${dst_dir}/ko-old"
  fi

  popd
}

make_git_commit() {
  x86_dst_dir="${1}"
  arm_dst_dir="${2}"

  git add "${x86_dst_dir}"
  git add "${arm_dst_dir}"

  git commit -a -m "$(
  echo Update kernel prebuilts to ${FLAGS_branch}/${FLAGS_bid}
  echo
  echo kernel: ${FLAGS_kernel}
  echo update: ${FLAGS_update}
  echo
  echo Test: TreeHugger
  echo Bug: ${FLAGS_bug}
  echo Change-Id: ${FLAGS_change_id}
  echo Merged-In: ${FLAGS_change_id}
  )"

  git commit --amend -s
}

main() {
  fail=0

  if [[ "${FLAGS_bug}" -eq 0 ]]; then
    echo "Must specify --bug" 1>&2
    fail=1
  fi

  if [[ "${FLAGS_bid}" -eq 0 ]]; then
    echo "Must specify --bid" 1>&2
    fail=1
  fi

  if [[ -z "${FLAGS_change_id}" ]]; then
    echo "Must specify --change_id, use Change-Id from your AOSP change" 1>&2
    fail=1
  fi

  do_fetch_kernel=0
  do_fetch_modules=0
  case "${FLAGS_update}" in
    both)
      do_fetch_kernel=1
      do_fetch_modules=1
      ;;
    kernel)
      do_fetch_kernel=1
      ;;
    modules)
      do_fetch_modules=1
      ;;
    *)
      echo "Unexpected value for --update, '${FLAGS_update}'" 1>&2
      fail=1
      ;;
  esac

  kernel_target_x86=""
  kernel_target_aarch=""
  case "${FLAGS_kernel}" in
    common)
      kernel_target_x86="kernel_x86_64"
      kernel_target_aarch="kernel_aarch64"
      ;;
    goldfish)
      echo "WARNING: goldfish kernel is not recommended"
      kernel_target_x86="kernel_gf_x86_64"
      kernel_target_aarch="kernel_gf_aarch64"
      ;;
    *)
      echo "Unexpected value for --kernel, '${FLAGS_kernel}'" 1>&2
      fail=1
      ;;
  esac

  if [[ "${fail}" -ne 0 ]]; then
    exit "${fail}"
  fi

  here="$(pwd)"
  x86_dst_dir="${here}/x86_64/5.4"
  arm_dst_dir="${here}/arm64/5.4"

  scratch_dir="$(mktemp -d)"
  x86_scratch_dir="${scratch_dir}/x86"
  arm_scratch_dir="${scratch_dir}/arm"

  fetch_arch "${x86_scratch_dir}" \
    "${FLAGS_branch}" "${FLAGS_bid}" \
    "${do_fetch_kernel}" ${do_fetch_modules} \
    "${kernel_target_x86}" "bzImage" \
    "kernel_gf_x86_64"

  fetch_arch "${arm_scratch_dir}" \
    "${FLAGS_branch}" "${FLAGS_bid}" \
    "${do_fetch_kernel}" ${do_fetch_modules} \
    "${kernel_target_aarch}" "Image.gz" \
    "kernel_gf_aarch64"

  move_artifacts "${x86_scratch_dir}" "${x86_dst_dir}" "bzImage" "${do_fetch_modules}"
  move_artifacts "${arm_scratch_dir}" "${arm_dst_dir}" "Image.gz" "${do_fetch_modules}"

  make_git_commit "${x86_dst_dir}" "${arm_dst_dir}"
}

gbash::main "$@"

