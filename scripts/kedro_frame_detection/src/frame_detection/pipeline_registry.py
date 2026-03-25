"""Project pipelines."""

from kedro.pipeline import Pipeline

from frame_detection.pipelines.empty_detection import pipeline as ed


def register_pipelines() -> dict[str, Pipeline]:
    empty_detection = ed.create_pipeline()
    return {
        "__default__": empty_detection,
        "empty_detection": empty_detection,
    }
