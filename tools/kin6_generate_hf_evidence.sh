#!/usr/bin/env bash
set -euo pipefail

output_dir="${1:-review_artifacts/KIN-6}"
generator="${2:-build/linux-backend/kin6_batch_pipeline_evidence}"
dataset="gavinlouuu/512x96stream"
config="default"
split="train"
length="${KIN6_HF_ROW_COUNT:-6}"

mkdir -p "${output_dir}/logs"

base_url="https://datasets-server.huggingface.co"
curl -sS "${base_url}/is-valid?dataset=${dataset}" > "${output_dir}/hf_is_valid.json"
curl -sS "${base_url}/splits?dataset=${dataset}" > "${output_dir}/hf_splits.json"
curl -sS "${base_url}/size?dataset=${dataset}" > "${output_dir}/hf_size.json"
curl -sS "${base_url}/rows?dataset=${dataset}&config=${config}&split=${split}&offset=0&length=${length}" > "${output_dir}/hf_rows.json"

mapfile -t image_urls < <(jq -r '.rows[].row.image.src' "${output_dir}/hf_rows.json")
mapfile -t image_widths < <(jq -r '.rows[].row.image.width' "${output_dir}/hf_rows.json")
mapfile -t image_heights < <(jq -r '.rows[].row.image.height' "${output_dir}/hf_rows.json")

if [[ "${#image_urls[@]}" -eq 0 ]]; then
    echo "No image rows returned for ${dataset} ${config}/${split}" >&2
    exit 1
fi

images=()
for i in "${!image_urls[@]}"; do
    image_path="${output_dir}/hf_row_$(printf '%03d' "${i}").jpg"
    curl -L -sS "${image_urls[$i]}" -o "${image_path}"
    images+=("${image_path}")
    echo "downloaded row ${i}: ${image_widths[$i]}x${image_heights[$i]} -> ${image_path}"
done

"${generator}" "${output_dir}" "${images[@]}"
