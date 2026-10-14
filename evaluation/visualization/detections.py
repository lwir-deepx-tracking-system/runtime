#!/usr/bin/env python3
"""Render benchmark detection and tracking boxes on LWIR sequence frames."""

from __future__ import annotations

import argparse
import csv
import math
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import DefaultDict, Iterable, Sequence

import cv2
import numpy as np


@dataclass(frozen=True)
class Detection:
    class_id: int
    confidence: float
    x: float
    y: float
    width: float
    height: float


@dataclass(frozen=True)
class Track:
    track_id: int
    confidence: float
    x: float
    y: float
    width: float
    height: float


Box = Detection | Track
Color = tuple[int, int, int]

DETECTION_COLOR: Color = (40, 220, 40)
TRACK_COLORS: tuple[Color, ...] = (
    (255, 120, 40),
    (40, 180, 255),
    (220, 80, 220),
    (80, 220, 220),
    (255, 80, 120),
    (120, 255, 80),
    (180, 120, 255),
    (255, 200, 80),
)

DEFAULT_DATASET_ROOT = Path("/mnt/lwir_data/datasets")
DEFAULT_BENCHMARK_ROOT = Path(
    "/mnt/lwir_data/results/benchmark/yolov8n_bytetrack"
)
DEFAULT_OUTPUT_ROOT = Path(
    "/mnt/lwir_data/results/visualization/yolov8n_bytetrack"
)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark detection/tracking 결과를 원본 LWIR 프레임에 표시해 "
            "프레임별 PNG로 저장합니다."
        )
    )
    parser.add_argument("run_id", help="benchmark 실행 ID (예: 20261007_190531)")
    parser.add_argument(
        "-s",
        "--sequence",
        required=True,
        help="표시할 sequence ID (예: seq_001)",
    )
    parser.add_argument(
        "--dataset-root",
        type=Path,
        default=DEFAULT_DATASET_ROOT,
        help=f"dataset root (기본값: {DEFAULT_DATASET_ROOT})",
    )
    parser.add_argument(
        "--benchmark-root",
        type=Path,
        default=DEFAULT_BENCHMARK_ROOT,
        help=f"benchmark root (기본값: {DEFAULT_BENCHMARK_ROOT})",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help=f"visualization 출력 root (기본값: {DEFAULT_OUTPUT_ROOT})",
    )
    parser.add_argument(
        "--view",
        choices=("side-by-side", "overlay", "detection", "tracking"),
        default="side-by-side",
        help="출력 구성 (기본값: side-by-side)",
    )
    parser.add_argument(
        "--clip-min",
        type=int,
        default=0,
        help="16-bit LWIR를 8-bit로 변환할 최소값 (기본값: 0)",
    )
    parser.add_argument(
        "--clip-max",
        type=int,
        default=65535,
        help="16-bit LWIR를 8-bit로 변환할 최대값 (기본값: 65535)",
    )
    return parser.parse_args(argv)


def _parse_int(value: str, path: Path, line_number: int, field: str) -> int:
    try:
        return int(value)
    except ValueError as exc:
        raise ValueError(f"{path}:{line_number}: {field}가 정수가 아닙니다: {value!r}") from exc


