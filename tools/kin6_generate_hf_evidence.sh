#!/usr/bin/env bash
set -euo pipefail

output_dir="${1:-review_artifacts/KIN-6}"
generator="${2:-build/linux-backend/kin6_batch_pipeline_evidence}"
dataset="gavinlouuu/512x96stream"
config="default"
split="train"
base_url="https://datasets-server.huggingface.co"
page_length="${KIN6_HF_PAGE_LENGTH:-100}"
page_delay="${KIN6_HF_PAGE_DELAY_SECONDS:-0.25}"
parallelism="${KIN6_DOWNLOAD_PARALLELISM:-8}"

mkdir -p "${output_dir}/logs"
find "${output_dir}" -maxdepth 1 -type f \( \
    -name 'hf_row_*.jpg' -o \
    -name 'hf_input_sample.png' -o \
    -name 'processed_mask_sample.png' -o \
    -name 'contour_overlay_sample.png' -o \
    -name 'metrics.json' -o \
    -name 'hf_rows.json' -o \
    -name 'hf_rows_preview.json' -o \
    -name 'hf_download_manifest.tsv' -o \
    -name 'hf_image_downloads.tsv' \
\) -delete
tmp_dir="${output_dir}/.tmp_hf_pages"
rm -rf "${tmp_dir}"
mkdir -p "${tmp_dir}"
trap 'rm -rf "${tmp_dir}"' EXIT

curl_args=(--fail --retry 10 --retry-delay 10 --retry-all-errors -sS)

curl "${curl_args[@]}" "${base_url}/is-valid?dataset=${dataset}" > "${output_dir}/hf_is_valid.json"
curl "${curl_args[@]}" "${base_url}/splits?dataset=${dataset}" > "${output_dir}/hf_splits.json"
curl "${curl_args[@]}" "${base_url}/size?dataset=${dataset}" > "${output_dir}/hf_size.json"

total_rows="$(
    jq -r --arg config "${config}" --arg split "${split}" \
        '.size.splits[] | select(.config == $config and .split == $split) | .num_rows' \
        "${output_dir}/hf_size.json"
)"

if [[ -z "${total_rows}" || "${total_rows}" == "null" ]]; then
    echo "Could not determine row count for ${dataset} ${config}/${split}" >&2
    exit 1
fi

requested_rows="${KIN6_HF_ROW_COUNT:-${total_rows}}"
if (( requested_rows > total_rows )); then
    echo "Requested ${requested_rows} rows, but ${dataset} ${config}/${split} only has ${total_rows}" >&2
    exit 1
fi

if (( page_length < 1 || page_length > 100 )); then
    echo "KIN6_HF_PAGE_LENGTH must be between 1 and 100 for Dataset Viewer rows API" >&2
    exit 1
fi

page_files=()
for (( offset = 0; offset < requested_rows; offset += page_length )); do
    length="${page_length}"
    if (( offset + length > requested_rows )); then
        length="$(( requested_rows - offset ))"
    fi
    page_file="${tmp_dir}/rows_${offset}.json"
    curl "${curl_args[@]}" "${base_url}/rows?dataset=${dataset}&config=${config}&split=${split}&offset=${offset}&length=${length}" > "${page_file}"
    jq empty "${page_file}"
    page_files+=("${page_file}")
    echo "fetched rows offset=${offset} length=${length}"
    sleep "${page_delay}"
done

jq -s '{
    features: (.[0].features // []),
    rows: ([.[].rows[]]),
    num_rows_total: (.[0].num_rows_total // null),
    num_rows_per_page: (.[0].num_rows_per_page // null),
    partial: ([.[].partial] | any)
}' "${page_files[@]}" > "${output_dir}/hf_rows.json"

jq '{
    features,
    rows: .rows[:6],
    num_rows_total,
    num_rows_per_page,
    partial
}' "${output_dir}/hf_rows.json" > "${output_dir}/hf_rows_preview.json"

jq -r '.rows[] | [.row_idx, .row.image.src, .row.image.width, .row.image.height] | @tsv' \
    "${output_dir}/hf_rows.json" > "${output_dir}/hf_download_manifest.tsv"

: > "${output_dir}/hf_image_downloads.tsv"
while IFS=$'\t' read -r row_idx image_url width height; do
    image_path="${output_dir}/hf_row_$(printf '%05d' "${row_idx}").jpg"
    printf '%s\t%s\t%s\t%s\t%s\n' "${image_path}" "${image_url}" "${row_idx}" "${width}" "${height}" \
        >> "${output_dir}/hf_image_downloads.tsv"
done < "${output_dir}/hf_download_manifest.tsv"

awk -F '\t' '{ print $1 "\t" $2 }' "${output_dir}/hf_image_downloads.tsv" |
    xargs -P "${parallelism}" -n 2 sh -c 'curl --fail --retry 10 --retry-delay 10 --retry-all-errors -L -sS "$2" -o "$1"' _

downloaded_count="$(wc -l < "${output_dir}/hf_image_downloads.tsv")"
echo "downloaded ${downloaded_count} image rows into ${output_dir}"

mapfile -t images < <(cut -f1 "${output_dir}/hf_image_downloads.tsv")
"${generator}" "${output_dir}" "${images[@]}"
