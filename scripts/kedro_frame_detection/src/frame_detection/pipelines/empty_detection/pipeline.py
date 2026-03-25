"""Empty frame detection pipeline definition."""

from kedro.pipeline import Pipeline, node, pipeline

from .nodes import (
    build_background_node,
    evaluate_node,
    load_dataset_node,
    log_mlflow_node,
    process_frames_node,
    save_artifacts_node,
)


def create_pipeline(**kwargs) -> Pipeline:
    return pipeline(
        [
            node(
                func=load_dataset_node,
                inputs="params:dataset_name",
                outputs="dataset_bundle",
                name="load_dataset",
            ),
            node(
                func=build_background_node,
                inputs="dataset_bundle",
                outputs="background",
                name="build_background",
            ),
            node(
                func=process_frames_node,
                inputs=["dataset_bundle", "background", "params:processing"],
                outputs="processing_results",
                name="process_frames",
            ),
            node(
                func=evaluate_node,
                inputs="processing_results",
                outputs="metrics",
                name="evaluate",
            ),
            node(
                func=save_artifacts_node,
                inputs=["processing_results", "metrics", "params:output_dir"],
                outputs="artifact_dir",
                name="save_artifacts",
            ),
            node(
                func=log_mlflow_node,
                inputs=[
                    "processing_results",
                    "metrics",
                    "artifact_dir",
                    "params:mlflow",
                ],
                outputs="mlflow_run_url",
                name="log_mlflow",
            ),
        ]
    )