def _parse_float(value: str, path: Path, line_number: int, field: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise ValueError(f"{path}:{line_number}: {field}가 숫자가 아닙니다: {value!r}") from exc
    if not math.isfinite(parsed):
        raise ValueError(f"{path}:{line_number}: {field}가 유한한 값이 아닙니다: {value!r}")
    return parsed


def _validate_box(box: Box, path: Path, line_number: int) -> None:
    if box.width <= 0.0 or box.height <= 0.0:
        raise ValueError(
            f"{path}:{line_number}: bbox width와 height는 0보다 커야 합니다 "
            f"(width={box.width}, height={box.height})"
        )


def load_detections(path: Path) -> dict[int, list[Detection]]:
    """Read frame,class,confidence,x,y,width,height benchmark output."""
    by_frame: DefaultDict[int, list[Detection]] = defaultdict(list)
    with path.open(newline="", encoding="utf-8") as result_file:
        for line_number, row in enumerate(csv.reader(result_file), start=1):
            if not row:
                continue
            if len(row) != 7:
                raise ValueError(
                    f"{path}:{line_number}: detection 열은 7개여야 합니다 (현재 {len(row)}개)"
                )
            frame_id = _parse_int(row[0], path, line_number, "frame_id")
            if frame_id < 1:
                raise ValueError(f"{path}:{line_number}: frame_id는 1 이상이어야 합니다")
            detection = Detection(
                class_id=_parse_int(row[1], path, line_number, "class_id"),
                confidence=_parse_float(row[2], path, line_number, "confidence"),
                x=_parse_float(row[3], path, line_number, "x"),
                y=_parse_float(row[4], path, line_number, "y"),
                width=_parse_float(row[5], path, line_number, "width"),
                height=_parse_float(row[6], path, line_number, "height"),
            )
            _validate_box(detection, path, line_number)
            by_frame[frame_id].append(detection)
    return dict(by_frame)


def load_tracks(path: Path) -> dict[int, list[Track]]:
    """Read MOTChallenge frame,id,x,y,width,height,confidence,-1,-1,-1 output."""
    by_frame: DefaultDict[int, list[Track]] = defaultdict(list)
    with path.open(newline="", encoding="utf-8") as result_file:
        for line_number, row in enumerate(csv.reader(result_file), start=1):
            if not row:
                continue
            if len(row) != 10:
                raise ValueError(
                    f"{path}:{line_number}: tracking 열은 10개여야 합니다 (현재 {len(row)}개)"
                )
            frame_id = _parse_int(row[0], path, line_number, "frame_id")
            track_id = _parse_int(row[1], path, line_number, "track_id")
            if frame_id < 1:
                raise ValueError(f"{path}:{line_number}: frame_id는 1 이상이어야 합니다")
            if track_id < 0:
                raise ValueError(f"{path}:{line_number}: track_id는 0 이상이어야 합니다")
            track = Track(
                track_id=track_id,
                x=_parse_float(row[2], path, line_number, "x"),
                y=_parse_float(row[3], path, line_number, "y"),
                width=_parse_float(row[4], path, line_number, "width"),
                height=_parse_float(row[5], path, line_number, "height"),
                confidence=_parse_float(row[6], path, line_number, "confidence"),
            )
            _validate_box(track, path, line_number)
            by_frame[frame_id].append(track)
    return dict(by_frame)


def list_frames(image_dir: Path) -> list[tuple[int, Path]]:
    frames: list[tuple[int, Path]] = []
    for image_path in image_dir.glob("*.png"):
        if not image_path.stem.isdigit():
            raise ValueError(f"숫자가 아닌 frame 파일명입니다: {image_path.name}")
        frames.append((int(image_path.stem), image_path))

    frames.sort(key=lambda item: item[0])
    if not frames:
        raise ValueError(f"PNG frame이 없습니다: {image_dir}")

    frame_ids = [frame_id for frame_id, _ in frames]
    if len(frame_ids) != len(set(frame_ids)):
        raise ValueError(f"중복된 frame ID가 있습니다: {image_dir}")
    return frames


def lwir_to_bgr(image: np.ndarray, clip_min: int, clip_max: int, path: Path) -> np.ndarray:
    if image.dtype != np.uint16 or image.ndim != 2:
        raise ValueError(
            f"CV_16UC1 PNG가 아닙니다: {path} (dtype={image.dtype}, shape={image.shape})"
        )
    clipped = np.clip(image, clip_min, clip_max)
    scale = 255.0 / float(clip_max - clip_min)
    gray = cv2.convertScaleAbs(clipped, alpha=scale, beta=-clip_min * scale)
    return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)


def _track_color(track_id: int) -> Color:
    return TRACK_COLORS[track_id % len(TRACK_COLORS)]


def _draw_label(image: np.ndarray, text: str, x: int, y: int, color: Color) -> None:
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = 0.45
    thickness = 1
    (text_width, text_height), baseline = cv2.getTextSize(
        text, font, font_scale, thickness
    )
    label_top = max(0, y - text_height - baseline - 5)
    label_right = min(image.shape[1] - 1, x + text_width + 6)
    cv2.rectangle(image, (x, label_top), (label_right, y), color, cv2.FILLED)
    cv2.putText(
        image,
        text,
        (x + 3, max(text_height, y - baseline - 3)),
        font,
        font_scale,
        (0, 0, 0),
        thickness,
        cv2.LINE_AA,
    )


def draw_boxes(image: np.ndarray, boxes: Iterable[Box]) -> None:
    height, width = image.shape[:2]
    for box in boxes:
        x1 = max(0, min(width - 1, int(round(box.x))))
        y1 = max(0, min(height - 1, int(round(box.y))))
        x2 = max(0, min(width - 1, int(round(box.x + box.width))))
        y2 = max(0, min(height - 1, int(round(box.y + box.height))))
        if x2 <= x1 or y2 <= y1:
            continue

        if isinstance(box, Detection):
            color = DETECTION_COLOR
            label = f"D cls={box.class_id} {box.confidence:.2f}"
        else:
            color = _track_color(box.track_id)
            label = f"T id={box.track_id} {box.confidence:.2f}"

        cv2.rectangle(image, (x1, y1), (x2, y2), color, 2, cv2.LINE_AA)
        _draw_label(image, label, x1, y1, color)


def add_header(image: np.ndarray, text: str) -> np.ndarray:
    header_height = 30
    output = cv2.copyMakeBorder(
        image, header_height, 0, 0, 0, cv2.BORDER_CONSTANT, value=(24, 24, 24)
    )
    cv2.putText(
        output,
        text,
        (8, 21),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (240, 240, 240),
        1,
        cv2.LINE_AA,
    )
    return output


def render_frame(
    base: np.ndarray,
    frame_id: int,
    detections: Sequence[Detection],
    tracks: Sequence[Track],
    view: str,
) -> np.ndarray:
    if view == "detection":
        output = base.copy()
        draw_boxes(output, detections)
        return add_header(output, f"frame {frame_id} | detections: {len(detections)}")

    if view == "tracking":
        output = base.copy()
        draw_boxes(output, tracks)
        return add_header(output, f"frame {frame_id} | tracks: {len(tracks)}")

    if view == "overlay":
        output = base.copy()
        draw_boxes(output, detections)
        draw_boxes(output, tracks)
        return add_header(
            output,
            f"frame {frame_id} | detections: {len(detections)} | tracks: {len(tracks)}",
        )

    detection_panel = base.copy()
    tracking_panel = base.copy()
    draw_boxes(detection_panel, detections)
    draw_boxes(tracking_panel, tracks)
    detection_panel = add_header(
        detection_panel, f"frame {frame_id} | detections: {len(detections)}"
    )
    tracking_panel = add_header(tracking_panel, f"frame {frame_id} | tracks: {len(tracks)}")
    return cv2.hconcat((detection_panel, tracking_panel))


def resolve_paths(args: argparse.Namespace) -> tuple[Path, Path, Path, Path]:
    if Path(args.run_id).name != args.run_id or Path(args.sequence).name != args.sequence:
        raise ValueError("run_id와 sequence는 경로가 아닌 디렉터리 이름이어야 합니다")

    run_dir = args.benchmark_root / args.run_id
    image_dir = args.dataset_root / args.sequence / "images"
    detection_path = run_dir / "detections" / f"{args.sequence}.txt"
    track_path = run_dir / "tracks" / f"{args.sequence}.txt"
    output_dir = args.output_root / args.run_id / args.sequence

    for path, description in (
        (image_dir, "image 디렉터리"),
        (detection_path, "detection 결과"),
        (track_path, "tracking 결과"),
    ):
        if not path.exists():
            raise FileNotFoundError(f"{description}가 없습니다: {path}")

    if not 0 <= args.clip_min < args.clip_max <= 65535:
        raise ValueError("clip 범위는 0 <= clip-min < clip-max <= 65535여야 합니다")

    if output_dir.exists() and any(output_dir.iterdir()):
        raise FileExistsError(f"출력 디렉터리가 비어 있지 않습니다: {output_dir}")
    return image_dir, detection_path, track_path, output_dir


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        image_dir, detection_path, track_path, output_dir = resolve_paths(args)
        frames = list_frames(image_dir)
        detections = load_detections(detection_path)
        tracks = load_tracks(track_path)

        available_frame_ids = {frame_id for frame_id, _ in frames}
        unknown_result_ids = (set(detections) | set(tracks)) - available_frame_ids
        if unknown_result_ids:
            first_unknown = min(unknown_result_ids)
            raise ValueError(f"원본 이미지가 없는 결과 frame_id가 있습니다: {first_unknown}")

        output_dir.mkdir(parents=True, exist_ok=True)
        for frame_id, image_path in frames:
            raw = cv2.imread(str(image_path), cv2.IMREAD_UNCHANGED)
            if raw is None:
                raise ValueError(f"이미지를 읽지 못했습니다: {image_path}")
            base = lwir_to_bgr(raw, args.clip_min, args.clip_max, image_path)
            output = render_frame(
                base,
                frame_id,
                detections.get(frame_id, ()),
                tracks.get(frame_id, ()),
                args.view,
            )
            output_path = output_dir / image_path.name
            if not cv2.imwrite(str(output_path), output):
                raise OSError(f"출력 이미지를 저장하지 못했습니다: {output_path}")

        print(f"완료: {len(frames)}개 프레임을 {output_dir}에 저장했습니다.")
        return 0
    except (FileNotFoundError, FileExistsError, OSError, ValueError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
